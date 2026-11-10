#include <initguid.h>
#include "audio.h"
#include <condition_variable>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <mfmediaengine.h>

namespace {

// ---------------------------------------------------------------- A song being downloaded

// The file grows as the download runs; readers wait for the part they need.
struct Song {
    std::mutex m;
    std::condition_variable cv;
    wstring path, finalPath;   // downloaded as a .part file, renamed when whole
    HANDLE reader = INVALID_HANDLE_VALUE;
    long long total = 0, have = 0;
    bool failed = false;
    std::atomic<bool> cancel{false};

    ~Song() { if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader); }

    // Reads up to [count] bytes at [offset]; waits for the download to get there.
    HRESULT read(long long offset, BYTE* into, ULONG count, ULONG* got) {
        *got = 0;
        if (offset >= total) return S_OK;
        long long want = std::min<long long>(count, total - offset);
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return have >= offset + std::min<long long>(want, 65536) || failed || cancel.load(); });
            if (cancel.load()) return E_ABORT;
            if (have <= offset) return failed ? E_FAIL : S_OK;
            want = std::min<long long>(want, have - offset);
        }
        OVERLAPPED at = {};
        at.Offset = (DWORD)(offset & 0xFFFFFFFF);
        at.OffsetHigh = (DWORD)(offset >> 32);
        DWORD n = 0;
        if (!ReadFile(reader, into, (DWORD)want, &n, &at) && GetLastError() != ERROR_HANDLE_EOF) return HRESULT_FROM_WIN32(GetLastError());
        *got = n;
        return S_OK;
    }
};

