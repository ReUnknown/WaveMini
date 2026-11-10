#include "app.h"
#include "signin.h"
#include <shellapi.h>

App app;

// ---------------------------------------------------------------- Saved state

static string itemJson(const Item& item) {
    string artists = "[";
    for (size_t i = 0; i < item.artists.size(); i++)
        artists += string(i ? "," : "") + "{\"name\":" + jq(item.artists[i].name) + ",\"id\":" + jq(item.artists[i].id) + "}";
    artists += "]";
    return "{\"kind\":" + std::to_string((int)item.kind) + ",\"id\":" + jq(item.id) + ",\"title\":" + jq(item.title) + ",\"art\":" + jq(item.art) +
           ",\"note\":" + jq(item.note) + ",\"artists\":" + artists + ",\"album\":" + jq(item.album.name) + ",\"albumId\":" + jq(item.album.id) +
           ",\"playlistId\":" + jq(item.playlistId) + ",\"year\":" + jq(item.year) + ",\"durationMs\":" + std::to_string(item.durationMs) + "}";
}

static Item itemFrom(const Json& j) {
    Item item;
    item.kind = (Kind)(int)j["kind"].num();
    item.id = j["id"].str();
    item.title = j["title"].str();
    item.art = j["art"].str();
    item.note = j["note"].str();
    for (auto& a : j["artists"].each()) item.artists.push_back({a["name"].str(), a["id"].str()});
    item.album = {j["album"].str(), j["albumId"].str()};
    item.playlistId = j["playlistId"].str();
    item.year = j["year"].str();
    item.durationMs = (long long)j["durationMs"].num();
    return item;
}

void loadState() {
    Json state = Json::parse(readFile(dataDir() + L"\\state.json"));
    Settings& s = app.settings;
    if (state.has()) {
        s.volume = std::max(0.0, std::min(1.0, state["volume"].num(1)));
        s.rate = state["rate"].num(1);
        if (s.rate < 0.5 || s.rate > 2) s.rate = 1;
        s.saver = state["saver"].truthy();
        s.evenVolume = !state["evenVolume"].has() || state["evenVolume"].truthy();
        s.autoplay = !state["autoplay"].has() || state["autoplay"].truthy();
        s.onTop = !state["onTop"].has() || state["onTop"].truthy();
        s.startWithWindows = state["startWithWindows"].truthy();
        s.corner = std::max(0, std::min(3, (int)state["corner"].num(3)));
        int tab = (int)state["tab"].num(1);
        s.tab = tab >= 0 && tab <= 5 ? (Tab)tab : Tab::UpNext;
        for (auto& r : state["recent"].each()) app.recent.push_back(r.str());
        if (state["last"].has()) { app.last = itemFrom(state["last"]); app.hasLast = !app.last.id.empty(); }
    }
    for (auto& id : split(readFile(dataDir() + L"\\likes.txt"), "\n")) if (!trim(id).empty()) app.liked.insert(trim(id));
    Json saved = Json::parse(readFile(dataDir() + L"\\session.json"));
    if (saved.has()) {
        Session session;
        session.cookie = unprotect(saved["cookie"].str());
        session.visitorData = saved["visitorData"].str();
        session.dataSyncId = saved["dataSyncId"].str();
        session.authUser = saved["authUser"].str();
        session.name = saved["name"].str();
        session.email = saved["email"].str();
        session.photo = saved["photo"].str();
        session.guest = saved["guest"].truthy();
        if (session.signedIn() || session.guest) {
            app.session = session;
            app.hasSession = true;
            yt::useSession(session);
        }
    }
}

void saveState() {
    const Settings& s = app.settings;
    string recent = "[";
    for (size_t i = 0; i < app.recent.size(); i++) recent += string(i ? "," : "") + jq(app.recent[i]);
    recent += "]";
    auto flag = [](bool b) { return b ? "true" : "false"; };
    char numbers[96];
    sprintf(numbers, "\"volume\":%.3f,\"rate\":%.2f", s.volume, s.rate);
    string out = string("{") + numbers + ",\"saver\":" + flag(s.saver) + ",\"evenVolume\":" + flag(s.evenVolume) + ",\"autoplay\":" + flag(s.autoplay) +
                 ",\"onTop\":" + flag(s.onTop) + ",\"startWithWindows\":" + flag(s.startWithWindows) + ",\"corner\":" + std::to_string(s.corner) +
                 ",\"tab\":" + std::to_string((int)s.tab) + ",\"recent\":" + recent + (app.hasLast ? ",\"last\":" + itemJson(app.last) : "") + "}";
    writeFile(dataDir() + L"\\state.json", out);
}

