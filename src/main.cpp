// The window: one small see-through card that floats over everything, snaps to a corner of the
// screen and tucks against its edge. Also the "--render" check, which saves every screen as a
// picture so the look can be verified without a person at the computer.
#include "app.h"
#include "signin.h"
#include <shellapi.h>
#include <windowsx.h>
#include <dwmapi.h>

#define WM_UICALL (WM_APP + 1)
#define WM_AUDIO (WM_APP + 2)

static HWND window;
static HDC memoryDc;
static HBITMAP bitmap;
static void* bitmapBits;
static int pixelW, pixelH;
static float scale = 1;
static bool dirty = true;
static float posX, posY, targetX, targetY;   // the window's place, in screen pixels
static bool placed, moving;
static int tuckY;
// Dragging the window itself.
static bool windowDrag, dragMoved, trackingMouse;
static POINT dragCursor;
static float dragX, dragY;
static UINT timerMs;
// The window's shape springs between sizes (unfolding, tucking) instead of jumping.
static float shownW, shownH, goalW, goalH, velX, velY, velW, velH;
static int bufferW, bufferH;

void invalidate() { dirty = true; }

// ---------------------------------------------------------------- Where the window sits

static RECT workArea() {
    POINT at = {(LONG)(posX + pixelW / 2), (LONG)(posY + pixelH / 2)};
    HMONITOR monitor = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}

static void home(int w, int h, float& x, float& y) {
    RECT area = workArea();
    float margin = 16 * scale;
    if (app.tucked) {
        x = app.tuckRight ? area.right - w - 6 * scale : area.left + 6 * scale;
        y = (float)std::max<LONG>(area.top + (LONG)margin, std::min<LONG>(area.bottom - h - (LONG)margin, tuckY - h / 2));
        return;
    }
    int corner = app.settings.corner;
    x = corner % 2 == 0 ? area.left + margin : area.right - w - margin;
    y = corner < 2 ? area.top + margin : area.bottom - h - margin;
}

static void retime() {
    // Fast while something moves, slow (just the sleep timer and messages) otherwise.
    bool lively = !app.tucked && (app.playing || app.loading || app.searchFocus || (app.settings.tab == Tab::Lyrics && app.playing));
    UINT want = moving ? 15 : lively ? 33 : (app.tucked || IsIconic(window)) ? 1000 : 500;
    if (want != timerMs) { timerMs = want; SetTimer(window, 1, want, nullptr); }
}

static void render() {
    if (IsIconic(window)) { dirty = false; return; }
    Size2 want = wantedSize();
    float wantW = want.w * scale, wantH = want.h * scale;
    if (wantW != goalW || wantH != goalH) {
        goalW = wantW;
        goalH = wantH;
        if (!placed) {
            shownW = goalW;
            shownH = goalH;
            pixelW = (int)goalW;
            pixelH = (int)goalH;
        }
        // A new size keeps its corner (or its spot on the edge).
        home((int)lroundf(goalW), (int)lroundf(goalH), targetX, targetY);
        if (!placed) { posX = targetX; posY = targetY; }
        else moving = true;
        placed = true;
    }
    int w = std::max(1, (int)lroundf(shownW)), h = std::max(1, (int)lroundf(shownH));
    if (w > bufferW || h > bufferH || !bitmap) {
        // Only ever grows, so a changing shape doesn't mean a new bitmap every frame.
        bufferW = std::max(bufferW, w);
        bufferH = std::max(bufferH, h);
        if (bitmap) DeleteObject(bitmap);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bufferW;
        info.bmiHeader.biHeight = -bufferH;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        if (!memoryDc) memoryDc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(memoryDc, &info, DIB_RGB_COLORS, &bitmapBits, nullptr, 0);
        SelectObject(memoryDc, bitmap);
    }
    pixelW = w;
    pixelH = h;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!gfx::begin(memoryDc, w, h, scale)) return;
        draw(want.w, want.h, w / scale, h / scale);
        if (gfx::end()) break;
    }
    POINT source = {0, 0}, place = {(LONG)lroundf(posX), (LONG)lroundf(posY)};
    SIZE size = {w, h};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, &place, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);
    dirty = false;
    retime();
}

