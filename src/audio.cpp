#include <initguid.h>
#include "audio.h"
#include "yt.h"
#include "media_abi.h"
#include "mp4.h"
#include "playback_clock.h"
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>

namespace {

struct Song {
    wstring path, part;
    long long total = 0;
    std::atomic<long long> have{0};
    std::atomic<bool> cancel{false};
};

bool matchesLength(const wstring& path, long long length) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           (((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow) == (uint64_t)length;
}

bool commitAudio(const Song& song) {
    return !song.cancel.load() && normalizeMp4ForWindows(song.part.c_str()) && !song.cancel.load() &&
           MoveFileExW(song.part.c_str(), song.path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool migrateAudio(Song& song, const wstring& legacy) {
    if (song.cancel.load() || !matchesLength(legacy, song.total)) return false;
    bool migrated = CopyFileW(legacy.c_str(), song.part.c_str(), FALSE) && commitAudio(song);
    if (migrated) {
        song.have.store(song.total);
        DeleteFileW(legacy.c_str());
    } else {
        DeleteFileW(song.part.c_str());
    }
    return migrated;
}

bool download(Song& song, const string& url, const string& agent) {
    if (song.cancel.load()) return false;
    HANDLE file = CreateFileW(song.part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    bool pulled = yt::pullAudio(url, agent, song.total, [&](const char* data, size_t count) {
        DWORD written = 0;
        if (!WriteFile(file, data, (DWORD)count, &written, nullptr) || written != count) return false;
        song.have += written;
        return true;
    }, &song.cancel);

    CloseHandle(file);
    bool ok = pulled && commitAudio(song);
    if (!ok) DeleteFileW(song.part.c_str());
    return ok;
}

// Keeps the cache small: the songs played longest ago go first.
void trimCache() {
    struct Entry { wstring path; ULONGLONG used; };
    std::vector<Entry> files;
    WIN32_FIND_DATAW found;
    // Ignore recent partial downloads, including the one that may already be starting.
    HANDLE parts = FindFirstFileW((cacheDir() + L"\\*.part").c_str(), &found);
    if (parts != INVALID_HANDLE_VALUE) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER time;
        time.LowPart = now.dwLowDateTime;
        time.HighPart = now.dwHighDateTime;
        do {
            ULARGE_INTEGER written;
            written.LowPart = found.ftLastWriteTime.dwLowDateTime;
            written.HighPart = found.ftLastWriteTime.dwHighDateTime;
            constexpr ULONGLONG day = 86400ULL * 10000000;
            if (written.QuadPart + day < time.QuadPart) DeleteFileW((cacheDir() + L"\\" + found.cFileName).c_str());
        } while (FindNextFileW(parts, &found));
        FindClose(parts);
    }
    HANDLE find = FindFirstFileW((cacheDir() + L"\\*.m4a").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        ULARGE_INTEGER t;
        t.LowPart = found.ftLastWriteTime.dwLowDateTime;
        t.HighPart = found.ftLastWriteTime.dwHighDateTime;
        files.push_back({cacheDir() + L"\\" + found.cFileName, t.QuadPart});
    } while (FindNextFileW(find, &found));
    FindClose(find);
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.used > b.used; });
    for (size_t i = 12; i < files.size(); i++) DeleteFileW(files[i].path.c_str());
}

HWND notifyWindow;
UINT notifyMessage;

using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::Media::Playback;
using ABI::Windows::Foundation::IClosable;
using ABI::Windows::Foundation::ITypedEventHandler;
using media_abi::TimeSpan;

template <typename T>
HRESULT factoryFor(const wchar_t* name, REFIID id, ComPtr<T>& factory) {
    HSTRING_HEADER header;
    HSTRING runtimeClass = nullptr;
    HRESULT hr = WindowsCreateStringReference(name, (UINT32)wcslen(name), &header, &runtimeClass);
    return FAILED(hr) ? hr : RoGetActivationFactory(runtimeClass, id, (void**)factory.GetAddressOf());
}

template <typename T>
HRESULT query(IUnknown* object, REFIID id, ComPtr<T>& result) {
    return object->QueryInterface(id, (void**)result.GetAddressOf());
}

template <typename Handler, typename Args> struct Notify : Handler {
    const HWND window;
    const UINT message;
    const LPARAM token;
    const AudioEvent event;
    LONG refs = 1;

    Notify(LPARAM token, AudioEvent event) : window(notifyWindow), message(notifyMessage), token(token), event(event) {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(Handler) || id == __uuidof(IAgileObject)) {
            *out = static_cast<Handler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP Invoke(IMediaPlayer*, Args*) override {
        PostMessageW(window, message, (WPARAM)event, token);
        return S_OK;
    }
};

using MediaEvent = ITypedEventHandler<MediaPlayer*, IInspectable*>;
using FailureEvent = ITypedEventHandler<MediaPlayer*, MediaPlayerFailedEventArgs*>;

struct Voice {
    ComPtr<IMediaPlayer> player;
    ComPtr<media_abi::Session> session;
    EventRegistrationToken opened{}, ended{}, failed{};
    bool hasOpened = false, hasEnded = false, hasFailed = false;
    double expectedDuration = 0;
    bool atEnd = false;
    bool requested = false;
    LPARAM token = 0;
    PlaybackClock clock;

    ~Voice() {
        if (!player) return;
        if (hasOpened) player->remove_MediaOpened(opened);
        if (hasEnded) player->remove_MediaEnded(ended);
        if (hasFailed) player->remove_MediaFailed(failed);
        ComPtr<IClosable> closable;
        if (SUCCEEDED(player.As(&closable))) closable->Close();
    }
};

std::unique_ptr<Voice> voice;
std::shared_ptr<Song> current;
bool started;

ComPtr<IAgileReference> prepareSource(const Song& song) {
    ComPtr<IAgileReference> result;
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) return result;
    {
        ComPtr<media_abi::StorageStatics> storage;
        ComPtr<media_abi::FileOperation> operation;
        ComPtr<IAsyncInfo> info;
        ComPtr<media_abi::StorageFile> file;
        HSTRING_HEADER header;
        HSTRING path = nullptr;
        HRESULT hr = factoryFor(L"Windows.Storage.StorageFile", media_abi::storageStaticsId, storage);
        if (SUCCEEDED(hr)) hr = WindowsCreateStringReference(song.path.c_str(), (UINT32)song.path.size(), &header, &path);
        if (SUCCEEDED(hr)) hr = storage->GetFileFromPathAsync(path, operation.GetAddressOf());
        if (SUCCEEDED(hr)) hr = operation.As(&info);
        AsyncStatus status = AsyncStatus::Started;
        long long deadline = nowMs() + 10000;
        while (SUCCEEDED(hr) && status == AsyncStatus::Started && !song.cancel.load() && nowMs() < deadline) {
            hr = info->get_Status(&status);
            if (SUCCEEDED(hr) && status == AsyncStatus::Started) Sleep(15);
        }
        if (SUCCEEDED(hr) && status == AsyncStatus::Completed && !song.cancel.load()) {
            hr = operation->GetResults(file.GetAddressOf());
            ComPtr<media_abi::SourceStatics> factory;
            ComPtr<IInspectable> source, playbackSource;
            if (SUCCEEDED(hr)) hr = factoryFor(L"Windows.Media.Core.MediaSource", media_abi::sourceStaticsId, factory);
            if (SUCCEEDED(hr)) hr = factory->CreateFromStorageFile(file.Get(), source.GetAddressOf());
            if (SUCCEEDED(hr)) hr = query(source.Get(), media_abi::playbackSourceId, playbackSource);
            if (SUCCEEDED(hr)) RoGetAgileReference(AGILEREFERENCE_DEFAULT, media_abi::playbackSourceId, playbackSource.Get(), result.GetAddressOf());
        } else if (info && status == AsyncStatus::Started) {
            info->Cancel();
        }
    }
    RoUninitialize();
    return result;
}

HRESULT setSource(Voice& next, IAgileReference* reference) {
    HSTRING_HEADER header;
    HSTRING runtimeClass = nullptr;
    const wchar_t name[] = L"Windows.Media.Playback.MediaPlayer";
    HRESULT hr = WindowsCreateStringReference(name, (UINT32)std::size(name) - 1, &header, &runtimeClass);
    ComPtr<IInspectable> object;
    if (SUCCEEDED(hr)) hr = RoActivateInstance(runtimeClass, object.GetAddressOf());
    if (SUCCEEDED(hr)) hr = object.As(&next.player);
    ComPtr<media_abi::Player3> player3;
    if (SUCCEEDED(hr)) hr = query(next.player.Get(), media_abi::player3Id, player3);
    if (SUCCEEDED(hr)) hr = player3->get_PlaybackSession(next.session.GetAddressOf());
    if (FAILED(hr)) return hr;

    ComPtr<media_abi::CommandManager> commands;
    if (SUCCEEDED(player3->get_CommandManager(commands.GetAddressOf()))) commands->put_IsEnabled(false);
    ComPtr<IMediaPlayer2> settings;
    if (SUCCEEDED(next.player.As(&settings))) settings->put_AudioCategory(MediaPlayerAudioCategory_Media);
    next.player->put_AutoPlay(false);

    ComPtr<MediaEvent> opened, ended;
    ComPtr<FailureEvent> failed;
    opened.Attach(new Notify<MediaEvent, IInspectable>(next.token, AudioEvent::Ready));
    ended.Attach(new Notify<MediaEvent, IInspectable>(next.token, AudioEvent::Ended));
    failed.Attach(new Notify<FailureEvent, IMediaPlayerFailedEventArgs>(next.token, AudioEvent::Failed));
    hr = next.player->add_MediaOpened(opened.Get(), &next.opened);
    next.hasOpened = SUCCEEDED(hr);
    if (SUCCEEDED(hr)) {
        hr = next.player->add_MediaEnded(ended.Get(), &next.ended);
        next.hasEnded = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        hr = next.player->add_MediaFailed(failed.Get(), &next.failed);
        next.hasFailed = SUCCEEDED(hr);
    }

    ComPtr<media_abi::PlayerSource> input;
    ComPtr<IInspectable> source;
    if (SUCCEEDED(hr)) hr = query(next.player.Get(), media_abi::playerSourceId, input);
    if (SUCCEEDED(hr)) hr = reference->Resolve(media_abi::playbackSourceId, (void**)source.GetAddressOf());
    return FAILED(hr) ? hr : input->put_Source(source.Get());
}

void openFile(const std::shared_ptr<Song>& song, LPARAM token, double expectedDuration) {
    async<ComPtr<IAgileReference>>([song] { return prepareSource(*song); }, [song, token, expectedDuration](ComPtr<IAgileReference> source) {
        if (current != song) return;
        auto next = std::make_unique<Voice>();
        next->token = token;
        next->expectedDuration = expectedDuration;
        if (source && SUCCEEDED(setSource(*next, source.Get()))) voice = std::move(next);
        else PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Failed, token);
    });
}

}   // namespace