static void saveLikes() {
    string out;
    for (auto& id : app.liked) out += id + "\n";
    writeFile(dataDir() + L"\\likes.txt", out);
}

void saveSession() {
    const Session& s = app.session;
    string out = "{\"cookie\":" + jq(s.cookie.empty() ? "" : protect(s.cookie)) + ",\"visitorData\":" + jq(s.visitorData) + ",\"dataSyncId\":" + jq(s.dataSyncId) +
                 ",\"authUser\":" + jq(s.authUser) + ",\"name\":" + jq(s.name) + ",\"email\":" + jq(s.email) + ",\"photo\":" + jq(s.photo) +
                 ",\"guest\":" + (s.guest ? "true" : "false") + "}";
    writeFile(dataDir() + L"\\session.json", app.hasSession ? out : "{}");
}

static void refreshAccount() {
    if (!app.session.signedIn()) return;
    async<Session>([] { Session s = yt::session(); if (!yt::accountInfo(s)) s.name.clear(); return s; }, [](Session s) {
        if (s.name.empty() || !app.session.signedIn()) return;
        app.session.name = s.name;
        app.session.email = s.email;
        app.session.photo = s.photo;
        saveSession();
        invalidate();
    });
    // Your likes, so every thumb shows right away.
    async<std::vector<string>>([] {
        std::vector<string> ids;
        ListPage page = yt::playlist("LM");
        for (auto& song : page.songs) ids.push_back(song.id);
        return ids;
    }, [](std::vector<string> ids) {
        if (ids.empty()) return;
        for (auto& id : ids) app.liked.insert(id);
        saveLikes();
        invalidate();
    });
}

void signedIn(const Session& s) {
    app.session = s;
    app.hasSession = true;
    yt::useSession(s);
    saveSession();
    for (int i = 0; i < 3; i++) { app.library[i].clear(); app.libraryLoaded[i] = app.libraryLoading[i] = false; }
    app.home.clear();
    app.homeMore.clear();
    app.homeLoaded = false;
    app.homeScroll = 0;
    refreshAccount();
    invalidate();
}

void signOut() {
    signin::close();
    app.signingIn = false;
    audio::pause();
    audio::close();
    app.generation++;
    app.queue.clear();
    app.index = -1;
    app.playing = app.loading = false;
    app.session = Session();
    app.hasSession = false;
    yt::useSession(app.session);
    saveSession();
    app.liked.clear();
    saveLikes();
    for (int i = 0; i < 3; i++) { app.library[i].clear(); app.libraryLoaded[i] = app.libraryLoading[i] = false; }
    app.home.clear();
    app.homeMore.clear();
    app.homeLoaded = false;
    app.homeScroll = 0;
    app.searchPages.clear();
    app.libraryPages.clear();
    app.menuOpen = false;
    background([] { Sleep(1500); signin::forget(); });
    invalidate();
}

// ---------------------------------------------------------------- Small things

void showToast(const string& text) {
    app.toast = text;
    app.toastUntil = nowMs() + 2600;
    invalidate();
}

void copyText(const string& text) {
    wstring w = W(text);
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (memory) {
        memcpy(GlobalLock(memory), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

void setStartWithWindows(bool on) {
    app.settings.startWithWindows = on;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        if (on) {
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            wstring command = L"\"" + wstring(exe) + L"\"";
            RegSetValueExW(key, L"WaveMini", 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t)));
        } else RegDeleteValueW(key, L"WaveMini");
        RegCloseKey(key);
    }
    saveState();
}

