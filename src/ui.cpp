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
};
struct ScrollArea { Rect r; float* value; float max; };

std::vector<Hit> hits;
std::vector<ScrollArea> scrolls;
float mouseX = -1, mouseY = -1, downX = -1, downY = -1;
bool mouseIn = false, inputOn = true, clipOn = false;
Rect clipRect;
std::function<void(float, float, bool)> dragging;
double seekPreview = -1;

Rect visible(Rect r) {
    if (!clipOn) return r;
    float x1 = std::max(r.x, clipRect.x), y1 = std::max(r.y, clipRect.y);
    float x2 = std::min(r.right(), clipRect.right()), y2 = std::min(r.bottom(), clipRect.bottom());
    return Rect{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
}

bool hover(Rect r) { return inputOn && mouseIn && !dragging && visible(r).has(mouseX, mouseY); }

bool click(Rect r, std::function<void()> fn) {
    if (!inputOn) return false;
    Rect v = visible(r);
    if (v.w <= 0 || v.h <= 0) return false;
    hits.push_back({v, std::move(fn), nullptr});
    return hover(r);
}

void dragArea(Rect r, std::function<void(float, float, bool)> fn) {
    if (inputOn) hits.push_back({visible(r), nullptr, std::move(fn)});
}

// Starts a scrolling list; returns the y its content starts at.
float beginScroll(Rect r, float& value) {
    gfx::pushClip(r);
    clipOn = true;
    clipRect = r;
    if (inputOn) scrolls.push_back({r, &value, 0});
    return r.y - value;
}

void endScroll(Rect r, float& value, float contentEnd) {
    float height = contentEnd - (r.y - value);
    float max = std::max(0.0f, height - r.h);
    if (value > max) value = max;
    if (value < 0) value = 0;
    if (inputOn && !scrolls.empty()) scrolls.back().max = max;
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

// A row: cover, title and a grey line, and an optional bit of text on the right.
void itemRow(Rect r, const Item& item, const string& sub, const string& right, bool current, std::function<void()> fn) {
    bool over = click(r, std::move(fn));
    if (current) gfx::fill(r, theme.accent.alpha(0.16f), 10);
    else if (over) gfx::fill(r, white(0.07f), 10);
    float art = std::min(40.0f, r.h - 10);
    Rect coverRect{r.x + 6, r.y + (r.h - art) / 2, art, art};
    cover(item.art, coverRect, item.kind == Kind::Artist ? art / 2 : 6, 120);
    float rightW = right.empty() ? 0 : gfx::textWidth(right, 12, Regular) + 10;
    float tx = coverRect.right() + 12, tw = r.right() - tx - rightW - 8;
    Color titleColor = current ? theme.soft : white();
    if (sub.empty()) gfx::text(item.title, Rect{tx, r.y, tw, r.h}, 14, Bold, titleColor);
    else {
        gfx::text(item.title, Rect{tx, r.y + r.h / 2 - 19, tw, 20}, 14, Bold, titleColor);
        gfx::text(sub, Rect{tx, r.y + r.h / 2 + 1, tw, 18}, 12.5f, Regular, white(0.7f));
    }
    if (!right.empty()) gfx::text(right, Rect{r.right() - rightW - 6, r.y, rightW, r.h}, 12, Regular, white(0.65f), AlignRight);
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
    theme = Theme{hex(0xa8c7ff), hex(0x0a2a5c), hex(0x0a1120), hex(0x1d3266), hex(0x13224a), hex(0xcfe0ff)};
    Color c;
    string art = playerArt();
    if (art.empty() || !gfx::imageColor(artAt(art, 360), c)) return;
    float h, s, l;
    toHsl(c, h, s, l);
    float sat = s < 0.08f ? 0.06f : std::max(0.35f, std::min(0.62f, s * 1.25f));
    theme.accent = fromHsl(h, sat, 0.73f);
    theme.onAccent = fromHsl(h, sat * 0.85f, 0.15f);
    theme.bg = fromHsl(h + 6, sat * 0.95f, 0.055f);
    theme.glowA = fromHsl(h + 14, std::min(0.7f, sat * 1.05f), 0.29f);
    theme.glowB = fromHsl(h - 10, std::min(0.75f, sat * 1.2f), 0.17f);
    theme.soft = fromHsl(h, std::min(0.75f, sat * 1.2f), 0.84f);
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
        label(app.last.title, left, 31, textW, 16, Bold, white());
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
    if (!app.error.empty()) {
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
        gfx::wave(bar, length > 0 ? (float)(at / length) : 0, phase, app.playing ? 2.4f : 0.0f, theme.accent);
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
    float unit = total / (active == Tab::None ? 5.0f : 6.4f), x = 10;
    for (int i = 0; i < 5; i++) {
        const Entry& e = entries[i];
        bool on = e.tab == active;
        Rect r{x, PLAYER + 8, on ? unit * 2.4f : unit, 40};
        Tab tab = e.tab;
        bool over = click(r, [tab] {
            app.settings.tab = app.settings.tab == tab ? Tab::None : tab;
            app.searchFocus = false;
            if (app.settings.tab == Tab::Lyrics) loadLyrics();
            if (app.settings.tab == Tab::Library) loadLibrary(app.libraryTab);
            saveState();
        });
        gfx::fillCorners(r, on ? theme.accent : white(over ? 0.15f : 0.08f), i == 0 ? 12 : 4, i == 4 ? 12 : 4, i == 4 ? 12 : 4, i == 0 ? 12 : 4);
        Color fg = on ? theme.onAccent : white();
        float labelW = on ? gfx::textWidth(e.name, 14, Bold) + 8 : 0;
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
        if (on) gfx::text(e.name, Rect{at + iconW + 8, r.y, labelW, 40}, 14, Bold, fg);
        x += r.w + gap;
    }
}

// ---------------------------------------------------------------- Up next

void drawUpNext() {
    gfx::text("Playing from", Rect{16, TOP, 90, 40}, 13, Regular, white(0.7f));
    float fromW = gfx::textWidth("Playing from ", 13, Regular);
    gfx::text(app.source.empty() ? "nothing yet" : app.source, Rect{16 + fromW, TOP, 320 - fromW, 40}, 13, Bold, white());
    bool shuffle = app.shuffle, repeat = app.repeat > 0;
    iconButton(Rect{W_ - 16 - 72, TOP + 3, 34, 34}, Icon::Shuffle, 18, shuffle ? theme.accent.alpha(0.18f) : white(0), shuffle ? theme.accent : white(0.8f), 10, [] { toggleShuffle(); });
    iconButton(Rect{W_ - 16 - 34, TOP + 3, 34, 34}, app.repeat == 2 ? Icon::RepeatOne : Icon::Repeat, 18, repeat ? theme.accent.alpha(0.18f) : white(0), repeat ? theme.accent : white(0.8f), 10, [] { cycleRepeat(); });

    Rect area{0, TOP + 40, W_, PANEL - 48};
    int first = app.index + 1;
    if (first >= (int)app.queue.size()) {
        centered(app.current() ? (app.fetchingMore ? "Finding more songs..." : "Nothing up next") : "Nothing playing",
                 app.current() ? "Songs you queue up show here." : "Pick something in Search or your Library.", area.y, area.h);
        return;
    }
    float y = beginScroll(area, app.queueScroll);
    for (int i = first; i < (int)app.queue.size(); i++, y += 56) {
        if (y + 56 < area.y || y > area.bottom()) continue;
        const Item& song = app.queue[i];
        itemRow(Rect{10, y + 2, W_ - 20 - 36, 52}, song, song.artistText(), song.durationMs ? formatTime(song.durationMs / 1000.0) : "", false, [i] { playIndex(i); });
        iconButton(Rect{W_ - 10 - 34, y + 12, 32, 32}, Icon::Close, 16, white(0), white(0.7f), 8, [i] { removeFromQueue(i); });
    }
    endScroll(area, app.queueScroll, y);
}

// ---------------------------------------------------------------- Lyrics

void drawLyrics() {
    Rect area{0, TOP, W_, PANEL};
    if (!app.current()) { centered("No song playing", "Lyrics show here while a song plays.", area.y, area.h); return; }
    if (app.lyricsFor != app.current()->id) loadLyrics();
    if (app.lyricsLoading) { centered("Finding lyrics...", "", area.y, area.h); return; }
    if (app.lyrics.empty()) { centered("No lyrics for this song", "", area.y, area.h); return; }
    const float width = W_ - 40, gap = 14;
    if (!app.lyricsMeasured) {
        for (auto& line : app.lyrics) {
            line.small = gfx::textHeight(line.text, width, 15, Semi);
            line.big = gfx::textHeight(line.text, width, 20, Bold);
        }
        app.lyricsMeasured = true;
    }
    int now = -1;
    if (app.lyricsSynced) {
        double at = audio::position() + 0.25;
        for (int i = 0; i < (int)app.lyrics.size(); i++) if (app.lyrics[i].at <= at) now = i; else break;
        // Keep the line being sung a third of the way down.
        float offset = 0;
        for (int i = 0; i < now; i++) offset += app.lyrics[i].small + gap;
        float want = std::max(0.0f, offset - area.h * 0.3f);
        app.lyricsScroll += (want - app.lyricsScroll) * 0.18f;
        if (fabsf(want - app.lyricsScroll) < 0.5f) app.lyricsScroll = want;
    }
    gfx::pushFade(area, 28, 60);
    float y = beginScroll(area, app.lyricsScroll) + 20;
    for (int i = 0; i < (int)app.lyrics.size(); i++) {
        const LyricLine& line = app.lyrics[i];
        bool current = i == now;
        float h = current ? line.big : line.small;
        if (y + h >= area.y && y <= area.bottom()) {
            Rect r{20, y, width, h + 2};
            if (app.lyricsSynced) { double at = line.at; click(r, [at] { seekTo(at); }); }
            gfx::text(line.text, r, current ? 20 : 15, current ? Bold : Semi, current ? white() : white(app.lyricsSynced ? 0.5f : 0.82f), AlignLeft, true, true);
        }
        y += h + gap;
    }
    float end = y + 70;
    if (app.lyricsSynced) {
        // The list follows the song; the wheel doesn't fight it.
        clipOn = false;
        gfx::popClip();
        if (inputOn && !scrolls.empty()) scrolls.pop_back();
    } else endScroll(area, app.lyricsScroll, end);
    gfx::popFade();
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
    pill(x, y + 44, 34, "Shuffle", (int)Icon::Shuffle, false, 13, 14, 10, [p] { playFromPage(*p, 0, true); });
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
                if (current) gfx::fill(r, theme.accent.alpha(0.16f), 10);
                else if (over) gfx::fill(r, white(0.07f), 10);
                gfx::text(std::to_string(i + 1), Rect{16, y, 26, 42}, 13, Regular, white(0.65f), AlignCenter);
                gfx::text(song.title, Rect{52, y, W_ - 52 - 70, 42}, 14, Semi, current ? theme.soft : white());
                gfx::text(time, Rect{W_ - 20 - 50, y, 50, 42}, 12, Regular, white(0.65f), AlignRight);
            } else itemRow(Rect{10, y + 1, W_ - 20, 50}, song, song.artistText(), time, current, [p, i] { playFromPage(*p, i, false); });
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
                });
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
            });
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