bool audio::start(HWND hwnd, UINT message) {
    notifyWindow = hwnd;
    notifyMessage = message;
    if (started) return true;
    started = SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED));
    if (started) background(trimCache);
    return started;
}

void audio::close() {
    if (current) {
        current->cancel.store(true);
        current.reset();
    }
    voice.reset();
}

void audio::stop() {
    close();
    if (started) RoUninitialize();
    started = false;
}

void audio::discard() {
    wstring path = current ? current->path : wstring();
    close();
    if (!path.empty()) DeleteFileW(path.c_str());
}

void audio::open(const string& videoId, const string& url, const string& userAgent, long long length, LPARAM token, double expectedDuration) {
    close();
    if (!started) {
        PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Unavailable, token);
        return;
    }

    auto song = std::make_shared<Song>();
    wstring cacheBase = cacheDir() + L"\\" + W(videoId);
    song->path = cacheBase + L".playable.m4a";
    song->part = song->path + L"." + std::to_wstring(token) + L".part";
    song->total = length;
    current = song;

    if (matchesLength(song->path, length)) {
        song->have.store(length);
        HANDLE touch = CreateFileW(song->path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (touch != INVALID_HANDLE_VALUE) {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            SetFileTime(touch, nullptr, nullptr, &now);
            CloseHandle(touch);
        }
        openFile(song, token, expectedDuration);
        return;
    }

    wstring legacy = cacheBase + L".verified.m4a";
    async<bool>([song, legacy, url, userAgent] {
        return migrateAudio(*song, legacy) || download(*song, url, userAgent);
    }, [song, token, expectedDuration](bool ok) {
        if (current != song) return;
        if (ok) openFile(song, token, expectedDuration);
        else PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::DownloadFailed, token);
    });
}