void checkForUpdates() {
    showToast("Checking for updates...");
    async<Json>([] {
        HttpRes res = http("GET", "https://api.github.com/repos/ReUnknown/WaveMini/releases/latest", {{"User-Agent", "WaveMini"}, {"Accept", "application/vnd.github+json"}});
        return res.ok() ? Json::parse(res.body) : Json();
    }, [](Json release) {
        string tag = release["tag_name"].str();
        if (tag.empty()) { showToast("Couldn't check for updates"); return; }
        if (tag == "v" WAVEMINI_VERSION || tag == WAVEMINI_VERSION) { showToast("You have the latest version"); return; }
        showToast("WaveMini " + tag + " is out");
        ShellExecuteW(nullptr, L"open", W(release["html_url"].str()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
}

void applyVolume() {
    audio::setVolume(app.settings.volume * (app.settings.evenVolume ? app.loudnessGain : 1));
}

void setSleep(int minutes) {
    app.sleepMinutes = minutes;
    app.sleepAt = minutes > 0 ? nowMs() + (long long)minutes * 60000 : 0;
}

// ---------------------------------------------------------------- Playing

static unsigned rejectedAudioClients = 0;
static AudioClient activeAudioClient = AudioClient::None;
static bool waitingForNext = false;

static void loadAudio(const Item& song, bool retryVerified, const string& previousError = "") {
    int generation = app.generation;
    bool saver = app.settings.saver;
    unsigned rejected = rejectedAudioClients;
    async<AudioPick>([song, saver, retryVerified, rejected] {
        return yt::audio(song.id, saver, retryVerified, rejected);
    }, [song, generation, previousError](AudioPick pick) {
        if (generation != app.generation) return;
        if (pick.url.empty()) {
            app.loading = false;
            app.needsVerification = pick.needsVerification;
            app.error = pick.error;
            if (!pick.needsVerification && !previousError.empty()) app.error = previousError;
            if (app.error.empty()) app.error = "This song can't be played";
        } else {
            activeAudioClient = pick.client;
            // Songs mastered louder than YouTube's reference are turned down to it.
            app.loudnessGain = pick.hasLoudness && pick.loudnessDb > 0 ? pow(10.0, -pick.loudnessDb / 20.0) : 1;
            audio::open(song.id, pick.url, pick.userAgent, pick.length, generation, song.durationMs / 1000.0);
        }
        invalidate();
    });
}

static void fetchMore() {
    if (app.fetchingMore || app.continuation.empty()) return;
    app.fetchingMore = true;
    string continuation = app.continuation;
    async<NextResult>([continuation] { return yt::next("", "", continuation); }, [continuation](NextResult more) {
        app.fetchingMore = false;
        if (app.continuation != continuation) return;
        app.continuation = more.continuation;
        for (auto& song : more.songs) {
            if (std::any_of(app.queue.begin(), app.queue.end(), [&](const Item& q) { return q.id == song.id; })) continue;
            app.queue.push_back(song);
        }
        invalidate();
    });
}

void playIndex(int index, bool retryVerified) {
    if (index < 0 || index >= (int)app.queue.size()) return;
    app.index = index;
    app.loading = true;
    app.playing = false;
    app.wantPlay = true;
    app.error.clear();
    app.needsVerification = false;
    app.queueScroll = 0;
    ++app.generation;
    waitingForNext = false;
    rejectedAudioClients = 0;
    activeAudioClient = AudioClient::None;
    audio::pause();
    audio::close();
    const Item song = app.queue[index];
    if (!app.hasLast || app.last.kind == Kind::Song) { app.last = song; app.hasLast = true; }
    app.lyrics.clear();
    app.lyricsFor.clear();
    app.lyricsScroll = 0;
    if (app.settings.tab == Tab::Lyrics) loadLyrics();
    loadAudio(song, retryVerified);
    if (app.session.signedIn()) background([song] { yt::reportPlay(song.id); });
    if (app.settings.autoplay && (int)app.queue.size() - index <= 3) fetchMore();
    saveState();
    invalidate();
}

void playSongs(const std::vector<Item>& songs, int index, const string& source, const string& continuation) {
    if (songs.empty()) return;
    app.queue = songs;
    app.source = source;
    app.continuation = continuation;
    app.shuffle = false;
    playIndex(std::max(0, std::min(index, (int)songs.size() - 1)));
}

void playRadio(const Item& song) {
    app.last = song;
    app.hasLast = true;
    playSongs({song}, 0, "Songs like " + song.title);
    string id = song.id;
    async<NextResult>([id] {
        // The song's own radio; plain "next" as a fallback.
        NextResult radio = yt::next(id, "RDAMVM" + id);
        return radio.songs.size() > 1 ? radio : yt::next(id);
    }, [id](NextResult radio) {
        // Still on that song, with nothing queued after it?
        if (app.queue.size() != 1 || app.queue[0].id != id) return;
        for (auto& s : radio.songs) if (s.id != id) app.queue.push_back(s);
        app.continuation = radio.continuation;
        invalidate();
    });
}

static void shuffleSongs(std::vector<Item>& songs, size_t from) {
    for (size_t i = songs.size(); i > from + 1; i--) std::swap(songs[i - 1], songs[from + rand() % (i - from)]);
}

void playItem(const Item& item, bool shuffled) {
    if (item.kind == Kind::Song) { playRadio(item); return; }
    if (item.kind == Kind::Artist) return;
    showToast("Starting " + item.title);
    Item head = item;
    async<ListPage>([head] {
        if (head.kind == Kind::Album) return yt::album(head.id);
        ListPage page = yt::playlist(head.id);
        // Long playlists come in pages.
        for (int more = 0; more < 4 && !page.continuation.empty(); more++)
            for (auto& song : yt::playlistMore(page.continuation)) page.songs.push_back(song);
        return page;
    }, [head, shuffled](ListPage page) {
        if (page.songs.empty()) { showToast("Couldn't open " + head.title); return; }
        if (shuffled) shuffleSongs(page.songs, 0);
        Item keep = head;
        if (keep.art.empty()) keep.art = page.head.art;
        if (keep.title.empty()) keep.title = page.head.title;
        if (keep.artists.empty()) keep.artists = page.head.artists;
        app.last = keep;
        app.hasLast = true;
        playSongs(page.songs, 0, keep.title);
        app.shuffle = shuffled;
    });
}

void playLast(bool shuffled) {
    if (app.hasLast) playItem(app.last, shuffled);
}

void togglePlay() {
    if (!app.current()) { playLast(false); return; }
    if (app.loading) { app.wantPlay = !app.wantPlay; return; }
    if (app.needsVerification) { verifyPlayback(); return; }
    if (!app.error.empty()) { playIndex(app.index); return; }
    if (audio::paused()) { app.wantPlay = true; audio::play(); }
    else { app.wantPlay = false; audio::pause(); }
}

void nextSong(bool byUser) {
    if (!app.current()) return;
    if (app.repeat == 2 && !byUser) { audio::seek(0); audio::play(); return; }
    if (app.index + 1 < (int)app.queue.size()) { playIndex(app.index + 1); return; }
    if (app.repeat == 1) { playIndex(0); return; }
    if (!app.continuation.empty() && app.settings.autoplay) {
        waitingForNext = !byUser;
        fetchMore();
        showToast("Finding more songs...");
        return;
    }
    app.playing = false;
    app.wantPlay = false;
    audio::pause();
    invalidate();
}

void previousSong() {
    if (!app.current()) return;
    if (audio::position() > 4 || app.index == 0) { audio::seek(0); invalidate(); return; }
    playIndex(app.index - 1);
}

void seekTo(double seconds) {
    if (!app.current() || app.loading) return;
    double length = audio::duration();
    audio::seek(std::max(0.0, length > 0 ? std::min(seconds, length - 0.5) : seconds));
    invalidate();
}

void removeFromQueue(int index) {
    if (index <= app.index || index >= (int)app.queue.size()) return;
    app.queue.erase(app.queue.begin() + index);
    invalidate();
}

void playNext(const Item& song) {
    if (!app.current()) { playRadio(song); return; }
    app.queue.insert(app.queue.begin() + app.index + 1, song);
    showToast("Playing next");
}

void addToQueue(const Item& song) {
    if (!app.current()) { playRadio(song); return; }
    app.queue.push_back(song);
    showToast("Added to the queue");
}

void moveInQueue(int from, int to) {
    int count = (int)app.queue.size();
    if (from <= app.index || from >= count) return;
    to = std::max(app.index + 1, std::min(count - 1, to));
    if (to == from) return;
    Item song = app.queue[from];
    app.queue.erase(app.queue.begin() + from);
    app.queue.insert(app.queue.begin() + to, song);
    invalidate();
}

bool isSaved(const Item& head) {
    if (app.savedLists.count(head.id)) return true;
    const std::vector<Item>& list = app.library[head.kind == Kind::Album ? 1 : 0];
    return std::any_of(list.begin(), list.end(), [&](const Item& item) { return item.id == head.id; });
}

void toggleSaved(const Item& head) {
    if (!app.session.signedIn()) { showToast("Sign in to save to your library"); return; }
    // Albums are saved by their own playlist; playlists by theirs.
    string playlistId = head.kind == Kind::Album ? head.playlistId : head.id;
    if (playlistId.empty()) { showToast("This one can't be saved"); return; }
    bool save = !isSaved(head);
    int which = head.kind == Kind::Album ? 1 : 0;
    if (save) {
        app.savedLists.insert(head.id);
        if (app.libraryLoaded[which]) app.library[which].insert(app.library[which].begin(), head);
    } else {
        app.savedLists.erase(head.id);
        auto& list = app.library[which];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Item& item) { return item.id == head.id; }), list.end());
    }
    showToast(save ? "Saved to your library" : "Removed from your library");
    background([playlistId, save] { yt::saveList(playlistId, save); });
}

void createPlaylist(const string& name) {
    string title = trim(name);
    if (title.empty()) return;
    async<string>([title] { return yt::createPlaylist(title); }, [title](string id) {
        if (id.empty()) { showToast("Couldn't make the playlist"); return; }
        Item made;
        made.kind = Kind::Playlist;
        made.id = id;
        made.title = title;
        app.library[0].insert(app.library[0].begin(), made);
        showToast("Made " + title);
    });
}

void removeFromPlaylist(const string& playlistId, const Item& song) {
    Item copy = song;
    async<bool>([playlistId, copy] { return yt::removeFromPlaylist(playlistId, copy.id, copy.setVideoId); }, [playlistId, copy](bool ok) {
        if (!ok) { showToast("Couldn't remove it (is it your playlist?)"); return; }
        for (auto* stack : {&app.searchPages, &app.libraryPages})
            for (auto& page : *stack) {
                if (page.type != Page::List || page.id != playlistId) continue;
                auto& songs = page.list.songs;
                songs.erase(std::remove_if(songs.begin(), songs.end(), [&](const Item& s) { return s.setVideoId == copy.setVideoId; }), songs.end());
            }
        showToast("Removed from the playlist");
    });
}

void toggleShuffle() {
    app.shuffle = !app.shuffle;
    if (app.shuffle && app.index >= 0) shuffleSongs(app.queue, (size_t)app.index + 1);
    invalidate();
}

void cycleRepeat() {
    app.repeat = (app.repeat + 1) % 3;
    invalidate();
}

void toggleLike() {
    const Item* song = app.current();
    if (!song) return;
    string id = song->id;
    bool liked = app.liked.count(id) == 0;
    if (liked) app.liked.insert(id); else app.liked.erase(id);
    saveLikes();
    if (app.session.signedIn()) background([id, liked] { yt::like(id, liked); });
    invalidate();
}

static void retryAudio(AudioEvent event, const string& error) {
    if (const Item* song = app.current()) {
        ++app.generation;
        rejectedAudioClients |= (unsigned)activeAudioClient;
        activeAudioClient = AudioClient::None;
        if (event == AudioEvent::DownloadFailed) audio::close();
        else audio::discard();
        app.loading = true;
        loadAudio(*song, false, error);
    }
}

void onAudio(AudioEvent event, LPARAM token) {
    if ((int)token != app.generation) return;
    switch (event) {
        case AudioEvent::Ready:
            app.loading = false;
            applyVolume();
            audio::setRate(app.settings.rate);
            if (app.wantPlay) audio::play();
            break;
        case AudioEvent::Playing: app.playing = true; break;
        case AudioEvent::Paused: app.playing = false; break;
        case AudioEvent::Ended:
            app.playing = false;
            if (!app.wantPlay || !app.error.empty()) break;
            if (audio::finished()) {
                audio::pause();
                nextSong(false);
            } else {
                retryAudio(event, "Windows stopped playback before the song ended");
            }
            break;
        case AudioEvent::Unavailable:
            app.loading = false;
            app.playing = false;
            app.error = "Windows Media Player is unavailable";
            break;
        case AudioEvent::DownloadFailed:
        case AudioEvent::Failed:
            app.playing = false;
            retryAudio(event, event == AudioEvent::DownloadFailed ?
                       "Couldn't download this song from YouTube" : "Windows couldn't play this audio");
            break;
    }
    invalidate();
}

void tick() {
    audio::observePlayback();
    long long now = nowMs();
    if (app.sleepAt && now >= app.sleepAt) {
        app.sleepAt = 0;
        app.sleepMinutes = 0;
        app.wantPlay = false;
        audio::pause();
        invalidate();
    }
    if (app.suggestDue && now >= app.suggestDue) loadSuggestions();
    if (app.toastUntil && now >= app.toastUntil) { app.toastUntil = 0; app.toast.clear(); invalidate(); }
    // Continue only if a real end-of-track event requested another radio page.
    if (waitingForNext && app.current() && app.wantPlay && app.error.empty()) {
        if (app.index + 1 < (int)app.queue.size()) playIndex(app.index + 1);
        else if (!app.fetchingMore && app.continuation.empty()) {
            waitingForNext = false;
            app.wantPlay = false;
        }
    }
}

// ---------------------------------------------------------------- Lyrics

struct LyricsResult { std::vector<LyricLine> lines; bool synced = false; };

static string cleanTitle(string title) {
    string low = lower(title);
    for (const char* cut : {" (feat", " [feat", " (ft.", " (with ", " - remaster", " (remaster", " - live", " - radio edit"}) {
        size_t at = low.find(cut);
        if (at != string::npos) { title = title.substr(0, at); low = low.substr(0, at); }
    }
    return trim(title);
}

static double ttmlTime(const string& value) {
    if (value.empty()) return -1;
    string v = value;
    if (v.back() == 's') v.pop_back();
    double total = 0;
    for (auto& part : split(v, ":")) total = total * 60 + atof(part.c_str());
    return total;
}

static string attribute(const string& attrs, const char* name) {
    string look = string(name) + "=\"";
    size_t at = attrs.find(look);
    while (at != string::npos && at > 0 && !isspace((unsigned char)attrs[at - 1])) at = attrs.find(look, at + 1);
    if (at == string::npos) return string();
    at += look.size();
    size_t end = attrs.find('"', at);
    return end == string::npos ? string() : attrs.substr(at, end - at);
}

static string unescape(const string& text) {
    string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '&') { out += text[i]; continue; }
        size_t end = text.find(';', i);
        string name = end == string::npos || end - i > 8 ? "" : text.substr(i + 1, end - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos" || name == "#39" || name == "#x27") out += '\'';
        else { out += '&'; continue; }
        i = end;
    }
    return out;
}