void drawSearch() {
    if (drawPages(app.searchPages)) return;
    Rect field{10, TOP, W_ - 20, 42};
    click(field, [] { app.searchFocus = true; });
    gfx::fill(field, white(0.1f), 12);
    if (app.searchFocus) gfx::outline(field, theme.accent, 12, 2);
    gfx::icon(Icon::Search, Rect{22, TOP + 12, 18, 18}, white(0.8f));
    string query = U(app.query);
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

    const char* names[5] = {"All", "Songs", "Artists", "Albums", "Playlists"};
    const SearchFilter filters[5] = {SearchFilter::All, SearchFilter::Songs, SearchFilter::Artists, SearchFilter::Albums, SearchFilter::Playlists};
    float x = 10;
    for (int i = 0; i < 5; i++) {
        SearchFilter f = filters[i];
        x += chip(x, TOP + 50, names[i], app.filter == f, [f] { if (app.filter != f) { app.filter = f; runSearch(); } }) + 6;
    }

    Rect area{0, TOP + 88, W_, PANEL - 96};
    if (app.searching) { centered("Searching...", "", area.y, area.h); return; }
    if (app.results.empty()) {
        if (!app.searched.empty()) { centered("Nothing found", "Try other words.", area.y, area.h); return; }
        if (app.recent.empty()) { centered("Search YouTube Music", "Type a song, an artist or an album, then press Enter.", area.y, area.h); return; }
        gfx::text("RECENT", Rect{16, area.y + 2, 200, 20}, 12, Bold, white(0.65f), AlignLeft, false, false, 0.8f);
        float cx = 12, cy = area.y + 28;
        for (auto& recent : app.recent) {
            float w = gfx::textWidth(recent, 13, Semi) + 24;
            if (cx + w > W_ - 12) { cx = 12; cy += 36; }
            string q = recent;
            cx += chip(cx, cy, recent, false, [q] { app.query = W(q); runSearch(); }) + 6;
        }
        return;
    }
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
                } else itemRow(Rect{10, y + 1, W_ - 20, 50}, item, item.subtitle(true), "", app.current() && app.current()->id == item.id, open);
            }
            y += h;
        }
    }
    endScroll(area, app.searchScroll, y + 6);
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
    const char* names[3] = {"Playlists", "Albums", "Artists"};
    float x = 10;
    for (int i = 0; i < 3; i++)
        x += chip(x, TOP + 68, names[i], app.libraryTab == i, [i] { app.libraryTab = i; app.libraryScroll = 0; loadLibrary(i); }) + 6;

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
    gfx::circle(28, 28, 28, theme.accent);
    if (!art.empty()) gfx::image(artAt(art, 120), Rect{3, 3, 50, 50}, 25);
    else gfx::circle(28, 28, 25, hex(0x36618E));
    gfx::circle(28, 28, 25, Color{0, 0, 0, art.empty() ? 0.0f : 0.35f});
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