void audio::prefetch(const string& videoId, const string& url, const string& userAgent, long long length) {
    auto song = std::make_shared<Song>();
    song->path = cacheDir() + L"\\" + W(videoId) + L".playable.m4a";
    song->part = song->path + L".next.part";
    song->total = length;
    if (length <= 0 || matchesLength(song->path, length)) return;
    background([song, url, userAgent] { download(*song, url, userAgent); });
}

void audio::play() {
    if (!voice) return;
    if (voice->atEnd) seek(0);
    bool ok = SUCCEEDED(voice->player->Play());
    if (ok) {
        voice->requested = true;
        observePlayback();
    }
    PostMessageW(notifyWindow, notifyMessage, (WPARAM)(ok ? AudioEvent::Playing : AudioEvent::Failed), voice->token);
}

void audio::pause() {
    if (!voice) return;
    voice->requested = false;
    voice->player->Pause();
    voice->clock.pause(nowMs());
    PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Paused, voice->token);
}

void audio::seek(double seconds) {
    if (voice && SUCCEEDED(voice->session->put_Position(TimeSpan{(long long)(seconds * 1e7)}))) {
        voice->atEnd = false;
        voice->clock.seek(seconds, nowMs());
    }
}

double audio::position() {
    TimeSpan position{};
    return voice && SUCCEEDED(voice->session->get_Position(&position)) ? position.Duration / 1e7 : 0;
}