// Apple-style TTML: <p> lines holding timed <span> words (backing vocals are left out).
static LyricsResult parseTtml(const string& ttml) {
    LyricsResult out;
    struct Piece { double at, end; string text; bool space; };
    std::vector<Piece> pieces;
    double lineAt = -1, lineEnd = -1, spanAt = -1, spanEnd = -1;
    bool inLine = false, inWord = false, anyWords = false;
    int backing = 0;
    string spanText;
    auto finishLine = [&] {
        if (pieces.empty()) return;
        LyricLine line;
        line.at = lineAt >= 0 ? lineAt : pieces[0].at;
        line.end = lineEnd > 0 ? lineEnd : pieces.back().end;
        for (auto& piece : pieces) {
            LyricLine::Word word;
            word.at = piece.at;
            word.end = piece.end;
            word.from = (int)W(line.text).size();
            word.count = (int)W(piece.text).size();
            line.text += piece.text;
            if (piece.space) line.text += ' ';
            if (piece.at >= 0) line.words.push_back(word);
        }
        line.text = trim(line.text);
        if (line.words.size() > 1) anyWords = true;
        if (!line.text.empty()) out.lines.push_back(line);
        pieces.clear();
    };
    size_t i = 0;
    while (i < ttml.size()) {
        if (ttml[i] != '<') {
            size_t next = ttml.find('<', i);
            string text = unescape(ttml.substr(i, next == string::npos ? string::npos : next - i));
            i = next == string::npos ? ttml.size() : next;
            if (!inLine || backing > 0) continue;
            if (inWord) spanText += text;
            else if (trim(text).empty()) { if (!pieces.empty()) pieces.back().space = true; }
            else pieces.push_back({lineAt, lineEnd, trim(text), !text.empty() && text.back() == ' '});
            continue;
        }
        size_t close = ttml.find('>', i);
        if (close == string::npos) break;
        string tag = ttml.substr(i + 1, close - i - 1);
        i = close + 1;
        bool closing = !tag.empty() && tag[0] == '/', selfClosing = !tag.empty() && tag.back() == '/';
        if (closing) tag = tag.substr(1);
        size_t space = tag.find_first_of(" \t\r\n/");
        string name = tag.substr(0, space), attrs = space == string::npos ? "" : tag.substr(space);
        if (!closing) {
            if (name == "p") {
                finishLine();
                inLine = true;
                lineAt = ttmlTime(attribute(attrs, "begin"));
                lineEnd = ttmlTime(attribute(attrs, "end"));
            } else if (name == "span" && inLine && !selfClosing) {
                if (backing > 0 || attribute(attrs, "ttm:role") == "x-bg") backing++;
                else {
                    spanAt = ttmlTime(attribute(attrs, "begin"));
                    spanEnd = ttmlTime(attribute(attrs, "end"));
                    spanText.clear();
                    inWord = spanAt >= 0;
                }
            }
        } else if (name == "span") {
            if (backing > 0) backing--;
            else if (inWord) {
                if (!trim(spanText).empty()) pieces.push_back({spanAt, spanEnd, trim(spanText), !spanText.empty() && spanText.back() == ' '});
                inWord = false;
            }
        } else if (name == "p") {
            inLine = false;
            finishLine();
        }
    }
    std::stable_sort(out.lines.begin(), out.lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.at < b.at; });
    if (!anyWords) for (auto& line : out.lines) line.words.clear();
    out.synced = !out.lines.empty();
    return out;
}