Size2 wantedSize() {
    if (app.tucked) return Size2{56, 56};
    if (!app.hasSession) return Size2{W_, PLAYER + TABS + PANEL};
    if (app.settings.tab == Tab::None) return Size2{W_, PLAYER + TABS};
    return Size2{W_, PLAYER + TABS + PANEL};
}

void draw(float w, float h) {
    hits.clear();
    scrolls.clear();
    clipOn = false;
    inputOn = true;
    pickTheme();
    if (app.tucked) {
        gfx::pushClip(Rect{0, 0, w, h}, 28);
        drawTucked();
        gfx::popClip();
        return;
    }
    gfx::pushClip(Rect{0, 0, w, h}, 14);
    if (!app.hasSession) {
        drawSignIn(w, h);
    } else {
        backdrop(w, h);
        // Under an open menu nothing takes clicks.
        inputOn = !app.menuOpen;
        drawPlayer();
        drawTabs();
        if (h > PLAYER + TABS + 1) {
            switch (app.settings.tab) {
                case Tab::UpNext: drawUpNext(); break;
                case Tab::Lyrics: drawLyrics(); break;
                case Tab::Search: drawSearch(); break;
                case Tab::Library: drawLibrary(); break;
                case Tab::Account: drawAccount(); break;
                default: break;
            }
        }
        if (app.menuOpen) {
            inputOn = true;
            drawMenu(w, h);
            // The menu's own button stays live on top of the scrim.
            windowButtons(true, Color{0, 0, 0, 0.22f});
        }
        drawToast(h);
    }
    gfx::outline(Rect{0, 0, w, h}, white(0.08f), 14, 1);
    gfx::popClip();
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
    Hit* hit = hitAt(x, y);
    if (hit && hit->drag) {
        dragging = hit->drag;
        dragging(x, y, false);
        invalidate();
    }
}

