// Everything WaveMini shows, drawn fresh each frame: the player, the tabs and their panels,
// the song menu, sign-in and the tucked button. Sizes are the design's (a 420 wide card).
#include "app.h"

namespace {

const float W_ = 420, PLAYER = 156, TABS = 56, PANEL = 340, TOP = PLAYER + TABS;

struct Theme { Color accent, onAccent, bg, glowA, glowB, soft; };
Theme theme;

// ---------------------------------------------------------------- Input

struct Hit {
    Rect r;
    std::function<void()> click;
    std::function<void(float, float, bool)> drag;   // x, y, released
    std::function<void(float, float)> context;      // right-click, at x, y
};
struct ScrollArea { Rect r; float* value; float max; };

std::vector<Hit> hits;
std::vector<ScrollArea> scrolls;
float mouseX = -1, mouseY = -1, downX = -1, downY = -1;
bool mouseIn = false, inputOn = true, clipOn = false;
Rect clipRect;
std::function<void(float, float, bool)> dragging, pendingDrag;
bool lastPushed = false;             // the last click() really added something to hit
double seekPreview = -1;

Rect visible(Rect r) {
    if (!clipOn) return r;
    float x1 = std::max(r.x, clipRect.x), y1 = std::max(r.y, clipRect.y);
    float x2 = std::min(r.right(), clipRect.right()), y2 = std::min(r.bottom(), clipRect.bottom());
    return Rect{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
}

bool live = true;                    // false while drawing something that's only fading or morphing
bool hover(Rect r) { return inputOn && live && mouseIn && !dragging && visible(r).has(mouseX, mouseY); }

// ---------------------------------------------------------------- Motion

float frameDt = 0;                   // seconds since the last frame
bool animating = false;              // set while drawing: something hasn't come to rest
struct Spring { float x, v; };
std::map<int, Spring> springs;

// The value under [key], springing toward [target] (the same spring as Wave's mini player:
// quick, with a hint of overshoot). The first time it's simply there.
float glide(int key, float target, float stiffness = 420, float damping = 0.82f) {
    auto found = springs.find(key);
    if (found == springs.end()) { springs[key] = Spring{target, 0}; return target; }
    Spring& s = found->second;
    if (frameDt < 0) { s.x = target; s.v = 0; return target; }   // settle at once (the render check)
    float left = std::min(frameDt, 0.05f);
    while (left > 0) {
        float h = std::min(left, 0.006f);
        s.v += (-stiffness * (s.x - target) - 2 * damping * sqrtf(stiffness) * s.v) * h;
        s.x += s.v * h;
        left -= h;
    }
    if (fabsf(s.x - target) < 0.004f && fabsf(s.v) < 0.05f) { s.x = target; s.v = 0; }
    else animating = true;
    return s.x;
}

// 0 -> 1 after [key] changes: what's new fades and slides in.
struct Arrival { string key; float t = 1; };
float arrive(Arrival& a, const string& key, float seconds = 0.24f) {
    if (frameDt < 0) { a.key = key; a.t = 1; }
    else if (a.key != key) { a.key = key; a.t = 0; }
    else if (a.t < 1) a.t = std::min(1.0f, a.t + frameDt / seconds);
    if (a.t < 1) animating = true;
    float left = 1 - a.t;
    return 1 - left * left * left;
}


bool click(Rect r, std::function<void()> fn) {
    lastPushed = false;
    if (!inputOn || !live) return false;
    Rect v = visible(r);
    if (v.w <= 0 || v.h <= 0) return false;
    hits.push_back({v, std::move(fn), nullptr, nullptr});
    lastPushed = true;
    return hover(r);
}

void dragArea(Rect r, std::function<void(float, float, bool)> fn) {
    if (inputOn && live) hits.push_back({visible(r), nullptr, std::move(fn), nullptr});
}

// Starts a scrolling list; returns the y its content starts at.
float beginScroll(Rect r, float& value) {
    gfx::pushClip(r);
    clipOn = true;
    clipRect = r;
    if (inputOn && live) scrolls.push_back({r, &value, 0});
    return r.y - value;
}

void endScroll(Rect r, float& value, float contentEnd) {
    float height = contentEnd - (r.y - value);
    float max = std::max(0.0f, height - r.h);
    if (value > max) value = max;
    if (value < 0) value = 0;
    if (inputOn && live && !scrolls.empty()) scrolls.back().max = max;
    clipOn = false;
    gfx::popClip();
    // A thin bar while there's more than fits.
    if (max > 0) {
        float bar = std::max(24.0f, r.h * r.h / height), at = r.y + (r.h - bar) * (value / max);
        gfx::fill(Rect{r.right() - 5, at, 3, bar}, white(0.22f), 1.5f);
    }
}

// ---------------------------------------------------------------- Pieces

const int Regular = 400, Semi = 600, Bold = 700;

void label(const string& s, float x, float y, float w, float size, int weight, Color c, int align = AlignLeft) {
    gfx::text(s, Rect{x, y, w, size * 1.45f}, size, weight, c, align);
}

bool iconButton(Rect r, Icon icon, float size, Color bg, Color fg, float radius, std::function<void()> fn) {
    bool over = click(r, std::move(fn));
    Color back = over ? Color{bg.r, bg.g, bg.b, std::min(1.0f, bg.a + 0.12f)} : bg;
    if (back.a > 0) gfx::fill(r, back, radius);
    gfx::icon(icon, Rect{r.x + (r.w - size) / 2, r.y + (r.h - size) / 2, size, size}, fg);
    return over;
}

// A button with a label (and an icon): filled with the accent, or tonal. Returns its width.
float pill(float x, float y, float h, const string& text, int iconOrNone, bool primary, float size, float pad, float radius, std::function<void()> fn) {
    float iconSize = size + 2, textW = gfx::textWidth(text, size, Bold);
    float w = pad * 2 + textW + (iconOrNone >= 0 ? iconSize + 6 : 0);
    Rect r{x, y, w, h};
    bool over = click(r, std::move(fn));
    Color bg = primary ? (over ? mix(theme.accent, white(), 0.12f) : theme.accent) : white(over ? 0.18f : 0.1f);
    Color fg = primary ? theme.onAccent : white();
    gfx::fill(r, bg, radius);
    float at = x + pad;
    if (iconOrNone >= 0) { gfx::icon((Icon)iconOrNone, Rect{at, y + (h - iconSize) / 2, iconSize, iconSize}, fg); at += iconSize + 6; }
    gfx::text(text, Rect{at, y, textW + 2, h}, size, Bold, fg);
    return w;
}

float chip(float x, float y, const string& text, bool selected, std::function<void()> fn) {
    float w = gfx::textWidth(text, 13, selected ? Bold : Semi) + 24;
    Rect r{x, y, w, 30};
    bool over = click(r, std::move(fn));
    gfx::fill(r, selected ? theme.accent : white(over ? 0.16f : 0.08f), 8);
    gfx::text(text, r, 13, selected ? Bold : Semi, selected ? theme.onAccent : white(), AlignCenter);
    return w;
}

string upper(string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }

void cover(const string& art, Rect r, float radius, int px) {
    gfx::fill(r, white(0.08f), radius);
    if (!art.empty()) gfx::image(artAt(art, px), r, radius);
}

// Opens the menu for a song at a point ([playlist]: the playlist it's listed in, if any).
std::function<void(float, float)> songMenu(const Item& item, const string& playlist = "") {
    if (item.kind != Kind::Song) return nullptr;
    Item copy = item;
    return [copy, playlist](float x, float y) {
        app.ctxOpen = true;
        app.ctxPage = 0;
        app.ctxItem = copy;
        app.ctxPlaylist = playlist;
        app.ctxX = x;
        app.ctxY = y;
        app.ctxScroll = 0;
    };
}

// The thing just made clickable also opens [menu] on a right-click.
void withMenu(const std::function<void(float, float)>& menu) {
    if (lastPushed && menu && !hits.empty()) hits.back().context = menu;
}

// A row: cover, title and a grey line, and an optional bit of text on the right. With a
// [menu], right-clicking opens it, and so do the three dots that show under the pointer.
// Returns where the row is in the list of clickable things (-1 when it isn't).
int itemRow(Rect r, const Item& item, const string& sub, const string& right, bool current, std::function<void()> fn, std::function<void(float, float)> menu = nullptr) {
    bool over = click(r, std::move(fn));
    int index = lastPushed ? (int)hits.size() - 1 : -1;
    withMenu(menu);
    if (current) gfx::fill(r, theme.accent.alpha(0.16f), 10);
    else if (over) gfx::fill(r, white(0.07f), 10);
    float art = std::min(40.0f, r.h - 10);
    Rect coverRect{r.x + 6, r.y + (r.h - art) / 2, art, art};
    cover(item.art, coverRect, item.kind == Kind::Artist ? art / 2 : 6, 120);
    bool dots = over && menu != nullptr;
    float rightW = dots ? 30 : right.empty() ? 0 : gfx::textWidth(right, 12, Regular) + 10;
    float tx = coverRect.right() + 12, tw = r.right() - tx - rightW - 8;
    Color titleColor = current ? theme.soft : white();
    if (sub.empty()) gfx::text(item.title, Rect{tx, r.y, tw, r.h}, 14, Bold, titleColor);
    else {
        gfx::text(item.title, Rect{tx, r.y + r.h / 2 - 19, tw, 20}, 14, Bold, titleColor);
        gfx::text(sub, Rect{tx, r.y + r.h / 2 + 1, tw, 18}, 12.5f, Regular, white(0.7f));
    }
    if (dots) {
        Rect more{r.right() - 34, r.y + (r.h - 28) / 2, 28, 28};
        iconButton(more, Icon::More, 14, white(0.1f), white(0.9f), 8, [menu, more] { menu(more.x - 190, more.bottom() + 2); });
    } else if (!right.empty()) gfx::text(right, Rect{r.right() - rightW - 6, r.y, rightW, r.h}, 12, Regular, white(0.65f), AlignRight);
    return index;
}

void centered(const string& title, const string& sub, float top, float height) {
    gfx::text(title, Rect{30, top + height / 2 - 26, W_ - 60, 24}, 15, Bold, white(0.9f), AlignCenter);
    if (!sub.empty()) gfx::text(sub, Rect{40, top + height / 2 - 2, W_ - 80, 40}, 13, Regular, white(0.65f), AlignCenter, true, true);
}

void toggle(float y, const string& text, bool on, std::function<void()> fn) {
    Rect row{10, y, W_ - 20, 40};
    bool over = click(row, std::move(fn));
    if (over) gfx::fill(row, white(0.05f), 10);
    gfx::text(text, Rect{16, y, 300, 40}, 14, Semi, white());
    Rect track{W_ - 16 - 48, y + 6, 48, 28};
    if (on) {
        gfx::fill(track, theme.accent, 14);
        gfx::circle(track.x + 34, track.y + 14, 10, theme.onAccent);
    } else {
        gfx::fill(track, white(0.12f), 14);
        gfx::outline(track, white(0.4f), 14, 2);
        gfx::circle(track.x + 14, track.y + 14, 8, white(0.6f));
    }
}

// A slider; [set] gets 0..1 while dragging, [done] when let go.
void slider(Rect r, float value, std::function<void(float)> set, std::function<void()> done) {
    float mid = r.y + r.h / 2;
    gfx::fill(Rect{r.x, mid - 2, r.w, 4}, white(0.2f), 2);
    gfx::fill(Rect{r.x, mid - 2, r.w * value, 4}, theme.accent, 2);
    gfx::circle(r.x + r.w * value, mid, 8, white());
    Rect area{r.x - 8, r.y, r.w + 16, r.h};
    dragArea(area, [r, set, done](float x, float, bool released) {
        set(std::max(0.0f, std::min(1.0f, (x - r.x) / r.w)));
        if (released && done) done();
    });
}

// ---------------------------------------------------------------- Colors from the cover

string playerArt() {
    if (const Item* song = app.current()) return song->art;
    return app.hasLast ? app.last.art : string();
}

void pickTheme() {
    // WaveMini's own blue until a cover says otherwise.
    Theme want = Theme{hex(0xa8c7ff), hex(0x0a2a5c), hex(0x0a1120), hex(0x1d3266), hex(0x13224a), hex(0xcfe0ff)};
    Color c;
    string art = playerArt();
    if (!art.empty() && gfx::imageColor(artAt(art, 360), c)) {
        float h, sat0, l;
        toHsl(c, h, sat0, l);
        float sat = sat0 < 0.08f ? 0.06f : std::max(0.35f, std::min(0.62f, sat0 * 1.25f));
        want.accent = fromHsl(h, sat, 0.73f);
        want.onAccent = fromHsl(h, sat * 0.85f, 0.15f);
        want.bg = fromHsl(h + 6, sat * 0.95f, 0.055f);
        want.glowA = fromHsl(h + 14, std::min(0.7f, sat * 1.05f), 0.29f);
        want.glowB = fromHsl(h - 10, std::min(0.75f, sat * 1.2f), 0.17f);
        want.soft = fromHsl(h, std::min(0.75f, sat * 1.2f), 0.84f);
    }
    static bool first = true;
    if (first) { theme = want; first = false; return; }
    float k = frameDt < 0 ? 1.0f : 1 - expf(-frameDt * 7);
    auto step = [k](Color& from, Color to) {
        float apart = fabsf(from.r - to.r) + fabsf(from.g - to.g) + fabsf(from.b - to.b);
        if (apart < 0.006f || k >= 1) { from = to; return; }
        from = mix(from, to, k);
        animating = true;
    };
    step(theme.accent, want.accent);
    step(theme.onAccent, want.onAccent);
    step(theme.bg, want.bg);
    step(theme.glowA, want.glowA);
    step(theme.glowB, want.glowB);
    step(theme.soft, want.soft);
}

void backdrop(float w, float h) {
    // The glows are the costliest thing here to paint, and only change with the song.
    char key[96];
    sprintf(key, "%.3f %.3f %.3f %.3f %.3f %.3f %.0f", theme.bg.r, theme.bg.g, theme.glowA.r, theme.glowA.g, theme.glowB.r, theme.glowB.b, h);
    gfx::cached(key, Rect{0, 0, w, h}, [w, h] {
        gfx::fill(Rect{0, 0, w, h}, theme.bg);
        gfx::glow(w * 0.12f, 0, 312, 190, theme.glowA);
        gfx::glow(w * 0.88f, std::max(h * 0.85f, 190.0f), 246, 250, theme.glowB);
    });
}

// ---------------------------------------------------------------- The player

void windowButtons(bool withMenu, Color bg) {
    float x = W_ - 6 - 28;
    iconButton(Rect{x, 6, 28, 28}, Icon::Close, 14, bg, white(0.9f), 8, [] { windowClose(); });
    x -= 30;
    iconButton(Rect{x, 6, 28, 28}, Icon::Minus, 14, bg, white(0.9f), 8, [] { windowTuck(true); });
    if (!withMenu) return;
    x -= 30;
    bool open = app.menuOpen;
    iconButton(Rect{x, 6, 28, 28}, Icon::More, 14, open ? theme.accent : bg, open ? theme.onAccent : white(0.9f), 8, [] {
        app.menuOpen = !app.menuOpen;
        app.menuPage = 0;
    });
}

void drawPlayer() {
    const Item* song = app.current();
    string art = playerArt();
    if (!art.empty()) gfx::image(artAt(art, 360), Rect{0, 0, 180, PLAYER}, 0, 0.55f);
    else {
        // Nothing has ever played: Wave's own mark.
        gfx::fill(Rect{18, 22, 112, 112}, hex(0x36618E), 31);
        gfx::icon(Icon::Wave, Rect{18, 22, 112, 112}, white());
    }
    windowButtons(song != nullptr, Color{0, 0, 0, 0.22f});

    const float left = 150, textW = W_ - left - 102;
    if (!song) {
        if (!app.hasLast) {
            label("Nothing playing", left, 22, textW + 60, 16, Bold, white());
            gfx::text("Search for a song, or open your library.", Rect{left, 46, W_ - left - 20, 40}, 13, Regular, white(0.75f), AlignLeft, true, true);
            return;
        }
        gfx::text("LAST PLAYED", Rect{left, 15, textW, 14}, 11, Bold, theme.accent, AlignLeft, false, false, 0.9f);
        label(app.last.title, left, 31, textW + 30, 16, Bold, white());
        label(app.last.subtitle(), left, 54, W_ - left - 16, 13, Regular, white(0.75f));
        float x = left;
        x += pill(x, 96, 42, "Play", (int)Icon::Play, true, 14, 16, 14, [] { playLast(false); }) + 6;
        if (app.last.kind != Kind::Song) pill(x, 96, 42, "Shuffle", (int)Icon::Shuffle, false, 14, 14, 14, [] { playLast(true); });
        return;
    }

    label(song->title, left, 14, textW, 16, Bold, white());
    string artist = song->artistText();
    float artistW = std::min(W_ - left - 16, gfx::textWidth(artist, 13, Regular) + 4);
    Rect artistRect{left, 38, artistW, 19};
    Item copy = *song;
    bool overArtist = !copy.artists.empty() && click(artistRect, [copy] {
        app.settings.tab = Tab::Search;
        if (!copy.artists[0].id.empty()) openPage(app.searchPages, Page::Artist, copy.artists[0].id, copy.artists[0].name);
        else { app.query = W(copy.artists[0].name); app.searchPages.clear(); runSearch(); }
    });
    gfx::text(artist, artistRect, 13, Regular, white(overArtist ? 1.0f : 0.75f));

    // Where you are in the song (or what went wrong).
    if (app.needsVerification) {
        float x = left + pill(left, 62, 30, app.signingIn ? "Waiting for YouTube" : "Verify on YouTube", -1,
                              true, 12, 9, 8, verifyPlayback) + 6;
        pill(x, 62, 30, "Open in browser", -1, false, 12, 9, 8, openSongInBrowser);
    } else if (!app.error.empty()) {
        gfx::text(app.error, Rect{left, 62, W_ - left - 16, 30}, 12.5f, Semi, theme.soft, AlignLeft, true, false);
    } else {
        double length = audio::duration(), at = audio::position();
        if (length <= 0 && song->durationMs > 0) length = song->durationMs / 1000.0;
        if (app.loading) at = 0;
        if (seekPreview >= 0) at = seekPreview * length;
        string a = formatTime(at), b = formatTime(length);
        float aw = gfx::textWidth(a, 12, Regular), bw = gfx::textWidth(b, 12, Regular);
        gfx::text(a, Rect{left, 64, aw + 2, 20}, 12, Regular, white(0.85f));
        gfx::text(b, Rect{W_ - 16 - bw - 2, 64, bw + 2, 20}, 12, Regular, white(0.85f), AlignRight);
        Rect bar{left + aw + 10, 66, W_ - 16 - bw - 10 - (left + aw + 10), 16};
        float phase = app.playing ? (float)(nowMs() % 100000) / 1000.0f * -3.2f : 0;
        gfx::wave(bar, length > 0 ? (float)(at / length) : 0, phase, glide(60, app.playing ? 2.4f : 0.0f), theme.accent);
        if (length > 0 && !app.loading) dragArea(Rect{bar.x - 4, bar.y - 6, bar.w + 8, bar.h + 12}, [bar, length](float x, float, bool released) {
            double f = std::max(0.0f, std::min(1.0f, (x - bar.x) / bar.w));
            seekPreview = f;
            if (released) { seekPreview = -1; seekTo(f * length); }
        });
    }

    bool liked = app.liked.count(song->id) > 0;
    iconButton(Rect{left - 4, 98, 44, 44}, liked ? Icon::Liked : Icon::Like, 22, white(0), liked ? theme.accent : white(), 12, [] { toggleLike(); });
    iconButton(Rect{250, 98, 44, 44}, Icon::Prev, 22, white(0), white(), 12, [] { previousSong(); });
    iconButton(Rect{362, 98, 44, 44}, Icon::Next, 22, white(0), white(), 12, [] { nextSong(true); });
    Rect main{298, 97, 60, 46};
    bool over = click(main, [] { togglePlay(); });
    gfx::fill(main, over ? mix(theme.accent, white(), 0.12f) : theme.accent, 16);
    if (app.loading) {
        // Three dots taking turns while the song is fetched.
        float t = (float)(nowMs() % 900) / 900.0f;
        for (int i = 0; i < 3; i++) {
            float k = fmodf(t - i * 0.18f + 1, 1);
            gfx::circle(main.x + 18 + i * 12, main.y + 23, 3.2f, theme.onAccent.alpha(0.35f + 0.65f * (k < 0.5f ? 1 - k * 2 : 0)));
        }
    } else {
        bool showPause = app.playing || (app.wantPlay && app.error.empty() && !audio::paused());
        gfx::icon(showPause ? Icon::Pause : Icon::Play, Rect{main.x + 19, main.y + 12, 22, 22}, theme.onAccent);
    }
}

// ---------------------------------------------------------------- The tabs

void drawTabs() {
    struct Entry { Tab tab; Icon icon; const char* name; };
    const Entry entries[5] = {{Tab::UpNext, Icon::Queue, "Up next"}, {Tab::Lyrics, Icon::Lyrics, "Lyrics"}, {Tab::Search, Icon::Search, "Search"},
                              {Tab::Library, Icon::Library, "Library"}, {Tab::Account, Icon::Person, "Account"}};
    Tab active = app.settings.tab;
    const float gap = 4, total = W_ - 20 - gap * 4;
    float weights[5], sum = 0;
    for (int i = 0; i < 5; i++) { weights[i] = std::max(0.6f, glide(100 + i, entries[i].tab == active ? 2.4f : 1.0f)); sum += weights[i]; }
    float x = 10;
    for (int i = 0; i < 5; i++) {
        const Entry& e = entries[i];
        float open = std::max(0.0f, std::min(1.0f, (weights[i] - 1) / 1.4f));   // 0 closed .. 1 open
        Rect r{x, PLAYER + 8, total * weights[i] / sum, 40};
        Tab tab = e.tab;
        bool over = click(r, [tab] {
            app.settings.tab = app.settings.tab == tab ? Tab::None : tab;
            app.searchFocus = false;
            if (app.settings.tab == Tab::Lyrics) loadLyrics();
            if (app.settings.tab == Tab::Library) loadLibrary(app.libraryTab);
            saveState();
        });
        float lit = glide(110 + i, over ? 1.0f : 0.0f, 700, 1);
        gfx::fillCorners(r, mix(white(0.08f + 0.07f * lit), theme.accent, open), i == 0 ? 12 : 4, i == 4 ? 12 : 4, i == 4 ? 12 : 4, i == 0 ? 12 : 4);
        Color fg = mix(white(), theme.onAccent, open);
        float fullLabel = gfx::textWidth(e.name, 14, Bold) + 8, labelW = fullLabel * open;
        float iconW = e.tab == Tab::Account ? 24 : 18;
        float at = r.x + (r.w - iconW - labelW) / 2;
        if (e.tab == Tab::Account) {
            // Your picture (or your initial).
            Rect face{at, r.y + 8, 24, 24};
            gfx::circle(face.x + 12, face.y + 12, 12, hex(0x7b8ef0));
            bool drawn = !app.session.photo.empty() && gfx::image(artAt(app.session.photo, 96), face, 12);
            if (!drawn) {
                if (app.session.name.empty()) gfx::icon(Icon::Person, face.inset(4), hex(0x0d1640));
                else gfx::text(upper(app.session.name.substr(0, 1)), face, 12, Bold, hex(0x0d1640), AlignCenter);
            }
        } else gfx::icon(e.icon, Rect{at, r.y + 11, 18, 18}, fg);
        if (open > 0.03f) {
            gfx::pushClip(Rect{at + iconW, r.y, labelW + 2, 40});
            gfx::text(e.name, Rect{at + iconW + 8, r.y, fullLabel, 40}, 14, Bold, fg.alpha(open));
            gfx::popClip();
        }
        x += r.w + gap;
    }
}

// ---------------------------------------------------------------- Up next

int dragRow = -1;                    // the queue entry being dragged to a new place
float dragRowY = 0, dragGrab = 0;

void drawUpNext() {
    gfx::text("Playing from", Rect{16, TOP, 90, 40}, 13, Regular, white(0.7f));
    float fromW = gfx::textWidth("Playing from ", 13, Regular);
    gfx::text(app.source.empty() ? "nothing yet" : app.source, Rect{16 + fromW, TOP, 320 - fromW, 40}, 13, Bold, white());
    bool shuffle = app.shuffle, repeat = app.repeat > 0;
    iconButton(Rect{W_ - 16 - 72, TOP + 3, 34, 34}, Icon::Shuffle, 18, shuffle ? theme.accent.alpha(0.18f) : white(0), shuffle ? theme.accent : white(0.8f), 10, [] { toggleShuffle(); });
    iconButton(Rect{W_ - 16 - 34, TOP + 3, 34, 34}, app.repeat == 2 ? Icon::RepeatOne : Icon::Repeat, 18, repeat ? theme.accent.alpha(0.18f) : white(0), repeat ? theme.accent : white(0.8f), 10, [] { cycleRepeat(); });

    Rect area{0, TOP + 40, W_, PANEL - 48};
    int first = app.index + 1, count = (int)app.queue.size();
    if (first >= count) {
        dragRow = -1;
        centered(app.current() ? (app.fetchingMore ? "Finding more songs..." : "Nothing up next") : "Nothing playing",
                 app.current() ? "Songs you queue up show here." : "Pick something in Search or your Library.", area.y, area.h);
        return;
    }
    if (dragRow >= count || dragRow < first) dragRow = -1;
    float top = beginScroll(area, app.queueScroll);
    auto slotAt = [first, count](float rowY, float listTop) {
        return std::max(first, std::min(count - 1, first + (int)floorf((rowY + 28 - listTop) / 56)));
    };
    int slot = dragRow < 0 ? -1 : slotAt(dragRowY, top);
    float y = top;
    for (int i = first; i < count; i++, y += 56) {
        float rowY = y;
        if (dragRow >= 0) {
            // The others step aside for the one being dragged.
            float aside = (dragRow < i && i <= slot) ? -56.0f : (slot <= i && i < dragRow) ? 56.0f : 0.0f;
            rowY += glide(4000 + i, aside, 620, 1);
        }
        if (i == dragRow || rowY + 56 < area.y || rowY > area.bottom()) continue;
        const Item& song = app.queue[i];
        int hit = itemRow(Rect{10, rowY + 2, W_ - 20 - 36, 52}, song, song.artistText(), song.durationMs ? formatTime(song.durationMs / 1000.0) : "", false,
                          [i] { playIndex(i); }, songMenu(song));
        if (hit >= 0 && dragRow < 0) hits[hit].drag = [i, rowY, top, slotAt](float, float py, bool released) {
            if (dragRow < 0) { dragRow = i; dragGrab = py - rowY; }
            dragRowY = py - dragGrab;
            if (released) {
                int to = slotAt(dragRowY, top);
                int from = dragRow;
                dragRow = -1;
                for (int k = 4000; k < 4000 + (int)app.queue.size() + 1; k++) springs.erase(k);
                moveInQueue(from, to);
            }
        };
        iconButton(Rect{W_ - 10 - 34, rowY + 12, 32, 32}, Icon::Close, 16, white(0), white(0.7f), 8, [i] { removeFromQueue(i); });
    }
    if (dragRow >= 0) {
        const Item& song = app.queue[dragRow];
        bool was = live;
        live = false;
        Rect r{10, dragRowY + 2, W_ - 20, 52};
        gfx::fill(r, mix(theme.bg, theme.accent, 0.22f), 10);
        itemRow(r, song, song.artistText(), "", false, [] {});
        live = was;
        animating = true;
    }
    endScroll(area, app.queueScroll, top + (count - first) * 56.0f);
}

// ---------------------------------------------------------------- Lyrics

void drawLyrics() {
    Rect area{0, TOP, W_, PANEL};
    if (!app.current()) { centered("No song playing", "Lyrics show here while a song plays.", area.y, area.h); return; }
    if (app.lyricsFor != app.current()->id) loadLyrics();
    if (app.lyricsLoading) { centered("Finding lyrics...", "", area.y, area.h); return; }
    if (app.lyrics.empty()) { centered("No lyrics for this song", "", area.y, area.h); return; }
    const float left = 22, width = W_ - 44, size = 21, gap = 18;
    int count = (int)app.lyrics.size();
    if (!app.lyricsMeasured) {
        for (auto& line : app.lyrics) {
            line.h = gfx::textHeight(line.text, width, size, Bold);
            line.plainH = gfx::textHeight(line.text, width, 16, Semi);
        }
        for (int k = 6000; k < 8000; k++) springs.erase(k);
        app.lyricsMeasured = true;
    }

    if (!app.lyricsSynced) {
        // No timing for this song: lyrics to read and scroll.
        gfx::pushFade(area, 24, 50);
        float y = beginScroll(area, app.lyricsScroll) + 16;
        for (auto& line : app.lyrics) {
            if (y + line.plainH >= area.y && y <= area.bottom())
                gfx::text(line.text, Rect{left, y, width, line.plainH + 2}, 16, Semi, white(0.86f), AlignLeft, true, true);
            y += line.plainH + 12;
        }
        endScroll(area, app.lyricsScroll, y + 50);
        gfx::popFade();
        return;
    }

    double at = audio::position() + 0.12;
    int now = -1;
    for (int i = 0; i < count; i++) { if (app.lyrics[i].at <= at) now = i; else break; }
    // Between lines (a long break in the singing) nothing is lit.
    bool resting = now >= 0 && app.lyrics[now].end > 0 && at > app.lyrics[now].end + 0.5 && now + 1 < count && app.lyrics[now + 1].at - at > 2;
    bool intro = now < 0 && app.lyrics[0].at > 3;
    float offset = intro ? -40.0f : 0;
    for (int i = 0; i < now; i++) offset += app.lyrics[i].h + gap;
    if (nowMs() > app.lyricsNudgeUntil) app.lyricsNudge = 0;
    float target = offset - area.h * 0.26f + app.lyricsNudge;

    gfx::pushFade(area, 40, 80);
    gfx::pushClip(area);
    clipOn = true;
    clipRect = area;
    if (intro || resting) {
        // Three dots breathing while the music plays on its own.
        float dotsY = area.y + (intro ? -24.0f : offset + app.lyrics[now].h + 2) - glide(5999, target, 240, 1);
        if (intro) for (int i = 0; i < 3; i++) {
            float wave = 0.5f + 0.5f * sinf((float)(nowMs() % 2400) / 2400.0f * 6.2831853f - i * 0.9f);
            gfx::circle(left + 7 + i * 20, dotsY, 4.5f + wave * 1.5f, white(0.35f + 0.5f * wave));
        }
    }
    float contentY = 0;
    for (int i = 0; i < count; i++) {
        LyricLine& line = app.lyrics[i];
        int ahead = std::max(0, std::min(8, i - std::max(now, 0)));
        // Each line follows the scroll a touch later than the one above it.
        float follow = glide(6000 + i, target, 70 + 230.0f / (1 + 0.3f * ahead), 1);
        bool current = i == now && !resting;
        float lit = glide(7000 + i, current ? 1.0f : 0.0f, 260, 1);
        float y = area.y + contentY - follow;
        contentY += line.h + gap;
        if (y + line.h < area.y - 20 || y > area.bottom() + 20) continue;
        Rect r{left, y, width, line.h + 4};
        double seekAt = line.at;
        click(r, [seekAt] { app.lyricsNudgeUntil = 0; seekTo(seekAt); });
        int away = std::min(4, abs(i - std::max(now, 0)));
        float dim = 0.36f - 0.045f * away;
        float scale = 0.95f + 0.05f * lit;
        gfx::pushTransform(scale, left * (1 - scale), (y + line.h / 2) * (1 - scale));
        if (current && !line.words.empty()) {
            int sung = 0, wordChars = 0;
            float part = 0;
            for (auto& word : line.words) {
                if (word.at > at) break;
                sung = word.from;
                wordChars = word.count;
                part = (float)std::max(0.0, std::min(1.0, (at - word.at) / std::max(0.06, word.end - word.at)));
            }
            gfx::textSung(line.text, r, size, Bold, white(0.38f + 0.1f * lit), white(), sung, wordChars, part);
        } else gfx::text(line.text, r, size, Bold, white(dim + (1 - dim) * lit), AlignLeft, true, true);
        gfx::popTransform();
    }
    clipOn = false;
    gfx::popClip();
    gfx::popFade();
    // Lyrics move with the music: a frame per screen refresh while they're showing.
    if (app.playing) animating = true;
}

// ---------------------------------------------------------------- Pages (artist, album, playlist, lists)

void backButton(std::vector<Page>& stack, Rect r) {
    std::vector<Page>* where = &stack;
    iconButton(r, Icon::Back, 18, white(0.08f), white(), 10, [where] { if (!where->empty()) where->pop_back(); });
}

void playFromPage(const Page& page, int index, bool shuffled) {
    std::vector<Item> songs = page.list.songs;
    if (songs.empty()) return;
    app.last = page.list.head;
    app.hasLast = true;
    if (shuffled) {
        for (size_t i = songs.size(); i > 1; i--) std::swap(songs[i - 1], songs[rand() % i]);
        index = 0;
    }
    playSongs(songs, index, page.list.head.title);
    app.shuffle = shuffled;
}

void drawListPage(std::vector<Page>& stack, Page& page) {
    Rect area{0, TOP, W_, PANEL};
    float y = beginScroll(area, page.scroll);
    const Item& head = page.list.head;
    bool album = head.kind == Kind::Album;
    backButton(stack, Rect{14, y + 2, 34, 76});
    cover(head.art, Rect{60, y + 2, 76, 76}, 10, 240);
    gfx::text(head.title.empty() ? page.title : head.title, Rect{148, y, W_ - 148 - 14, 24}, 17, Bold, white());
    string sub = album ? head.subtitle() : (head.note.empty() ? "Playlist" : head.note);
    gfx::text(sub, Rect{148, y + 23, W_ - 148 - 14, 18}, 12.5f, Regular, white(0.7f));
    Page* p = &page;
    float x = 148;
    x += pill(x, y + 44, 34, "Play", (int)Icon::Play, true, 13, 14, 10, [p] { playFromPage(*p, 0, false); }) + 6;
    x += pill(x, y + 44, 34, "Shuffle", (int)Icon::Shuffle, false, 13, 14, 10, [p] { playFromPage(*p, 0, true); }) + 6;
    if (!page.loading && page.list.ok && page.id != "LM" && app.session.signedIn() && x + 34 <= W_ - 10) {
        bool saved = isSaved(head);
        iconButton(Rect{x, y + 44, 34, 34}, saved ? Icon::HeartFull : Icon::Heart, 16, white(0.1f), saved ? theme.accent : white(), 10, [p] { toggleSaved(p->list.head); });
    }
    y += 90;
    const Item* playing = app.current();
    for (int i = 0; i < (int)page.list.songs.size(); i++) {
        const Item& song = page.list.songs[i];
        float h = album ? 42.0f : 52.0f;
        if (y + h >= area.y && y <= area.bottom()) {
            bool current = playing && playing->id == song.id;
            string time = song.durationMs ? formatTime(song.durationMs / 1000.0) : "";
            if (album) {
                Rect r{10, y, W_ - 20, 42};
                bool over = click(r, [p, i] { playFromPage(*p, i, false); });
                withMenu(songMenu(song));
                if (current) gfx::fill(r, theme.accent.alpha(0.16f), 10);
                else if (over) gfx::fill(r, white(0.07f), 10);
                gfx::text(std::to_string(i + 1), Rect{16, y, 26, 42}, 13, Regular, white(0.65f), AlignCenter);
                gfx::text(song.title, Rect{52, y, W_ - 52 - 70, 42}, 14, Semi, current ? theme.soft : white());
                gfx::text(time, Rect{W_ - 20 - 50, y, 50, 42}, 12, Regular, white(0.65f), AlignRight);
            } else itemRow(Rect{10, y + 1, W_ - 20, 50}, song, song.artistText(), time, current, [p, i] { playFromPage(*p, i, false); },
                           songMenu(song, song.setVideoId.empty() ? "" : page.id));
        }
        y += h;
    }
    if (page.loading) { gfx::text("Loading...", Rect{0, y + 20, W_, 30}, 14, Semi, white(0.7f), AlignCenter); y += 70; }
    else if (page.failed || page.list.songs.empty()) { gfx::text("Nothing here", Rect{0, y + 20, W_, 30}, 14, Semi, white(0.7f), AlignCenter); y += 70; }
    // A long playlist: more songs as you reach the end.
    if (!page.loading && !page.loadingMore && !page.list.continuation.empty() && y - area.bottom() < 200) {
        page.loadingMore = true;
        string continuation = page.list.continuation, id = page.id;
        std::vector<Page>* where = &stack;
        async<std::pair<std::vector<Item>, string>>([continuation] {
            string c = continuation;
            auto songs = yt::playlistMore(c);
            return std::make_pair(songs, c);
        }, [where, id](std::pair<std::vector<Item>, string> more) {
            for (auto& pg : *where) {
                if (pg.id != id || pg.type != Page::List) continue;
                pg.loadingMore = false;
                pg.list.continuation = more.first.empty() ? string() : more.second;
                for (auto& song : more.first) pg.list.songs.push_back(song);
            }
            invalidate();
        });
    }
    endScroll(area, page.scroll, y + 10);
}

void drawArtistPage(std::vector<Page>& stack, Page& page) {
    Rect area{0, TOP, W_, PANEL};
    gfx::pushFade(area, 0, 40);
    float y = beginScroll(area, page.scroll);
    const ArtistPage& a = page.artist;
    backButton(stack, Rect{14, y + 2, 34, 76});
    cover(a.artist.art, Rect{60, y + 4, 72, 72}, 36, 240);
    gfx::text(a.artist.title.empty() ? page.title : a.artist.title, Rect{144, y, W_ - 144 - 14, 24}, 18, Bold, white());
    gfx::text(a.artist.note.empty() ? "Artist" : "Artist \xC2\xB7 " + a.artist.note, Rect{144, y + 24, W_ - 144 - 14, 18}, 12.5f, Regular, white(0.7f));
    Page* p = &page;
    std::vector<Page>* where = &stack;
    if (!page.loading && a.ok) {
        float x = 144;
        if (!a.shuffleId.empty()) x += pill(x, y + 46, 34, "Shuffle", (int)Icon::Shuffle, true, 13, 12, 10, [p] {
            Item list; list.kind = Kind::Playlist; list.id = p->artist.shuffleId; list.title = p->artist.artist.title; list.art = p->artist.artist.art;
            playItem(list, true);
        }) + 6;
        if (!a.radioId.empty()) x += pill(x, y + 46, 34, "Radio", (int)Icon::Radio, a.shuffleId.empty(), 13, 12, 10, [p] {
            Item list; list.kind = Kind::Playlist; list.id = p->artist.radioId; list.title = p->artist.artist.title + " radio"; list.art = p->artist.artist.art;
            playItem(list, false);
        }) + 6;
        if (app.session.signedIn() && x + 96 < W_) pill(x, y + 46, 34, a.subscribed ? "Subscribed" : "Subscribe", -1, false, 13, 12, 10, [p] {
            p->artist.subscribed = !p->artist.subscribed;
            string channel = p->artist.channelId;
            bool on = p->artist.subscribed;
            background([channel, on] { yt::subscribe(channel, on); });
        });
    }
    y += 92;
    if (page.loading) { gfx::text("Loading...", Rect{0, y + 40, W_, 30}, 14, Semi, white(0.7f), AlignCenter); y += 100; }
    else if (page.failed) { gfx::text("Couldn't open this artist", Rect{0, y + 40, W_, 30}, 14, Semi, white(0.7f), AlignCenter); y += 100; }
    for (int s = 0; s < (int)a.sections.size(); s++) {
        const Section& section = a.sections[s];
        gfx::text(upper(section.title), Rect{16, y + 4, 280, 22}, 12, Bold, white(0.65f), AlignLeft, false, false, 0.8f);
        if (!section.moreId.empty()) {
            Rect more{W_ - 16 - 62, y + 2, 62, 26};
            string id = section.moreId, params = section.moreParams, title = section.title;
            bool over = click(more, [where, id, params, title] {
                if (startsWith(id, "VL")) openPage(*where, Page::List, id.substr(2), title);
                else openPage(*where, Page::Items, id, title, params);
            });
            gfx::text("See all", more, 12.5f, Bold, over ? white() : theme.soft, AlignCenter);
        }
        y += 30;
        bool songs = !section.items.empty() && section.items[0].kind == Kind::Song;
        if (songs) {
            for (int i = 0; i < (int)section.items.size() && i < 5; i++, y += 48) {
                if (y + 48 < area.y || y > area.bottom()) continue;
                const Item& song = section.items[i];
                itemRow(Rect{10, y, W_ - 20, 46}, song, song.album.name.empty() ? song.artistText() : song.album.name, "", app.current() && app.current()->id == song.id, [p, s, i] {
                    const Section& sec = p->artist.sections[s];
                    std::vector<Item> list;
                    for (auto& it : sec.items) if (it.kind == Kind::Song) list.push_back(it);
                    app.last = sec.items[i];
                    app.hasLast = true;
                    playSongs(list, i, p->artist.artist.title);
                }, songMenu(song));
            }
        } else {
            float x = 16;
            for (int i = 0; i < (int)section.items.size() && i < 3; i++, x += 124) {
                const Item& item = section.items[i];
                Rect tile{x, y, 112, 112 + 40};
                if (tile.bottom() < area.y || tile.y > area.bottom()) continue;
                Item copy = item;
                bool over = click(tile, [where, copy] { openItem(*where, copy); });
                cover(item.art, Rect{x, y, 112, 112}, item.kind == Kind::Artist ? 56 : 10, 240);
                if (over) gfx::fill(Rect{x, y, 112, 112}, white(0.1f), item.kind == Kind::Artist ? 56 : 10);
                gfx::text(item.title, Rect{x, y + 116, 112, 18}, 13, Bold, white());
                string sub = item.kind == Kind::Album ? item.year : item.kind == Kind::Song ? item.artistText() : item.note;
                gfx::text(sub, Rect{x, y + 133, 112, 16}, 12, Regular, white(0.65f));
            }
            y += 158;
        }
        y += 6;
    }
    endScroll(area, page.scroll, y + 30);
    gfx::popFade();
}

void drawItemsPage(std::vector<Page>& stack, Page& page) {
    backButton(stack, Rect{14, TOP + 2, 34, 34});
    gfx::text(page.title, Rect{60, TOP, W_ - 76, 38}, 16, Bold, white());
    Rect area{0, TOP + 44, W_, PANEL - 52};
    if (page.loading) { centered("Loading...", "", area.y, area.h); return; }
    if (page.items.empty()) { centered("Nothing here", "", area.y, area.h); return; }
    std::vector<Page>* where = &stack;
    Page* p = &page;
    float y = beginScroll(area, page.scroll);
    for (int i = 0; i < (int)page.items.size(); i++, y += 52) {
        if (y + 52 < area.y || y > area.bottom()) continue;
        const Item& item = page.items[i];
        itemRow(Rect{10, y + 1, W_ - 20, 50}, item, item.subtitle(), item.kind == Kind::Song && item.durationMs ? formatTime(item.durationMs / 1000.0) : "",
            app.current() && app.current()->id == item.id, [where, p, i] {
                Item copy = p->items[i];
                openItem(*where, copy);
            }, songMenu(item));
    }
    endScroll(area, page.scroll, y);
}

bool drawPages(std::vector<Page>& stack) {
    if (stack.empty()) return false;
    Page& page = stack.back();
    if (page.type == Page::Artist) drawArtistPage(stack, page);
    else if (page.type == Page::List) drawListPage(stack, page);
    else drawItemsPage(stack, page);
    return true;
}

// ---------------------------------------------------------------- Search

// One row of the home page: cards you can page through sideways.
void homeRow(int s, float& y, Rect area) {
    const Section& section = app.home[s];
    const float tile = 104, step = 114;
    int count = std::min((int)section.items.size(), 14);
    bool allSongs = true;
    for (int i = 0; i < count; i++) if (section.items[i].kind != Kind::Song) allSongs = false;
    float room = W_ - 32, most = std::max(0.0f, count * step - 10 - room);
    if (y + 190 < area.y || y > area.bottom()) { y += 190; return; }

    gfx::text(section.title, Rect{16, y, W_ - 32 - 104, 28}, 15, Bold, white());
    float bx = W_ - 16 - 26;
    if (most > 0) {
        bool canRight = app.homeX[s] < most - 1, canLeft = app.homeX[s] > 1;
        iconButton(Rect{bx, y + 1, 26, 26}, Icon::Chevron, 14, white(0.08f), white(canRight ? 0.95f : 0.3f), 8, [s, most, step] { app.homeX[s] = std::min(most, app.homeX[s] + step * 3); });
        bx -= 30;
        iconButton(Rect{bx, y + 1, 26, 26}, Icon::Back, 14, white(0.08f), white(canLeft ? 0.95f : 0.3f), 8, [s, step] { app.homeX[s] = std::max(0.0f, app.homeX[s] - step * 3); });
        bx -= 30;
    }
    if (allSongs && count > 1) iconButton(Rect{bx, y + 1, 26, 26}, Icon::Play, 12, theme.accent, theme.onAccent, 13, [s] {
        if (s >= (int)app.home.size()) return;
        app.last = app.home[s].items[0];
        app.hasLast = true;
        playSongs(app.home[s].items, 0, app.home[s].title);
    });
    y += 34;

    Rect strip{0, y, W_, tile + 42};
    Rect before = clipRect;
    gfx::pushClip(strip);
    clipRect = Rect{0, std::max(strip.y, area.y), W_, std::max(0.0f, std::min(strip.bottom(), area.bottom()) - std::max(strip.y, area.y))};
    float x = 16 - glide(2000 + s, app.homeX[s]);
    for (int i = 0; i < count; i++, x += step) {
        if (x + tile < 0 || x > W_) continue;
        const Item& item = section.items[i];
        Rect card{x, y, tile, tile + 40};
        bool over = click(card, [s, i, allSongs] {
            if (s >= (int)app.home.size() || i >= (int)app.home[s].items.size()) return;
            Item copy = app.home[s].items[i];
            if (copy.kind == Kind::Song && allSongs) {
                app.last = copy;
                app.hasLast = true;
                playSongs(app.home[s].items, i, app.home[s].title);
            } else openItem(app.searchPages, copy);
        });
        withMenu(songMenu(item));
        float radius = item.kind == Kind::Artist ? tile / 2 : 10;
        cover(item.art, Rect{x, y, tile, tile}, radius, 240);
        if (over) gfx::fill(Rect{x, y, tile, tile}, white(0.12f), radius);
        if (app.current() && app.current()->id == item.id) gfx::outline(Rect{x, y, tile, tile}, theme.accent, radius, 2.5f);
        gfx::text(item.title, Rect{x, y + tile + 5, tile, 18}, 13, Bold, white());
        string sub = item.kind == Kind::Song ? item.artistText() : item.kind == Kind::Album ? item.artistText() : item.kind == Kind::Artist ? "Artist" : item.note;
        gfx::text(sub, Rect{x, y + tile + 22, tile, 16}, 12, Regular, white(0.65f));
    }
    clipRect = before;
    gfx::popClip();
    y += tile + 42 + 14;
}

void drawHome(Rect area) {
    if (!app.homeLoaded) {
        loadHome();
        centered("Loading your music...", "", area.y, area.h);
        return;
    }
    if (app.home.empty()) {
        centered("Couldn't load home", "Check your connection, then search for something.", area.y, area.h - 50);
        float w = gfx::textWidth("Try again", 13, Bold) + 28;
        pill((W_ - w) / 2, area.y + area.h / 2 + 30, 34, "Try again", -1, false, 13, 14, 10, [] { app.homeLoaded = false; });
        return;
    }
    if (app.homeX.size() < app.home.size()) app.homeX.resize(app.home.size(), 0);
    float y = beginScroll(area, app.homeScroll) + 4;
    // Just clicked into the search box: what you searched lately.
    if (app.searchFocus && !app.recent.empty()) {
        float cx = 12;
        for (auto& recent : app.recent) {
            float w = gfx::textWidth(recent, 13, Semi) + 24;
            if (cx + w > W_ - 12) break;
            string q = recent;
            cx += chip(cx, y, recent, false, [q] { app.query = W(q); runSearch(); }) + 6;
        }
        y += 40;
    }
    for (int s = 0; s < (int)app.home.size(); s++) homeRow(s, y, area);
    if (app.homeLoading) { gfx::text("Loading more...", Rect{0, y, W_, 30}, 13, Semi, white(0.65f), AlignCenter); y += 40; }
    else if (!app.homeMore.empty() && y - area.bottom() < 240) loadHome(true);
    endScroll(area, app.homeScroll, y);
}

void drawSuggestions(Rect area) {
    float y = area.y + 2;
    int shown = 0;
    for (auto& query : app.suggestQueries) {
        if (y + 40 > area.bottom() || shown >= 4) break;
        Rect r{10, y, W_ - 20, 40};
        string q = query;
        bool over = click(r, [q] { app.query = W(q); runSearch(); });
        if (over) gfx::fill(r, white(0.07f), 10);
        gfx::icon(Icon::Search, Rect{22, y + 11, 18, 18}, white(0.7f));
        gfx::text(query, Rect{52, y, W_ - 52 - 20, 40}, 14, Semi, white());
        y += 40;
        shown++;
    }
    for (int i = 0; i < (int)app.suggestItems.size(); i++) {
        if (y + 52 > area.bottom()) break;
        const Item& item = app.suggestItems[i];
        itemRow(Rect{10, y + 1, W_ - 20, 50}, item, item.subtitle(true), "", false, [i] {
            if (i >= (int)app.suggestItems.size()) return;
            Item copy = app.suggestItems[i];
            openItem(app.searchPages, copy);
        }, songMenu(item));
        y += 52;
        shown++;
    }
    if (shown == 0) centered("Press Enter to search", "", area.y, area.h);
}

void drawSearch() {
    if (drawPages(app.searchPages)) return;
    Rect field{10, TOP, W_ - 20, 42};
    click(field, [] { app.searchFocus = true; });
    gfx::fill(field, white(0.1f), 12);
    float focus = glide(70, app.searchFocus ? 1.0f : 0.0f, 600, 1);
    if (focus > 0.02f) gfx::outline(field, theme.accent.alpha(focus), 12, 2);
    gfx::icon(Icon::Search, Rect{22, TOP + 12, 18, 18}, white(0.8f));
    string query = U(app.query), typed = trim(query);
    Rect textRect{50, TOP, W_ - 50 - 50, 42};
    if (query.empty()) gfx::text("Songs, artists, albums", textRect, 14, Regular, white(0.6f));
    else gfx::text(query, textRect, 14, Semi, white());
    if (app.searchFocus && (nowMs() / 530) % 2 == 0) {
        float caret = 50 + std::min(textRect.w, gfx::textWidth(query, 14, Semi)) + 1;
        gfx::fill(Rect{caret, TOP + 12, 1.5f, 18}, white());
    }
    if (!query.empty()) iconButton(Rect{W_ - 10 - 36, TOP + 7, 28, 28}, Icon::Close, 14, white(0), white(0.75f), 8, [] {
        app.query.clear();
        app.results.clear();
        app.searched.clear();
        app.searching = false;
        app.searchFocus = true;
    });

    // Nothing typed: home. Typing: suggestions. Searched: results.
    static Arrival arrival;
    int mode = typed.empty() ? 0 : (app.searchFocus && typed != app.searched) ? 1 : 2;
    float in = arrive(arrival, std::to_string(mode));
    Rect below{0, TOP + 48, W_, PANEL - 56};
    if (in < 1) { gfx::pushOpacity(below, in); gfx::pushTransform(1, 0, (1 - in) * 10); }
    if (mode == 0) drawHome(Rect{0, TOP + 50, W_, PANEL - 58});
    else if (mode == 1) drawSuggestions(Rect{0, TOP + 50, W_, PANEL - 58});
    else {
        const char* names[5] = {"All", "Songs", "Artists", "Albums", "Playlists"};
        const SearchFilter filters[5] = {SearchFilter::All, SearchFilter::Songs, SearchFilter::Artists, SearchFilter::Albums, SearchFilter::Playlists};
        float x = 10;
        for (int i = 0; i < 5; i++) {
            SearchFilter f = filters[i];
            x += chip(x, TOP + 50, names[i], app.filter == f, [f] { if (app.filter != f) { app.filter = f; runSearch(); } }) + 6;
        }
        Rect area{0, TOP + 88, W_, PANEL - 96};
        if (app.searching) centered("Searching...", "", area.y, area.h);
        else if (app.results.empty()) centered("Nothing found", "Try other words.", area.y, area.h);
        else {
            float y = beginScroll(area, app.searchScroll);
            for (int s = 0; s < (int)app.results.size(); s++) {
                const Section& section = app.results[s];
                bool top = section.title == "Top result";
                if (!top && app.filter == SearchFilter::All) {
                    gfx::text(upper(section.title), Rect{16, y + 6, 300, 20}, 12, Bold, white(0.65f), AlignLeft, false, false, 0.8f);
                    y += 30;
                }
                for (int i = 0; i < (int)section.items.size(); i++) {
                    const Item& item = section.items[i];
                    bool card = top && i == 0 && item.kind != Kind::Song;
                    float h = card ? 70.0f : 52.0f;
                    if (y + h >= area.y && y <= area.bottom()) {
                        auto open = [s, i] {
                            if (s >= (int)app.results.size() || i >= (int)app.results[s].items.size()) return;
                            Item copy = app.results[s].items[i];
                            app.searchFocus = false;
                            openItem(app.searchPages, copy);
                        };
                        if (card) {
                            Rect r{10, y, W_ - 20, 64};
                            bool over = click(r, open);
                            gfx::fill(r, white(over ? 0.12f : 0.07f), 12);
                            cover(item.art, Rect{20, y + 10, 44, 44}, item.kind == Kind::Artist ? 22 : 6, 120);
                            gfx::text(item.title, Rect{76, y + 11, W_ - 76 - 50, 22}, 15, Bold, white());
                            gfx::text(item.subtitle(true), Rect{76, y + 33, W_ - 76 - 50, 18}, 12.5f, Regular, white(0.7f));
                            gfx::icon(Icon::Chevron, Rect{W_ - 10 - 30, y + 23, 18, 18}, white(0.7f));
                        } else itemRow(Rect{10, y + 1, W_ - 20, 50}, item, item.subtitle(true), "", app.current() && app.current()->id == item.id, open, songMenu(item));
                    }
                    y += h;
                }
            }
            endScroll(area, app.searchScroll, y + 6);
        }
    }
    if (in < 1) { gfx::popTransform(); gfx::popOpacity(); }
}

// ---------------------------------------------------------------- Library

void drawLibrary() {
    if (drawPages(app.libraryPages)) return;
    if (!app.session.signedIn()) {
        centered("Your library needs an account", "Sign in to see your likes, playlists, albums and artists.", TOP, PANEL - 70);
        float w = gfx::textWidth("Sign in", 14, Bold) + 36;
        pill((W_ - w) / 2, TOP + PANEL / 2 + 20, 42, "Sign in", -1, true, 14, 18, 14, [] { startSignIn(); });
        return;
    }
    struct Tile { const char* title; const char* sub; Icon icon; bool accent; };
    const Tile tiles[2] = {{"Liked songs", "Your likes", Icon::HeartFull, true}, {"History", "Played lately", Icon::Clock, false}};
    for (int i = 0; i < 2; i++) {
        Rect r{10 + i * 203.0f, TOP, 197, 60};
        bool over = click(r, [i] {
            if (i == 0) openPage(app.libraryPages, Page::List, "LM", "Liked songs");
            else openPage(app.libraryPages, Page::Items, "history", "History");
        });
        gfx::fill(r, tiles[i].accent ? theme.accent.alpha(over ? 0.26f : 0.18f) : white(over ? 0.14f : 0.08f), 12);
        Rect badge{r.x + 12, r.y + 12, 36, 36};
        gfx::fill(badge, tiles[i].accent ? theme.accent : white(0.12f), 10);
        gfx::icon(tiles[i].icon, badge.inset(9), tiles[i].accent ? theme.onAccent : white());
        gfx::text(tiles[i].title, Rect{r.x + 58, r.y + 11, 130, 20}, 14, Bold, white());
        gfx::text(tiles[i].sub, Rect{r.x + 58, r.y + 31, 130, 18}, 12, Regular, white(0.7f));
    }
    if (app.naming) {
        // A name for the new playlist, typed right here.
        Rect field{10, TOP + 66, W_ - 20 - 122, 34};
        gfx::fill(field, white(0.1f), 10);
        gfx::outline(field, theme.accent, 10, 2);
        string name = U(app.newName);
        if (name.empty()) gfx::text("Playlist name", Rect{22, field.y, field.w - 20, 34}, 14, Regular, white(0.6f));
        else gfx::text(name, Rect{22, field.y, field.w - 20, 34}, 14, Semi, white());
        if ((nowMs() / 530) % 2 == 0) gfx::fill(Rect{22 + std::min(field.w - 24, gfx::textWidth(name, 14, Semi)) + 1, field.y + 8, 1.5f, 18}, white());
        float cw = pill(field.right() + 6, TOP + 66, 34, "Create", -1, true, 13, 14, 10, [] {
            createPlaylist(U(app.newName));
            app.naming = false;
            app.newName.clear();
        });
        iconButton(Rect{field.right() + 12 + cw, TOP + 66, 34, 34}, Icon::Close, 14, white(0.1f), white(), 10, [] { app.naming = false; app.newName.clear(); });
    } else {
        const char* names[3] = {"Playlists", "Albums", "Artists"};
        float x = 10;
        for (int i = 0; i < 3; i++)
            x += chip(x, TOP + 68, names[i], app.libraryTab == i, [i] { app.libraryTab = i; app.libraryScroll = 0; loadLibrary(i); }) + 6;
        if (app.libraryTab == 0) {
            float w = gfx::textWidth("New", 13, Bold) + 28 + 20;
            pill(W_ - 10 - w, TOP + 66, 34, "New", (int)Icon::AddList, false, 13, 12, 10, [] { app.naming = true; app.newName.clear(); });
        }
    }

    int which = app.libraryTab;
    Rect area{0, TOP + 106, W_, PANEL - 114};
    if (!app.libraryLoaded[which]) { loadLibrary(which); centered("Loading...", "", area.y, area.h); return; }
    if (app.library[which].empty()) { centered("Nothing saved here yet", "", area.y, area.h); return; }
    float y = beginScroll(area, app.libraryScroll);
    for (int i = 0; i < (int)app.library[which].size(); i++, y += 52) {
        if (y + 52 < area.y || y > area.bottom()) continue;
        const Item& item = app.library[which][i];
        itemRow(Rect{10, y + 1, W_ - 20, 50}, item, item.subtitle(), "", false, [which, i] {
            if (i >= (int)app.library[which].size()) return;
            Item copy = app.library[which][i];
            openItem(app.libraryPages, copy);
        });
    }
    endScroll(area, app.libraryScroll, y);
}

// ---------------------------------------------------------------- Account

void drawAccount() {
    Rect card{10, TOP, W_ - 20, 58};
    gfx::fill(card, white(0.07f), 12);
    Rect face{22, TOP + 11, 36, 36};
    gfx::circle(face.x + 18, face.y + 18, 18, hex(0x7b8ef0));
    bool signedInNow = app.session.signedIn();
    bool drawn = !app.session.photo.empty() && gfx::image(artAt(app.session.photo, 96), face, 18);
    if (!drawn) {
        if (app.session.name.empty()) gfx::icon(Icon::Person, face.inset(8), hex(0x0d1640));
        else gfx::text(upper(app.session.name.substr(0, 1)), face, 16, Bold, hex(0x0d1640), AlignCenter);
    }
    if (signedInNow) {
        gfx::text(app.session.name.empty() ? "Signed in" : app.session.name, Rect{70, TOP + 10, 220, 20}, 14, Bold, white());
        gfx::text(app.session.email.empty() ? "YouTube Music" : app.session.email, Rect{70, TOP + 30, 220, 18}, 12.5f, Regular, white(0.7f));
        Rect out{W_ - 20 - 80, TOP + 12, 80, 34};
        bool over = click(out, [] { signOut(); });
        if (over) gfx::fill(out, white(0.08f), 10);
        gfx::text("Sign out", out, 13, Bold, theme.soft, AlignCenter);
    } else {
        gfx::text("No account", Rect{70, TOP + 10, 200, 20}, 14, Bold, white());
        gfx::text("Likes and playlists need one", Rect{70, TOP + 30, 220, 18}, 12.5f, Regular, white(0.7f));
        float w = gfx::textWidth("Sign in", 13, Bold) + 28;
        pill(W_ - 20 - w, TOP + 12, 34, "Sign in", -1, true, 13, 14, 10, [] { startSignIn(); });
    }

    float y = TOP + 66;
    gfx::text("Audio quality", Rect{16, y, 200, 40}, 14, Semi, white());
    float saverW = gfx::textWidth("Saver", 13, Bold) + 24, highW = gfx::textWidth("High", 13, Bold) + 24;
    chip(W_ - 16 - saverW, y + 5, "Saver", app.settings.saver, [] { app.settings.saver = true; saveState(); });
    chip(W_ - 16 - saverW - 6 - highW, y + 5, "High", !app.settings.saver, [] { app.settings.saver = false; saveState(); });
    y += 42;
    toggle(y, "Even volume across songs", app.settings.evenVolume, [] { app.settings.evenVolume = !app.settings.evenVolume; applyVolume(); saveState(); });
    y += 42;
    toggle(y, "Keep playing similar songs", app.settings.autoplay, [] { app.settings.autoplay = !app.settings.autoplay; saveState(); });
    y += 42;
    toggle(y, "Stay on top of other windows", app.settings.onTop, [] { app.settings.onTop = !app.settings.onTop; applyOnTop(); saveState(); });
    y += 42;
    toggle(y, "Start with Windows", app.settings.startWithWindows, [] { setStartWithWindows(!app.settings.startWithWindows); });

    float foot = TOP + PANEL - 44;
    gfx::text("WaveMini " WAVEMINI_VERSION, Rect{16, foot, 200, 36}, 12.5f, Regular, white(0.65f));
    float w = gfx::textWidth("Check for updates", 12.5f, Bold) + 20;
    Rect update{W_ - 16 - w, foot + 3, w, 30};
    bool over = click(update, [] { checkForUpdates(); });
    gfx::fill(update, white(over ? 0.16f : 0.08f), 8);
    gfx::text("Check for updates", update, 12.5f, Bold, white(), AlignCenter);
}

// ---------------------------------------------------------------- The song menu

void menuRow(float x, float& y, float w, Icon icon, const string& text, const string& note, bool chevron, std::function<void()> fn) {
    Rect r{x + 6, y, w - 12, 40};
    bool over = click(r, std::move(fn));
    if (over) gfx::fill(r, white(0.09f), 10);
    gfx::icon(icon, Rect{r.x + 10, y + 11, 18, 18}, white());
    float noteW = note.empty() ? 0 : std::min(110.0f, gfx::textWidth(note, 12.5f, Regular) + 4);
    float right = r.right() - 10 - (chevron ? 20 : 0);
    gfx::text(text, Rect{r.x + 40, y, right - noteW - (r.x + 40) - 6, 40}, 14, Semi, white());
    if (!note.empty()) gfx::text(note, Rect{right - noteW, y, noteW, 40}, 12.5f, Regular, white(0.65f), AlignRight);
    if (chevron) gfx::icon(Icon::Chevron, Rect{r.right() - 24, y + 12, 16, 16}, white(0.65f));
    y += 40;
}

void drawMenu(float w, float h) {
    const Item* song = app.current();
    if (!song) { app.menuOpen = false; return; }
    click(Rect{0, 0, w, h}, [] { app.menuOpen = false; });
    gfx::fill(Rect{0, 0, w, h}, Color{0, 0, 0, 0.4f});
    const float mw = 268, x = W_ - 8 - mw, top = 38;
    bool signedInNow = app.session.signedIn();
    Item copy = *song;
    float height = app.menuPage == 1 ? std::min(h - top - 8, 320.0f)
        : 12 + 40 * (3 + (copy.album.id.empty() ? 0 : 1) + (signedInNow ? 1 : 0)) + 13 + 40 * 3;
    height = std::min(height, h - top - 6);
    Rect card{x, top, mw, height};
    click(card, [] {});
    gfx::fill(card, mix(theme.bg, theme.accent, 0.13f), 14);
    gfx::outline(card, white(0.09f), 14, 1);
    float y = top + 6;

    if (app.menuPage == 1) {
        menuRow(x, y, mw, Icon::Back, "Add to playlist", "", false, [] { app.menuPage = 0; });
        Rect area{x, y, mw, card.bottom() - y - 6};
        if (!app.libraryLoaded[0]) { loadLibrary(0); gfx::text("Loading...", area, 13, Semi, white(0.7f), AlignCenter); return; }
        if (app.library[0].empty()) { gfx::text("No playlists yet", area, 13, Semi, white(0.7f), AlignCenter); return; }
        float ry = beginScroll(area, app.menuScroll);
        for (int i = 0; i < (int)app.library[0].size(); i++, ry += 44) {
            if (ry + 44 < area.y || ry > area.bottom()) continue;
            const Item& list = app.library[0][i];
            string id = list.id, title = list.title, videoId = copy.id;
            itemRow(Rect{x + 6, ry, mw - 12, 42}, list, "", "", false, [id, title, videoId] {
                app.menuOpen = false;
                async<bool>([id, videoId] { return yt::addToPlaylist(id, videoId); }, [title](bool ok) { showToast(ok ? "Added to " + title : "Couldn't add it"); });
            });
        }
        endScroll(area, app.menuScroll, ry);
        return;
    }

    string artist = copy.artists.empty() ? "" : copy.artists[0].name;
    menuRow(x, y, mw, Icon::Person, "View artist", artist, false, [copy] {
        app.menuOpen = false;
        if (copy.artists.empty()) return;
        app.settings.tab = Tab::Search;
        if (!copy.artists[0].id.empty()) openPage(app.searchPages, Page::Artist, copy.artists[0].id, copy.artists[0].name);
        else { app.query = W(copy.artists[0].name); app.searchPages.clear(); runSearch(); }
    });
    if (!copy.album.id.empty()) menuRow(x, y, mw, Icon::Disc, "View album", copy.album.name, false, [copy] {
        app.menuOpen = false;
        app.settings.tab = Tab::Search;
        openPage(app.searchPages, Page::List, copy.album.id, copy.album.name);
    });
    menuRow(x, y, mw, Icon::Radio, "Start radio", "", false, [copy] { app.menuOpen = false; playRadio(copy); });
    if (signedInNow) menuRow(x, y, mw, Icon::AddList, "Add to playlist", "", true, [] { app.menuPage = 1; app.menuScroll = 0; loadLibrary(0); });
    menuRow(x, y, mw, Icon::Link, "Copy song link", "", false, [copy] {
        app.menuOpen = false;
        copyText("https://music.youtube.com/watch?v=" + copy.id);
        showToast("Link copied");
    });
    gfx::fill(Rect{x + 10, y + 6, mw - 20, 1}, white(0.1f));
    y += 13;

    gfx::icon(Icon::Volume, Rect{x + 16, y + 11, 18, 18}, white());
    slider(Rect{x + 48, y, mw - 48 - 22, 40}, (float)app.settings.volume, [](float v) { app.settings.volume = v; applyVolume(); }, [] { saveState(); });
    y += 40;

    gfx::icon(Icon::Speed, Rect{x + 16, y + 11, 18, 18}, white());
    const double rates[4] = {0.75, 1, 1.25, 1.5};
    const char* rateNames[4] = {"0.75\xC3\x97", "1\xC3\x97", "1.25\xC3\x97", "1.5\xC3\x97"};
    float cw = (mw - 48 - 16 - 12) / 4;
    for (int i = 0; i < 4; i++) {
        Rect r{x + 48 + i * (cw + 4), y + 5, cw, 30};
        bool on = fabs(app.settings.rate - rates[i]) < 0.01;
        double rate = rates[i];
        bool over = click(r, [rate] { app.settings.rate = rate; audio::setRate(rate); saveState(); });
        gfx::fill(r, on ? theme.accent : white(over ? 0.16f : 0.08f), 8);
        gfx::text(rateNames[i], r, 12.5f, Bold, on ? theme.onAccent : white(), AlignCenter);
    }
    y += 40;

    string left = "Off";
    if (app.sleepAt) left = std::to_string((int)std::max<long long>(1, (app.sleepAt - nowMs() + 59999) / 60000)) + " min";
    menuRow(x, y, mw, Icon::Moon, "Sleep timer", left, false, [] {
        // Off, 15, 30, 45, 60 minutes, and round again.
        int next = app.sleepMinutes >= 60 ? 0 : app.sleepMinutes + 15;
        setSleep(next);
    });
}

// ---------------------------------------------------------------- The menu on any song

void drawContext(float w, float h) {
    click(Rect{0, 0, w, h}, [] { app.ctxOpen = false; });
    gfx::fill(Rect{0, 0, w, h}, Color{0, 0, 0, 0.32f});
    Item song = app.ctxItem;
    bool account = app.session.signedIn();
    bool removable = account && !app.ctxPlaylist.empty() && !song.setVideoId.empty();
    const float mw = 232;
    int rows = 3 + (account ? 1 : 0) + (song.artists.empty() ? 0 : 1) + (song.album.id.empty() ? 0 : 1) + (removable ? 1 : 0);
    float height = app.ctxPage == 1 ? std::min(h - 16, 300.0f) : 12 + 40.0f * rows;
    float x = std::max(8.0f, std::min(app.ctxX, w - mw - 8)), top = std::max(8.0f, std::min(app.ctxY, h - height - 8));
    Rect card{x, top, mw, height};
    click(card, [] {});
    gfx::fill(card, mix(theme.bg, theme.accent, 0.13f), 14);
    gfx::outline(card, white(0.09f), 14, 1);
    float y = top + 6;
    if (app.ctxPage == 1) {
        menuRow(x, y, mw, Icon::Back, "Add to playlist", "", false, [] { app.ctxPage = 0; });
        Rect area{x, y, mw, card.bottom() - y - 6};
        if (!app.libraryLoaded[0]) { loadLibrary(0); gfx::text("Loading...", area, 13, Semi, white(0.7f), AlignCenter); return; }
        if (app.library[0].empty()) { gfx::text("No playlists yet", area, 13, Semi, white(0.7f), AlignCenter); return; }
        float ry = beginScroll(area, app.ctxScroll);
        for (int i = 0; i < (int)app.library[0].size(); i++, ry += 44) {
            if (ry + 44 < area.y || ry > area.bottom()) continue;
            const Item& list = app.library[0][i];
            string id = list.id, title = list.title, videoId = song.id;
            itemRow(Rect{x + 6, ry, mw - 12, 42}, list, "", "", false, [id, title, videoId] {
                app.ctxOpen = false;
                async<bool>([id, videoId] { return yt::addToPlaylist(id, videoId); }, [title](bool ok) { showToast(ok ? "Added to " + title : "Couldn't add it"); });
            });
        }
        endScroll(area, app.ctxScroll, ry);
        return;
    }
    menuRow(x, y, mw, Icon::Play, "Play next", "", false, [song] { app.ctxOpen = false; playNext(song); });
    menuRow(x, y, mw, Icon::Queue, "Add to queue", "", false, [song] { app.ctxOpen = false; addToQueue(song); });
    menuRow(x, y, mw, Icon::Radio, "Start radio", "", false, [song] { app.ctxOpen = false; playRadio(song); });
    if (account) menuRow(x, y, mw, Icon::AddList, "Add to playlist", "", true, [] { app.ctxPage = 1; app.ctxScroll = 0; loadLibrary(0); });
    if (!song.artists.empty()) menuRow(x, y, mw, Icon::Person, "View artist", "", false, [song] {
        app.ctxOpen = false;
        app.settings.tab = Tab::Search;
        if (!song.artists[0].id.empty()) openPage(app.searchPages, Page::Artist, song.artists[0].id, song.artists[0].name);
        else { app.query = W(song.artists[0].name); app.searchPages.clear(); runSearch(); }
    });
    if (!song.album.id.empty()) menuRow(x, y, mw, Icon::Disc, "View album", "", false, [song] {
        app.ctxOpen = false;
        app.settings.tab = Tab::Search;
        openPage(app.searchPages, Page::List, song.album.id, song.album.name);
    });
    if (removable) {
        string playlist = app.ctxPlaylist;
        menuRow(x, y, mw, Icon::Close, "Remove from playlist", "", false, [song, playlist] { app.ctxOpen = false; removeFromPlaylist(playlist, song); });
    }
}

// ---------------------------------------------------------------- Sign-in

void waveLines(float top) {
    const float alphas[3] = {0.07f, 0.05f, 0.035f};
    for (int i = 0; i < 3; i++) {
        float base = top + 70 + i * 30, px = -10, py = base;
        for (float x = -6; x <= W_ + 4; x += 4) {
            float y = base - 20 * sinf((x + 10) / 210 * 6.2831853f);
            gfx::line(px, py, x, y, white(alphas[i]), 2);
            px = x; py = y;
        }
    }
}

void drawSignIn(float w, float h) {
    gfx::fill(Rect{0, 0, w, h}, hex(0x36618E));
    waveLines(-16);
    waveLines(390);
    windowButtons(false, white(0.12f));
    gfx::icon(Icon::Wave, Rect{150, 56, 120, 120}, white());
    float waveW = gfx::textWidth("Wave", 30, 800), miniW = gfx::textWidth("Mini", 30, 800);
    float start = (W_ - waveW - miniW) / 2;
    gfx::text("Wave", Rect{start, 192, waveW + 2, 44}, 30, 800, white());
    gfx::text("Mini", Rect{start + waveW, 192, miniW + 4, 44}, 30, 800, hex(0xcfe0ff));
    gfx::text("All of YouTube Music, no ads,\nin a window this small.", Rect{32, 240, W_ - 64, 48}, 15, Regular, white(0.85f), AlignCenter, true, true);

    Rect primary{28, 300, W_ - 56, 52};
    bool over = click(primary, [] { startSignIn(); });
    gfx::fill(primary, over ? hex(0xeef3fa) : white(), 16);
    string text = app.signingIn ? "Waiting for Google..." : "Sign in with Google";
    float tw = gfx::textWidth(text, 15, Bold);
    float at = (W_ - tw - 30) / 2;
    gfx::icon(Icon::Person, Rect{at, 316, 20, 20}, hex(0x36618E));
    gfx::text(text, Rect{at + 30, 300, tw + 4, 52}, 15, Bold, hex(0x0a1120));

    Rect guest{28, 362, W_ - 56, 48};
    bool overGuest = click(guest, [] {
        Session s;
        s.guest = true;
        s.name = "";
        signedIn(s);
    });
    gfx::fill(guest, white(overGuest ? 0.2f : 0.14f), 16);
    gfx::outline(guest, white(0.22f), 16, 1);
    gfx::text("Try it without an account", guest, 15, Semi, white(), AlignCenter);
    gfx::text("Google's sign-in opens right over this window.\nWithout an account you can still search and play.", Rect{24, h - 64, W_ - 48, 44}, 12, Regular, white(0.8f), AlignCenter, true, true);
}

void drawTucked() {
    string art = playerArt();
    Rect circle{0, 0, 56, 56};
    if (gfx::image(artAt(art, 120), circle.inset(-6), 0, 1, true))
        gfx::fill(circle, Color{0, 0, 0, 0.35f});
    else
        gfx::fill(circle, hex(0x36618E));
    gfx::icon(Icon::Wave, Rect{8, 8, 40, 40}, white());
}

void drawToast(float h) {
    if (app.toast.empty()) return;
    float w = std::min(W_ - 40, gfx::textWidth(app.toast, 13, Semi) + 32);
    Rect r{(W_ - w) / 2, h - 52, w, 36};
    gfx::fill(r, Color{0.05f, 0.05f, 0.06f, 0.92f}, 18);
    gfx::outline(r, white(0.12f), 18, 1);
    gfx::text(app.toast, r, 13, Semi, white(), AlignCenter);
}

}   // namespace

// ---------------------------------------------------------------- Drawing and input

static Size2 fullSize() {
    if (!app.hasSession) return Size2{W_, PLAYER + TABS + PANEL};
    if (app.settings.tab == Tab::None) return Size2{W_, PLAYER + TABS};
    return Size2{W_, PLAYER + TABS + PANEL};
}

Size2 wantedSize() {
    if (app.tucked) return Size2{56, 56};
    return fullSize();
}

void uiFrame(float seconds) { frameDt = seconds; }
bool uiAnimating() { return animating; }

static void drawPanel(Tab tab) {
    switch (tab) {
        case Tab::UpNext: drawUpNext(); break;
        case Tab::Lyrics: drawLyrics(); break;
        case Tab::Search: drawSearch(); break;
        case Tab::Library: drawLibrary(); break;
        case Tab::Account: drawAccount(); break;
        default: break;
    }
}

// The card at its full layout; [shownH] is how much of it the window shows right now.
static void drawCard(float w, float h, float shownH, bool usable) {
    if (!app.hasSession) { live = usable; drawSignIn(w, h); return; }
    backdrop(w, PLAYER + TABS + PANEL);
    // Under an open menu nothing takes clicks.
    inputOn = !app.menuOpen && !app.ctxOpen;
    live = usable;
    drawPlayer();
    drawTabs();
    if (shownH > PLAYER + TABS + 1) {
        // While the panel folds away it's still the one that was open.
        static Tab shownTab = Tab::UpNext;
        bool folding = app.settings.tab == Tab::None;
        if (!folding) shownTab = app.settings.tab;
        const std::vector<Page>* pages = shownTab == Tab::Search ? &app.searchPages : shownTab == Tab::Library ? &app.libraryPages : nullptr;
        string key = std::to_string((int)shownTab) + ":" + (pages && !pages->empty() ? std::to_string(pages->size()) + pages->back().id : "");
        static Arrival arrival;
        float in = arrive(arrival, key);
        Rect panel{0, TOP, w, PANEL};
        gfx::pushClip(panel);
        if (in < 1) { gfx::pushOpacity(panel, in); gfx::pushTransform(1, 0, (1 - in) * 12); }
        live = usable && !folding;
        drawPanel(shownTab);
        live = usable;
        if (in < 1) { gfx::popTransform(); gfx::popOpacity(); }
        gfx::popClip();
    }
    float menu = glide(50, app.menuOpen ? 1.0f : 0.0f, 520, 1);
    if (menu > 0.01f && app.current()) {
        inputOn = true;
        live = usable && app.menuOpen;
        gfx::pushOpacity(Rect{0, 0, w, h}, std::min(1.0f, menu));
        gfx::pushTransform(1, 0, (1 - menu) * -10);
        drawMenu(w, h);
        gfx::popTransform();
        gfx::popOpacity();
        live = usable;
        // The menu's own button stays live on top of the scrim.
        if (app.menuOpen) windowButtons(true, Color{0, 0, 0, 0.22f});
    }
    float context = glide(52, app.ctxOpen ? 1.0f : 0.0f, 520, 1);
    if (context > 0.01f) {
        inputOn = true;
        live = usable && app.ctxOpen;
        gfx::pushOpacity(Rect{0, 0, w, h}, std::min(1.0f, context));
        gfx::pushTransform(1, 0, (1 - context) * -8);
        drawContext(w, h);
        gfx::popTransform();
        gfx::popOpacity();
        live = usable;
    }
    inputOn = true;
    float toast = glide(51, app.toast.empty() ? 0.0f : 1.0f, 520, 1);
    if (toast > 0.01f && !app.toast.empty()) {
        gfx::pushOpacity(Rect{0, h - 70, w, 70}, std::min(1.0f, toast));
        gfx::pushTransform(1, 0, (1 - toast) * 10);
        drawToast(h);
        gfx::popTransform();
        gfx::popOpacity();
    }
}

void draw(float w, float h, float shownW, float shownH) {
    hits.clear();
    scrolls.clear();
    clipOn = false;
    inputOn = true;
    live = true;
    animating = false;
    pickTheme();
    Size2 full = fullSize();
    // How far along the card is between its full shape (0) and the tucked button (1).
    float tuck = std::max(0.0f, std::min(1.0f, (full.w - shownW) / (full.w - 56)));
    float radius = 14 + (std::min(shownW, shownH) / 2 - 14) * tuck * tuck;
    Rect all{0, 0, shownW, shownH};
    gfx::pushClip(all, radius);
    if (tuck < 0.995f) {
        bool fading = tuck > 0.004f;
        if (fading) gfx::pushOpacity(all, 1 - tuck);
        drawCard(full.w, full.h, shownH, !app.tucked && tuck < 0.03f);
        if (fading) gfx::popOpacity();
    }
    if (tuck > 0.004f) {
        live = false;
        float size = std::min(shownW, shownH);
        gfx::pushOpacity(all, tuck);
        gfx::pushTransform(size / 56, (shownW - size) / 2, (shownH - size) / 2);
        drawTucked();
        gfx::popTransform();
        gfx::popOpacity();
    } else gfx::outline(all, white(0.08f), radius, 1);
    live = true;
    gfx::popClip();
    frameDt = 0;
}

static Hit* hitAt(float x, float y) {
    for (size_t i = hits.size(); i > 0; i--) if (hits[i - 1].r.has(x, y)) return &hits[i - 1];
    return nullptr;
}

bool uiTakes(float x, float y) {
    if (app.tucked) return false;
    return hitAt(x, y) != nullptr;
}

void uiMouseDown(float x, float y) {
    downX = x;
    downY = y;
    mouseX = x;
    mouseY = y;
    pendingDrag = nullptr;
    Hit* hit = hitAt(x, y);
    if (!hit || !hit->drag) return;
    // Something that's also clickable only starts dragging once the pointer has moved.
    if (hit->click) { pendingDrag = hit->drag; return; }
    dragging = hit->drag;
    dragging(x, y, false);
    invalidate();
}

void uiMouseMove(float x, float y) {
    mouseIn = true;
    mouseX = x;
    mouseY = y;
    if (pendingDrag && (fabsf(x - downX) > 5 || fabsf(y - downY) > 5)) {
        dragging = pendingDrag;
        pendingDrag = nullptr;
        dragging(downX, downY, false);
    }
    if (dragging) dragging(x, y, false);
    invalidate();
}

void uiMouseUp(float x, float y) {
    pendingDrag = nullptr;
    if (dragging) {
        auto fn = dragging;
        dragging = nullptr;
        fn(x, y, true);
        invalidate();
        return;
    }
    Hit* hit = hitAt(x, y);
    if (hit && hit->click && hit->r.has(downX, downY)) {
        app.searchFocus = false;
        auto fn = hit->click;      // the click may redraw and replace the hits
        fn();
    } else app.searchFocus = false;
    invalidate();
}

void uiRightClick(float x, float y) {
    for (size_t i = hits.size(); i > 0; i--) {
        Hit& hit = hits[i - 1];
        if (!hit.r.has(x, y)) continue;
        if (hit.context) { auto fn = hit.context; fn(x, y); invalidate(); }
        return;
    }
}

void uiMouseLeave() {
    mouseIn = false;
    invalidate();
}

void uiWheel(float x, float y, float delta) {
    if (app.settings.tab == Tab::Lyrics && app.lyricsSynced && y > TOP && !app.menuOpen && !app.ctxOpen && !app.tucked) {
        app.lyricsNudge = std::max(-3000.0f, std::min(3000.0f, app.lyricsNudge - delta * 70));
        app.lyricsNudgeUntil = nowMs() + 3500;
        invalidate();
        return;
    }
    for (size_t i = scrolls.size(); i > 0; i--) {
        ScrollArea& area = scrolls[i - 1];
        if (!area.r.has(x, y)) continue;
        *area.value = std::max(0.0f, std::min(area.max, *area.value - delta * 56));
        invalidate();
        return;
    }
}

// What's being typed into right now, if anything.
static wstring* typingInto() {
    if (app.naming && app.settings.tab == Tab::Library && app.libraryPages.empty()) return &app.newName;
    if (app.searchFocus && app.settings.tab == Tab::Search && app.searchPages.empty()) return &app.query;
    return nullptr;
}

bool uiChar(wchar_t c) {
    wstring* text = typingInto();
    if (!text) return false;
    bool naming = text == &app.newName;
    if (c == L'\r') {
        if (naming) { createPlaylist(U(app.newName)); app.naming = false; app.newName.clear(); }
        else runSearch();
        invalidate();
        return true;
    }
    if (c == 27) { if (naming) { app.naming = false; app.newName.clear(); } else app.searchFocus = false; invalidate(); return true; }
    if (c == 8) { if (!text->empty()) text->pop_back(); }
    else if (c < 32) return false;
    else if (text->size() < (naming ? 80u : 200u)) text->push_back(c);
    if (!naming) app.suggestDue = nowMs() + 170;
    invalidate();
    return true;
}

bool uiKey(WPARAM key, bool ctrl) {
    if (wstring* text = typingInto()) {
        if (ctrl && key == 'V') {
            if (OpenClipboard(nullptr)) {
                HANDLE data = GetClipboardData(CF_UNICODETEXT);
                if (data) {
                    const wchar_t* from = (const wchar_t*)GlobalLock(data);
                    if (from) {
                        for (const wchar_t* p = from; *p && text->size() < 200; p++) if (*p >= 32) text->push_back(*p);
                        GlobalUnlock(data);
                    }
                }
                CloseClipboard();
            }
            if (text == &app.query) app.suggestDue = nowMs() + 170;
            invalidate();
            return true;
        }
        return false;
    }
    switch (key) {
        case VK_SPACE: togglePlay(); invalidate(); return true;
        case VK_LEFT: seekTo(audio::position() - 5); return true;
        case VK_RIGHT: seekTo(audio::position() + 5); return true;
        case VK_ESCAPE:
            if (app.ctxOpen) { app.ctxOpen = false; invalidate(); return true; }
            if (app.menuOpen) { app.menuOpen = false; invalidate(); return true; }
            return false;
        default: return false;
    }
}