static LyricsResult parseLyrics(const Json& entry) {
    LyricsResult out;
    string synced = entry["syncedLyrics"].str();
    if (!synced.empty()) {
        for (auto& raw : split(synced, "\n")) {
            string line = trim(raw);
            std::vector<double> stamps;
            while (line.size() > 5 && line[0] == '[') {
                size_t close = line.find(']');
                if (close == string::npos) break;
                string stamp = line.substr(1, close - 1);
                size_t colon = stamp.find(':');
                if (colon == string::npos || !isdigit((unsigned char)stamp[0])) break;
                stamps.push_back(atof(stamp.substr(0, colon).c_str()) * 60 + atof(stamp.substr(colon + 1).c_str()));
                line = trim(line.substr(close + 1));
            }
            if (line.empty()) continue;
            for (double at : stamps) { LyricLine l; l.at = at; l.text = line; out.lines.push_back(l); }
        }
        std::stable_sort(out.lines.begin(), out.lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.at < b.at; });
        out.synced = !out.lines.empty();
        if (out.synced) return out;
    }
    for (auto& raw : split(entry["plainLyrics"].str(), "\n")) {
        string line = trim(raw);
        if (line.empty()) continue;
        LyricLine l;
        l.text = line;
        out.lines.push_back(l);
    }
    return out;
}