void uiMouseMove(float x, float y) {
    mouseIn = true;
    mouseX = x;
    mouseY = y;
    if (dragging) dragging(x, y, false);
    invalidate();
}

void uiMouseUp(float x, float y) {
    if (dragging) {
        auto fn = dragging;
        dragging = nullptr;
        fn(x, y, true);
        invalidate();
        return;
    }
    Hit* hit = hitAt(x, y);
    if (hit && hit->click && hit->r.has(downX, downY)) {
        bool focused = app.searchFocus;
        app.searchFocus = false;
        auto fn = hit->click;      // the click may redraw and replace the hits
        fn();
        (void)focused;
    } else app.searchFocus = false;
    invalidate();
}

void uiMouseLeave() {
    mouseIn = false;
    invalidate();
}

void uiWheel(float x, float y, float delta) {
    for (size_t i = scrolls.size(); i > 0; i--) {
        ScrollArea& area = scrolls[i - 1];
        if (!area.r.has(x, y)) continue;
        *area.value = std::max(0.0f, std::min(area.max, *area.value - delta * 56));
        invalidate();
        return;
    }
}

bool uiChar(wchar_t c) {
    if (!app.searchFocus || app.settings.tab != Tab::Search || !app.searchPages.empty()) return false;
    if (c == L'\r') { runSearch(); return true; }
    if (c == 8) { if (!app.query.empty()) app.query.pop_back(); invalidate(); return true; }
    if (c == 27) { app.searchFocus = false; invalidate(); return true; }
    if (c < 32) return false;
    if (app.query.size() < 200) app.query.push_back(c);
    invalidate();
    return true;
}

bool uiKey(WPARAM key, bool ctrl) {
    bool typing = app.searchFocus && app.settings.tab == Tab::Search && app.searchPages.empty();
    if (typing) {
        if (ctrl && key == 'V') {
            if (OpenClipboard(nullptr)) {
                HANDLE data = GetClipboardData(CF_UNICODETEXT);
                if (data) {
                    const wchar_t* text = (const wchar_t*)GlobalLock(data);
                    if (text) {
                        for (const wchar_t* p = text; *p && app.query.size() < 200; p++) if (*p >= 32) app.query.push_back(*p);
                        GlobalUnlock(data);
                    }
                }
                CloseClipboard();
            }
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
            if (app.menuOpen) { app.menuOpen = false; invalidate(); return true; }
            return false;
        default: return false;
    }
}