// One frame of the window's own motion: the same spring for place and size, so the corner
// it's anchored to stays put while the rest moves.
static void stepWindow(float dt) {
    auto spring = [dt](float& x, float& v, float target) {
        const float stiffness = 420, damping = 0.82f;
        float left = std::min(dt, 0.05f);
        while (left > 0) {
            float h = std::min(left, 0.006f);
            v += (-stiffness * (x - target) - 2 * damping * sqrtf(stiffness) * v) * h;
            x += v * h;
            left -= h;
        }
        return fabsf(x - target) < 0.5f && fabsf(v) < 6;
    };
    bool a = spring(posX, velX, targetX), b = spring(posY, velY, targetY), c = spring(shownW, velW, goalW), d = spring(shownH, velH, goalH);
    if (a && b && c && d) {
        posX = targetX; posY = targetY; shownW = goalW; shownH = goalH;
        velX = velY = velW = velH = 0;
        moving = false;
        if (app.tucked || app.settings.tab == Tab::None) PostMessageW(window, WM_APP + 3, 0, 0);
    }
    dirty = true;
}

static void glideHome() {
    home((int)lroundf(goalW), (int)lroundf(goalH), targetX, targetY);
    moving = true;
    retime();
}

void windowTuck(bool tuck) {
    if (tuck) {
        app.tuckRight = app.settings.corner % 2 == 1;
        tuckY = (int)(posY + pixelH / 2);
    }
    app.tucked = tuck;
    app.menuOpen = false;
    invalidate();
}