void loadLyrics() {
    const Item* song = app.current();
    if (!song || app.lyricsFor == song->id) return;
    app.lyricsFor = song->id;
    app.lyrics.clear();
    app.lyricsLoading = true;
    app.lyricsMeasured = false;
    app.lyricsScroll = 0;
    Item s = *song;
    async<LyricsResult>([s] {
        Headers h = {{"User-Agent", "WaveMini (https://github.com/ReUnknown/WaveMini)"}};
        string artist = s.artists.empty() ? "" : s.artists[0].name;
        string query = "track_name=" + urlEncode(cleanTitle(s.title)) + "&artist_name=" + urlEncode(artist);
        LyricsResult result;
        // Word-by-word first (the lyrics Wave uses), then line-by-line, then plain.
        HttpRes timed = http("GET", "https://lyrics-api.boidu.dev/getLyrics?s=" + urlEncode(cleanTitle(s.title)) + "&a=" + urlEncode(artist) +
                             (s.durationMs ? "&d=" + std::to_string(s.durationMs / 1000) : "") + (s.album.name.empty() ? "" : "&al=" + urlEncode(s.album.name)), h);
        if (timed.ok()) {
            LyricsResult words = parseTtml(Json::parse(timed.body)["ttml"].str());
            if (!words.lines.empty()) return words;
        }
        HttpRes exact = http("GET", "https://lrclib.net/api/get?" + query + (s.durationMs ? "&duration=" + std::to_string(s.durationMs / 1000) : ""), h);
        if (exact.ok()) result = parseLyrics(Json::parse(exact.body));
        if (result.lines.empty()) {
            HttpRes found = http("GET", "https://lrclib.net/api/search?" + query, h);
            if (found.ok()) {
                Json list = Json::parse(found.body);
                for (auto& entry : list.each()) {
                    LyricsResult candidate = parseLyrics(entry);
                    if (candidate.lines.empty()) continue;
                    if (result.lines.empty() || (candidate.synced && !result.synced)) result = candidate;
                    if (result.synced) break;
                }
            }
        }
        if (result.lines.empty()) {
            // YouTube Music's own plain lyrics.
            NextResult next = yt::next(s.id);
            for (auto& raw : split(yt::plainLyrics(next.lyricsId), "\n")) {
                string line = trim(raw);
                if (line.empty()) continue;
                LyricLine l;
                l.text = line;
                result.lines.push_back(l);
            }
        }
        return result;
    }, [s](LyricsResult result) {
        if (app.lyricsFor != s.id) return;
        app.lyrics = std::move(result.lines);
        app.lyricsSynced = result.synced;
        app.lyricsLoading = false;
        app.lyricsMeasured = false;
        invalidate();
    });
}