double audio::duration() {
    TimeSpan duration{};
    if (!voice) return 0;
    if (SUCCEEDED(voice->session->get_NaturalDuration(&duration)) && duration.Duration > 0)
        return duration.Duration / 1e7;
    return voice->expectedDuration;
}

bool audio::finished() {
    if (!voice || !voice->requested) return false;
    double length = duration();
    if (voice->expectedDuration > 2 && length < voice->expectedDuration * 0.8)
        return false;
    voice->atEnd = voice->clock.finished(length, nowMs());
    return voice->atEnd;
}

void audio::observePlayback() {
    if (!voice || !voice->requested) return;
    int state = 0;
    if (FAILED(voice->session->get_PlaybackState(&state))) return;
    // MediaPlaybackState.Playing is 3. Buffering and pauses don't count as playback.
    if (state == 3) voice->clock.play(nowMs());
    else voice->clock.pause(nowMs());
}

bool audio::paused() { return !voice || !voice->requested; }
void audio::setVolume(double volume) { if (voice) voice->player->put_Volume(std::clamp(volume, 0.0, 1.0)); }
void audio::setRate(double rate) {
    if (voice && SUCCEEDED(voice->session->put_PlaybackRate(rate))) voice->clock.speed(rate, nowMs());
}

double audio::buffered() {
    return current && current->total > 0 ? (double)current->have.load() / current->total : 0;
}