void trimMemory() {
    // Covers not on screen right now, the painted background and the drawing surface (it's
    // remade at the size needed), then Windows takes back the pages nothing is using.
    gfx::trimImages(1500);
    if (bitmap && (bufferW > pixelW * 2 || bufferH > pixelH * 2 || IsIconic(window))) {
        DeleteObject(bitmap);
        bitmap = nullptr;
        bufferW = bufferH = 0;
    }
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

void windowClose() { PostMessageW(window, WM_CLOSE, 0, 0); }

void applyOnTop() {
    SetWindowPos(window, app.settings.onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void startSignIn() {
    if (app.signingIn) return;
    app.signingIn = true;
    invalidate();
    RECT over = {(LONG)posX, (LONG)posY, (LONG)posX + pixelW, (LONG)posY + pixelH};
    signin::open(window, over, [](bool ok, Session session) {
        app.signingIn = false;
        if (ok) signedIn(session);
        else showToast("Sign-in didn't finish");
        invalidate();
    });
}

void verifyPlayback() {
    const Item* song = app.current();
    if (!song || !app.needsVerification || app.signingIn) return;
    app.signingIn = true;
    if (app.tucked) windowTuck(false);
    invalidate();
    const int generation = app.generation;
    RECT over = {(LONG)posX, (LONG)posY, (LONG)posX + pixelW, (LONG)posY + pixelH};
    signin::open(window, over, [generation](bool ok, Session session) {
        app.signingIn = false;
        invalidate();
        if (generation != app.generation) return;
        if (!ok) {
            showToast("You can finish on YouTube in your browser");
            return;
        }
        signedIn(session);
        playIndex(app.index, true);
    }, song->id, app.session.signedIn());
}

void openSongInBrowser() {
    const Item* song = app.current();
    if (!song) return;
    wstring url = W(yt::watchUrl(song->id));
    if ((INT_PTR)ShellExecuteW(window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        showToast("Couldn't open YouTube in your browser");
}

// ---------------------------------------------------------------- Messages

static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_APP + 3: render(); trimMemory(); return 0;
        case WM_SIZE: if (wparam == SIZE_MINIMIZED) trimMemory(); else if (wparam == SIZE_RESTORED) { invalidate(); render(); } return 0;
        case WM_RBUTTONUP: uiRightClick(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale); if (dirty) render(); return 0;
        case WM_UICALL: runUiCall(lparam); if (dirty && window) render(); return 0;
        case WM_AUDIO: onAudio((AudioEvent)wparam, lparam); if (dirty) render(); return 0;
        case WM_TIMER: {
            tick();
            // Out of the way (tucked or minimized): once a minute, hand back whatever built up.
            static long long lastTrim = 0;
            if ((app.tucked || IsIconic(hwnd)) && nowMs() - lastTrim > 60000) { lastTrim = nowMs(); trimMemory(); }
            bool lively = !app.tucked && (app.playing || app.loading || app.searchFocus);
            if (dirty || lively) render();
            else retime();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            float x = GET_X_LPARAM(lparam) / scale, y = GET_Y_LPARAM(lparam) / scale;
            SetCapture(hwnd);
            if (app.tucked || !uiTakes(x, y)) {
                windowDrag = true;
                dragMoved = false;
                GetCursorPos(&dragCursor);
                dragX = posX;
                dragY = posY;
                velX = velY = 0;
            } else uiMouseDown(x, y);
            if (dirty) render();
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!trackingMouse) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                trackingMouse = true;
            }
            if (windowDrag) {
                POINT now;
                GetCursorPos(&now);
                if (abs(now.x - dragCursor.x) > 4 || abs(now.y - dragCursor.y) > 4) dragMoved = true;
                if (dragMoved) {
                    posX = dragX + (now.x - dragCursor.x);
                    posY = dragY + (now.y - dragCursor.y);
                    SetWindowPos(hwnd, nullptr, (int)posX, (int)posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                return 0;
            }
            uiMouseMove(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale);
            if (dirty) render();
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (windowDrag) {
                windowDrag = false;
                POINT now;
                GetCursorPos(&now);
                RECT area = workArea();
                if (!dragMoved) {
                    // A click: the tucked button comes back out.
                    if (app.tucked) windowTuck(false);
                    else uiMouseUp(-1, -1);
                } else if (now.x <= area.left + 24 * scale || now.x >= area.right - 24 * scale) {
                    // Let go against the edge: tuck away right there.
                    app.tuckRight = now.x > (area.left + area.right) / 2;
                    tuckY = now.y;
                    app.tucked = true;
                    app.menuOpen = false;
                    home(pixelW, pixelH, targetX, targetY);
                    invalidate();
                } else {
                    float cx = posX + pixelW / 2, cy = posY + pixelH / 2;
                    app.settings.corner = (cy > (area.top + area.bottom) / 2 ? 2 : 0) + (cx > (area.left + area.right) / 2 ? 1 : 0);
                    if (app.tucked) { app.tucked = false; invalidate(); }
                    saveState();
                    glideHome();
                }
                if (dirty) render();
                if (dragMoved && !moving) glideHome();
                return 0;
            }
            uiMouseUp(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale);
            if (dirty) render();
            return 0;
        }
        case WM_MOUSELEAVE: trackingMouse = false; uiMouseLeave(); if (dirty) render(); return 0;
        case WM_MOUSEWHEEL: {
            POINT at = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &at);
            uiWheel(at.x / scale, at.y / scale, GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA);
            if (dirty) render();
            return 0;
        }
        case WM_CHAR: uiChar((wchar_t)wparam); if (dirty) render(); return 0;
        case WM_KEYDOWN: uiKey(wparam, GetKeyState(VK_CONTROL) < 0); if (dirty) render(); return 0;
        case WM_APPCOMMAND:
            switch (GET_APPCOMMAND_LPARAM(lparam)) {
                case APPCOMMAND_MEDIA_PLAY_PAUSE: togglePlay(); break;
                case APPCOMMAND_MEDIA_NEXTTRACK: nextSong(true); break;
                case APPCOMMAND_MEDIA_PREVIOUSTRACK: previousSong(); break;
                default: return DefWindowProcW(hwnd, message, wparam, lparam);
            }
            if (dirty) render();
            return TRUE;
        case WM_DPICHANGED:
            scale = HIWORD(wparam) / 96.0f;
            gfx::forgetImages();
            invalidate();
            render();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            // The screen or the taskbar changed: back to the corner.
            if (placed && !windowDrag) { home(pixelW, pixelH, targetX, targetY); posX = targetX; posY = targetY; invalidate(); }
            return 0;
        case WM_CLOSE:
            saveState();
            signin::close();
            audio::stop();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: window = nullptr; PostQuitMessage(0); return 0;
        default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

// ---------------------------------------------------------------- The render check

static Item fake(Kind kind, const char* id, const char* title, const char* artist, long long seconds = 0, const char* album = "") {
    Item item;
    item.kind = kind;
    item.id = id;
    item.title = title;
    if (*artist) item.artists.push_back({artist, "UCfake"});
    if (*album) item.album = {album, "MPREfake"};
    item.durationMs = seconds * 1000;
    return item;
}

static void savePicture(const wstring& dir, const wchar_t* name) {
    Size2 want = wantedSize();
    const float s = 2;
    int w = (int)(want.w * s), h = (int)(want.h * s);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP picture = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ before = SelectObject(dc, picture);
    // Twice, with messages handled in between, so covers that were asked for have arrived.
    for (int pass = 0; pass < 2; pass++) {
        uiFrame(-1);   // no motion in a still picture: everything at rest
        if (gfx::begin(dc, w, h, s)) { draw(want.w, want.h, want.w, want.h); gfx::end(); }
        if (pass == 0) {
            long long until = nowMs() + 2500;
            MSG msg;
            while (nowMs() < until) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
                Sleep(20);
            }
        }
    }
    GdiFlush();
    gfx::savePng(dir + L"\\" + name + L".png", bits, w, h);
    SelectObject(dc, before);
    DeleteObject(picture);
    DeleteDC(dc);
}

static LRESULT CALLBACK quietProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_UICALL) { runUiCall(lparam); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static int renderCheck(const wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    WNDCLASSW quiet = {};
    quiet.lpfnWndProc = quietProc;
    quiet.hInstance = GetModuleHandleW(nullptr);
    quiet.lpszClassName = L"WaveMiniQuiet";
    RegisterClassW(&quiet);
    HWND messages = CreateWindowExW(0, quiet.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, quiet.hInstance, nullptr);
    setUiWindow(messages, WM_UICALL);
    string log;

    // Real answers from YouTube Music where it will give them (this also checks the reading of
    // its answers); made-up songs otherwise.
    std::vector<Section> found = yt::search("daft punk", SearchFilter::All);
    log += "search sections: " + std::to_string(found.size()) + "\r\n";
    std::vector<Item> songs;
    Item artist, album;
    for (auto& section : found) {
        log += "  " + section.title + ": " + std::to_string(section.items.size()) + "\r\n";
        for (auto& item : section.items) {
            if (item.kind == Kind::Song && !item.video) songs.push_back(item);
            if (item.kind == Kind::Artist && artist.id.empty()) artist = item;
            if (item.kind == Kind::Album && album.id.empty()) album = item;
        }
    }
    NextResult radio;
    if (!songs.empty()) radio = yt::next(songs[0].id, "RDAMVM" + songs[0].id);
    log += "radio songs: " + std::to_string(radio.songs.size()) + ", lyrics id: " + radio.lyricsId + "\r\n";
    if (radio.songs.size() < 4) {
        radio.songs = {fake(Kind::Song, "a1", "Harbor Lights", "Lumen Coast", 204, "Tidewater"), fake(Kind::Song, "a2", "Paper Moons", "Ivy Halden", 178),
                       fake(Kind::Song, "a3", "Slow Static", "The Northern Wires", 252), fake(Kind::Song, "a4", "Glasshouse", "Mira Sol", 185),
                       fake(Kind::Song, "a5", "Undertow", "Lumen Coast", 213), fake(Kind::Song, "a6", "Kite Season", "Ivy Halden, Odo", 227)};
    }
    if (found.empty()) {
        Section top{"Top result", {fake(Kind::Artist, "UCfake", "Lumen Coast", ""), radio.songs[0], radio.songs[4]}, "", ""};
        found.push_back(top);
    }

    app.hasSession = true;
    app.session.guest = true;
    app.queue = radio.songs;
    app.index = 0;
    app.source = "Songs like " + app.queue[0].title;
    app.playing = true;
    app.liked.insert(app.queue[0].id);
    app.settings.tab = Tab::UpNext;
    savePicture(dir, L"01-upnext");

    app.settings.tab = Tab::Lyrics;
    app.lyricsFor = app.queue[0].id;
    const char* lines[] = {"The harbor hums a quiet tune", "Boats asleep beneath the moon", "And every light along the shore is calling me back home",
                           "I kept your letters in the rain", "Read them twice and walked again", "Down to where the water knows my name", "Harbor lights, harbor lights"};
    for (int i = 0; i < 7; i++) { LyricLine l; l.at = i == 0 ? 0 : 1000 + i; l.text = lines[i]; app.lyrics.push_back(l); }
    app.lyrics[1].at = 0;
    app.lyrics[2].at = 0;
    app.lyricsSynced = true;
    {
        // The third line is being sung, part-way through its fifth word.
        LyricLine& sung = app.lyrics[2];
        std::vector<string> words = split(sung.text, " ");
        int from = 0;
        for (size_t i = 0; i < words.size(); i++) {
            LyricLine::Word word;
            word.at = i < 4 ? 0 : i == 4 ? 0.02 : 50;
            word.end = i < 4 ? 0.01 : i == 4 ? 0.22 : 51;
            word.from = from;
            word.count = (int)W(words[i]).size();
            sung.words.push_back(word);
            from += word.count + 1;
        }
    }
    savePicture(dir, L"02-lyrics");

    app.settings.tab = Tab::Search;
    app.query = L"daft punk";
    app.searched = "daft punk";
    app.results = found;
    app.searchFocus = true;
    savePicture(dir, L"03-search");
    app.searchFocus = false;

    Page artistPage;
    artistPage.type = Page::Artist;
    artistPage.loading = false;
    if (!artist.id.empty()) artistPage.artist = yt::artist(artist.id);
    log += "artist: " + artistPage.artist.artist.title + ", sections: " + std::to_string(artistPage.artist.sections.size()) + "\r\n";
    if (!artistPage.artist.ok) {
        artistPage.artist.ok = true;
        artistPage.artist.artist = fake(Kind::Artist, "UCfake", "Lumen Coast", "");
        artistPage.artist.shuffleId = artistPage.artist.radioId = "x";
        artistPage.artist.sections.push_back(Section{"Top songs", {radio.songs[0], radio.songs[4], radio.songs[1]}, "VLx", ""});
        artistPage.artist.sections.push_back(Section{"Albums", {fake(Kind::Album, "MPREa", "Tidewater", "Lumen Coast"), fake(Kind::Album, "MPREb", "Static Bloom", "Lumen Coast"), fake(Kind::Album, "MPREc", "First Light", "Lumen Coast")}, "x", ""});
    }
    app.searchPages.push_back(artistPage);
    savePicture(dir, L"04-artist");
    app.searchPages.clear();

    Page albumPage;
    albumPage.type = Page::List;
    albumPage.loading = false;
    if (!album.id.empty()) albumPage.list = yt::album(album.id);
    log += "album: " + albumPage.list.head.title + ", songs: " + std::to_string(albumPage.list.songs.size()) + "\r\n";
    if (albumPage.list.songs.empty()) {
        albumPage.list.ok = true;
        albumPage.list.head = fake(Kind::Album, "MPREa", "Tidewater", "Lumen Coast");
        albumPage.list.head.year = "2025";
        albumPage.list.songs = {fake(Kind::Song, "b1", "Low Tide", "Lumen Coast", 192), app.queue[0], fake(Kind::Song, "b3", "Saltwater Letters", "Lumen Coast", 241),
                                fake(Kind::Song, "b4", "Undertow", "Lumen Coast", 213), fake(Kind::Song, "b5", "Lighthouse Keeper", "Lumen Coast", 238)};
    }
    app.searchPages.push_back(albumPage);
    savePicture(dir, L"05-album");
    app.searchPages.clear();

    app.settings.tab = Tab::Library;
    savePicture(dir, L"06-library-guest");
    app.session.guest = false;
    app.session.cookie = "SAPISID=fake";
    app.session.name = "Ayden";
    for (int i = 0; i < 3; i++) app.libraryLoaded[i] = true;
    app.library[0] = {fake(Kind::Playlist, "p1", "Late night drives", ""), fake(Kind::Playlist, "p2", "Focus, no words", ""),
                      fake(Kind::Playlist, "p3", "Sunday kitchen", ""), fake(Kind::Playlist, "p4", "Gym, loud", "")};
    savePicture(dir, L"07-library");

    app.naming = true;
    app.newName = L"Road trip";
    savePicture(dir, L"17-new-playlist");
    app.naming = false;
    app.newName.clear();

    app.settings.tab = Tab::Account;
    savePicture(dir, L"08-account");

    app.settings.tab = Tab::UpNext;
    app.ctxOpen = true;
    app.ctxItem = app.queue[2];
    app.ctxX = 150;
    app.ctxY = 300;
    savePicture(dir, L"18-song-menu-anywhere");
    app.ctxOpen = false;

    app.settings.tab = Tab::UpNext;
    app.menuOpen = true;
    savePicture(dir, L"09-menu");
    app.menuPage = 1;
    savePicture(dir, L"10-menu-playlists");
    app.menuOpen = false;
    app.menuPage = 0;

    app.settings.tab = Tab::None;
    savePicture(dir, L"11-folded");

    app.tucked = true;
    savePicture(dir, L"12-tucked");
    app.tucked = false;

    app.settings.tab = Tab::Search;
    app.index = -1;
    app.queue.clear();
    app.playing = false;
    app.results.clear();
    app.query.clear();
    app.searched.clear();
    app.recent = {"lumen coast", "rainy day jazz", "ivy halden"};
    app.last = albumPage.list.head;
    app.hasLast = true;
    savePicture(dir, L"13-nothing-playing");

    string more;
    app.home = yt::home(more);
    app.homeLoaded = true;
    app.homeX.assign(app.home.size(), 0);
    log += "home sections: " + std::to_string(app.home.size()) + "\r\n";
    for (auto& section : app.home) log += "  " + section.title + ": " + std::to_string(section.items.size()) + "\r\n";
    app.recent.clear();
    savePicture(dir, L"15-home");

    yt::Suggestions suggestions = yt::suggest("daft");
    log += "suggestions: " + std::to_string(suggestions.queries.size()) + " searches, " + std::to_string(suggestions.items.size()) + " items\r\n";
    app.suggestQueries = suggestions.queries;
    app.suggestItems = suggestions.items;
    app.query = L"daft";
    app.searchFocus = true;
    savePicture(dir, L"16-suggestions");
    app.searchFocus = false;
    app.query.clear();

    app.hasSession = false;
    savePicture(dir, L"14-sign-in");

    writeFile(dir + L"\\render.txt", log);
    return 0;
}

// ---------------------------------------------------------------- Start

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Crisp on every screen, whatever its scaling.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI* SetContext)(HANDLE);
    if (SetContext set = (SetContext)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext")) set((HANDLE)-4 /* per monitor, v2 */);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    srand((unsigned)GetTickCount64());
    if (!gfx::start()) return 1;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && wstring(argv[1]) == L"--render") return renderCheck(argv[2]);

    // One WaveMini at a time: a second start just brings the first forward.
    HANDLE once = CreateMutexW(nullptr, TRUE, L"WaveMini.SingleInstance");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"WaveMiniWindow", nullptr)) SetForegroundWindow(other);
        return 0;
    }

    loadState();
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = L"WaveMiniWindow";
    RegisterClassExW(&cls);
    window = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW | (app.settings.onTop ? WS_EX_TOPMOST : 0), cls.lpszClassName, L"WaveMini",
                             WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    typedef UINT(WINAPI* DpiForWindow)(HWND);
    if (DpiForWindow dpi = (DpiForWindow)(void*)GetProcAddress(user32, "GetDpiForWindow")) scale = std::max(1.0f, dpi(window) / 96.0f);
    setUiWindow(window, WM_UICALL);
    gfx::onImage([] { invalidate(); });
    audio::start(window, WM_AUDIO);
    // Start in the saved corner of the screen the pointer is on.
    POINT cursor;
    GetCursorPos(&cursor);
    posX = (float)cursor.x;
    posY = (float)cursor.y;
    if (app.session.signedIn()) signedIn(app.session);
    if (app.settings.tab == Tab::Library) loadLibrary(app.libraryTab);
    render();
    ShowWindow(window, SW_SHOWNOACTIVATE);
    background([] { yt::warmUp(); });

    // While anything is moving, one frame per screen refresh (whatever the monitor's rate);
    // otherwise asleep until something happens.
    MSG msg;
    LARGE_INTEGER frequency, last = {}, now;
    QueryPerformanceFrequency(&frequency);
    bool running = true, inMotion = false;
    while (running) {
        if (window && (moving || uiAnimating()) && !windowDrag) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running || !window) break;
            QueryPerformanceCounter(&now);
            float dt = inMotion ? (float)((double)(now.QuadPart - last.QuadPart) / (double)frequency.QuadPart) : 1 / 60.0f;
            dt = std::max(0.001f, std::min(0.05f, dt));
            last = now;
            inMotion = true;
            if (moving) stepWindow(dt);
            uiFrame(dt);
            render();
            if (FAILED(DwmFlush())) Sleep(8);
        } else {
            inMotion = false;
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return 0;
}