// ---------------------------------------------------------------- Search, library, pages

void runSearch() {
    string query = trim(U(app.query));
    app.searched = query;
    app.searchScroll = 0;
    if (query.empty()) { app.results.clear(); app.searching = false; invalidate(); return; }
    app.recent.erase(std::remove(app.recent.begin(), app.recent.end(), query), app.recent.end());
    app.recent.insert(app.recent.begin(), query);
    if (app.recent.size() > 6) app.recent.resize(6);
    saveState();
    app.searching = true;
    SearchFilter filter = app.filter;
    async<std::vector<Section>>([query, filter] { return yt::search(query, filter); }, [query, filter](std::vector<Section> sections) {
        if (app.searched != query || app.filter != filter) return;
        app.results = std::move(sections);
        app.searching = false;
        invalidate();
    });
    invalidate();
}

void loadHome(bool more) {
    if (app.homeLoading || (!more && app.homeLoaded) || (more && app.homeMore.empty())) return;
    app.homeLoading = true;
    string from = more ? app.homeMore : string();
    async<std::pair<std::vector<Section>, string>>([from] {
        string next = from;
        auto sections = yt::home(next);
        return std::make_pair(sections, next);
    }, [more](std::pair<std::vector<Section>, string> got) {
        app.homeLoading = false;
        app.homeLoaded = true;
        if (!more) app.home.clear();
        for (auto& section : got.first) app.home.push_back(section);
        app.homeMore = got.second;
        app.homeX.resize(app.home.size(), 0);
        invalidate();
    });
}