void download(std::shared_ptr<Song> song, string url, string userAgent) {
    HANDLE out = CreateFileW(song->path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> lock(song->m);
        song->failed = true;
        song->cv.notify_all();
        return;
    }
    // In pieces: YouTube slows down one long request to listening speed.
    const long long piece = 1024 * 1024;
    int errors = 0;
    while (!song->cancel.load()) {
        long long from;
        { std::lock_guard<std::mutex> lock(song->m); from = song->have; }
        if (from >= song->total) break;
        long long to = std::min(song->total, from + piece) - 1;
        long long wrote = 0;
        HttpRes res = http("GET", url, {{"User-Agent", userAgent}, {"Range", "bytes=" + std::to_string(from) + "-" + std::to_string(to)}}, "",
            [&](const char* data, size_t n) {
                DWORD w = 0;
                if (!WriteFile(out, data, (DWORD)n, &w, nullptr)) return false;
                wrote += w;
                std::lock_guard<std::mutex> lock(song->m);
                song->have += w;
                song->cv.notify_all();
                return true;
            }, &song->cancel);
        if (song->cancel.load()) break;
        if (!(res.status == 206 || res.status == 200) || wrote == 0) {
            if (++errors >= 3) {
                std::lock_guard<std::mutex> lock(song->m);
                song->failed = true;
                song->cv.notify_all();
                break;
            }
            Sleep(500 * errors);
        } else errors = 0;
    }
    CloseHandle(out);
    bool whole;
    { std::lock_guard<std::mutex> lock(song->m); whole = song->have >= song->total; }
    // A whole song goes into the cache under its own name; half a song isn't worth keeping.
    if (whole) MoveFileExW(song->path.c_str(), song->finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(song->path.c_str());
}

// Keeps the cache small: the songs played longest ago go first.
void trimCache() {
    struct Entry { wstring path; ULONGLONG used; };
    std::vector<Entry> files;
    WIN32_FIND_DATAW found;
    // Leftovers of downloads that never finished.
    HANDLE parts = FindFirstFileW((cacheDir() + L"\\*.part").c_str(), &found);
    if (parts != INVALID_HANDLE_VALUE) {
        do DeleteFileW((cacheDir() + L"\\" + found.cFileName).c_str()); while (FindNextFileW(parts, &found));
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

// ---------------------------------------------------------------- The stream Windows reads

struct ReadDone : IUnknown {
    LONG refs = 1;
    ULONG count = 0;
    virtual ~ReadDone() {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
};

struct Stream : IMFByteStream {
    LONG refs = 1;
    std::shared_ptr<Song> song;
    std::mutex m;
    long long at = 0;

    explicit Stream(std::shared_ptr<Song> s) : song(std::move(s)) {}
    virtual ~Stream() {}

    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IMFByteStream) { *out = static_cast<IMFByteStream*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }

    STDMETHODIMP GetCapabilities(DWORD* caps) override { *caps = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE; return S_OK; }
    STDMETHODIMP GetLength(QWORD* length) override { *length = (QWORD)song->total; return S_OK; }
    STDMETHODIMP SetLength(QWORD) override { return E_NOTIMPL; }
    STDMETHODIMP GetCurrentPosition(QWORD* position) override { std::lock_guard<std::mutex> lock(m); *position = (QWORD)at; return S_OK; }
    STDMETHODIMP SetCurrentPosition(QWORD position) override { std::lock_guard<std::mutex> lock(m); at = (long long)position; return S_OK; }
    STDMETHODIMP IsEndOfStream(BOOL* end) override { std::lock_guard<std::mutex> lock(m); *end = at >= song->total; return S_OK; }

    STDMETHODIMP Read(BYTE* into, ULONG count, ULONG* got) override {
        long long from;
        { std::lock_guard<std::mutex> lock(m); from = at; }
        ULONG n = 0;
        HRESULT hr = song->read(from, into, count, &n);
        if (SUCCEEDED(hr)) { std::lock_guard<std::mutex> lock(m); at = from + n; }
        if (got) *got = n;
        return hr;
    }

    struct Pending { Stream* stream; BYTE* into; ULONG count; IMFAsyncResult* result; ReadDone* done; };

    static DWORD WINAPI readWork(void* arg) {
        Pending* p = (Pending*)arg;
        ULONG n = 0;
        HRESULT hr = p->stream->Read(p->into, p->count, &n);
        p->done->count = n;
        p->result->SetStatus(hr);
        MFInvokeCallback(p->result);
        p->result->Release();
        p->done->Release();
        p->stream->Release();
        delete p;
        return 0;
    }

    STDMETHODIMP BeginRead(BYTE* into, ULONG count, IMFAsyncCallback* callback, IUnknown* state) override {
        ReadDone* done = new ReadDone();
        IMFAsyncResult* result = nullptr;
        HRESULT hr = MFCreateAsyncResult(done, callback, state, &result);
        if (FAILED(hr)) { done->Release(); return hr; }
        AddRef();
        Pending* p = new Pending{this, into, count, result, done};
        if (!QueueUserWorkItem(readWork, p, WT_EXECUTELONGFUNCTION)) {
            result->Release();
            done->Release();
            Release();
            delete p;
            return E_FAIL;
        }
        return S_OK;
    }

    STDMETHODIMP EndRead(IMFAsyncResult* result, ULONG* got) override {
        *got = 0;
        IUnknown* object = nullptr;
        if (SUCCEEDED(result->GetObject(&object)) && object) {
            *got = static_cast<ReadDone*>(object)->count;
            object->Release();
        }
        return result->GetStatus();
    }

    STDMETHODIMP Write(const BYTE*, ULONG, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP BeginWrite(const BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    STDMETHODIMP EndWrite(IMFAsyncResult*, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* now) override {
        std::lock_guard<std::mutex> lock(m);
        at = origin == msoBegin ? offset : at + offset;
        if (at < 0) at = 0;
        if (now) *now = (QWORD)at;
        return S_OK;
    }
    STDMETHODIMP Flush() override { return S_OK; }
    STDMETHODIMP Close() override { return S_OK; }
};

// ---------------------------------------------------------------- The engine

HWND notifyWindow;
UINT notifyMessage;
std::atomic<LPARAM> token{0};

struct Notify : IMFMediaEngineNotify {
    LONG refs = 1;
    virtual ~Notify() {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == __uuidof(IMFMediaEngineNotify)) { *out = static_cast<IMFMediaEngineNotify*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        int what = -1;
        switch (event) {
            case MF_MEDIA_ENGINE_EVENT_CANPLAY: what = (int)AudioEvent::Ready; break;
            case MF_MEDIA_ENGINE_EVENT_PLAYING: what = (int)AudioEvent::Playing; break;
            case MF_MEDIA_ENGINE_EVENT_PAUSE: what = (int)AudioEvent::Paused; break;
            case MF_MEDIA_ENGINE_EVENT_ENDED: what = (int)AudioEvent::Ended; break;
            case MF_MEDIA_ENGINE_EVENT_ERROR: what = (int)AudioEvent::Failed; break;
            default: break;
        }
        if (what >= 0) PostMessageW(notifyWindow, notifyMessage, (WPARAM)what, token.load());
        return S_OK;
    }
};

IMFMediaEngine* engine;
IMFMediaEngineEx* engineEx;
Notify* notify;
std::shared_ptr<Song> current;
bool started;

}   // namespace

bool audio::start(HWND hwnd, UINT message) {
    notifyWindow = hwnd;
    notifyMessage = message;
    if (started) return engine != nullptr;
    started = true;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    IMFMediaEngineClassFactory* factory = nullptr;
    IMFAttributes* attributes = nullptr;
    notify = new Notify();
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
              SUCCEEDED(MFCreateAttributes(&attributes, 2)) &&
              SUCCEEDED(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify)) &&
              SUCCEEDED(attributes->SetUINT32(MF_MEDIA_ENGINE_AUDIO_CATEGORY, 3 /* AudioCategory_Media */)) &&
              SUCCEEDED(factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attributes, &engine));
    if (ok) engine->QueryInterface(IID_PPV_ARGS(&engineEx));
    if (attributes) attributes->Release();
    if (factory) factory->Release();
    background(trimCache);
    return ok && engineEx;
}

void audio::close() {
    if (current) {
        current->cancel.store(true);
        current->cv.notify_all();
        current.reset();
    }
}

void audio::stop() {
    close();
    if (engine) engine->Shutdown();
    if (engineEx) { engineEx->Release(); engineEx = nullptr; }
    if (engine) { engine->Release(); engine = nullptr; }
    if (started) MFShutdown();
    started = false;
}

void audio::open(const string& videoId, const string& url, const string& userAgent, long long length, LPARAM songToken) {
    close();
    token.store(songToken);
    if (!engineEx || length <= 0) { PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Failed, songToken); return; }
    auto song = std::make_shared<Song>();
    song->finalPath = cacheDir() + L"\\" + W(videoId) + L".m4a";
    song->total = length;
    // A finished download from before plays straight from the cache.
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    long long onDisk = -1;
    if (GetFileAttributesExW(song->finalPath.c_str(), GetFileExInfoStandard, &info))
        onDisk = ((long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (onDisk == length) {
        song->path = song->finalPath;
        song->have = length;
        HANDLE touch = CreateFileW(song->path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (touch != INVALID_HANDLE_VALUE) {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            SetFileTime(touch, nullptr, nullptr, &now);
            CloseHandle(touch);
        }
    } else {
        song->path = cacheDir() + L"\\" + W(videoId) + L"." + std::to_wstring(GetTickCount64()) + L".part";
        HANDLE create = CreateFileW(song->path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (create != INVALID_HANDLE_VALUE) CloseHandle(create);
    }
    song->reader = CreateFileW(song->path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (song->reader == INVALID_HANDLE_VALUE) { PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Failed, songToken); return; }
    current = song;
    if (song->have < song->total) background([song, url, userAgent] { download(song, url, userAgent); });
    Stream* stream = new Stream(song);
    BSTR name = SysAllocString(L"song.m4a");
    HRESULT hr = engineEx->SetSourceFromByteStream(stream, name);
    SysFreeString(name);
    stream->Release();
    if (FAILED(hr)) PostMessageW(notifyWindow, notifyMessage, (WPARAM)AudioEvent::Failed, songToken);
}

void audio::play() { if (engine) engine->Play(); }
void audio::pause() { if (engine) engine->Pause(); }
void audio::seek(double seconds) { if (engine) engine->SetCurrentTime(seconds); }
double audio::position() { return engine ? engine->GetCurrentTime() : 0; }
double audio::duration() { double d = engine ? engine->GetDuration() : 0; return d > 0 && d < 1e7 ? d : 0; }
bool audio::paused() { return !engine || engine->IsPaused(); }
void audio::setVolume(double volume) { if (engine) engine->SetVolume(std::max(0.0, std::min(1.0, volume))); }
void audio::setRate(double rate) { if (engine) { engine->SetDefaultPlaybackRate(rate); engine->SetPlaybackRate(rate); } }

double audio::buffered() {
    if (!current || current->total <= 0) return 0;
    std::lock_guard<std::mutex> lock(current->m);
    return (double)current->have / (double)current->total;
}