void loadSuggestions() {
    string input = trim(U(app.query));
    app.suggestDue = 0;
    if (input.empty() || input == app.suggestFor) return;
    app.suggestFor = input;
    async<yt::Suggestions>([input] { return yt::suggest(input); }, [input](yt::Suggestions got) {
        if (app.suggestFor != input) return;
        app.suggestQueries = std::move(got.queries);
        app.suggestItems = std::move(got.items);
        invalidate();
    });
}

void loadLibrary(int which, bool force) {
    if (!app.session.signedIn() || app.libraryLoading[which] || (app.libraryLoaded[which] && !force)) return;
    app.libraryLoading[which] = true;
    async<std::vector<Item>>([which] { return which == 0 ? yt::libraryPlaylists() : which == 1 ? yt::libraryAlbums() : yt::libraryArtists(); },
        [which](std::vector<Item> items) {
            app.library[which] = std::move(items);
            app.libraryLoading[which] = false;
            app.libraryLoaded[which] = true;
            invalidate();
        });
}

struct Loaded { ArtistPage artist; ListPage list; std::vector<Item> items; };

void openPage(std::vector<Page>& stack, Page::Type type, const string& id, const string& title, const string& params) {
    Page page;
    page.type = type;
    page.id = id;
    page.title = title;
    stack.push_back(page);
    std::vector<Page>* where = &stack;
    async<Loaded>([type, id, params] {
        Loaded loaded;
        if (type == Page::Artist) loaded.artist = yt::artist(id);
        else if (type == Page::List) loaded.list = startsWith(id, "MPRE") ? yt::album(id) : yt::playlist(id);
        else if (id == "history") { for (auto& section : yt::history()) for (auto& item : section.items) loaded.items.push_back(item); }
        else loaded.items = yt::browseItems(id, params);
        return loaded;
    }, [where, type, id](Loaded loaded) {
        for (auto& page : *where) {
            if (page.type != type || page.id != id || !page.loading) continue;
            page.loading = false;
            page.artist = std::move(loaded.artist);
            page.list = std::move(loaded.list);
            page.items = std::move(loaded.items);
            page.failed = type == Page::Artist ? !page.artist.ok : type == Page::List ? !page.list.ok : page.items.empty();
            break;
        }
        invalidate();
    });
    invalidate();
}

void openItem(std::vector<Page>& stack, const Item& item) {
    switch (item.kind) {
        case Kind::Song: playRadio(item); break;
        case Kind::Artist: openPage(stack, Page::Artist, item.id, item.title); break;
        default: openPage(stack, Page::List, item.id, item.title); break;
    }
}
