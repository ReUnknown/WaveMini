// Drawing: Direct2D in software (no graphics driver loaded into the app, which keeps memory low),
// DirectWrite text, icons from path strings and a small cache of cover images.
#pragma once
#include "core.h"

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color alpha(float to) const { return Color{r, g, b, to}; }
};
Color hex(unsigned rgb, float alpha = 1);
Color white(float alpha = 1);
Color mix(Color a, Color b, float t);
Color fromHsl(float h, float s, float l, float alpha = 1);
void toHsl(Color c, float& h, float& s, float& l);

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect inset(float by) const { return Rect{x + by, y + by, w - by * 2, h - by * 2}; }
};

enum class Icon {
    Queue, Lyrics, Search, Library, More, Minus, Close, Like, Liked, Prev, Next, Pause, Play, Shuffle, Repeat, RepeatOne,
    Back, Chevron, Heart, HeartFull, Person, Disc, Radio, AddList, Link, Volume, Speed, Moon, Clock, Wave, Check, Count
};

enum { AlignLeft = 0, AlignCenter = 1, AlignRight = 2 };

namespace gfx {
bool start();
// Draws into a 32-bit bitmap selected into [dc] ([pxW] x [pxH] pixels); coordinates are in
// design units, [scale] pixels each.
bool begin(HDC dc, int pxW, int pxH, float scale);
bool end();     // false: the target was lost and everything must be drawn again

void fill(Rect r, Color c, float radius = 0);
void fillCorners(Rect r, Color c, float tl, float tr, float br, float bl);
void outline(Rect r, Color c, float radius, float width);
void circle(float cx, float cy, float radius, Color c);
void line(float x1, float y1, float x2, float y2, Color c, float width);
// A soft glow: [c] at the center fading to nothing at the ellipse's edge.
void glow(float cx, float cy, float rx, float ry, Color c);
// The seek bar: a moving wave up to [progress], a faint straight line after it.
void wave(Rect r, float progress, float phase, float amplitude, Color c);

void text(const string& s, Rect r, float size, int weight, Color c, int align = AlignLeft, bool wrap = false, bool top = false, float spacing = 0);
float textWidth(const string& s, float size, int weight, float spacing = 0);
float textHeight(const string& s, float width, float size, int weight);
void icon(Icon which, Rect r, Color c);

// Draws the image at [url] (fetched and decoded on first use; false until it's there).
// [fadeFrom] < 1 fades it out to the right, starting at that fraction of its width.
bool image(const string& url, Rect r, float radius, float fadeFrom = 1);
// The image's overall color, once it has loaded.
bool imageColor(const string& url, Color& out);
// Called (on the window's thread) when an image arrives, so the window can draw again.
void onImage(std::function<void()> fn);
void forgetImages();

// Paints something costly once (at the origin, [r] in size) and reuses the picture until [key] changes.
void cached(const string& key, Rect r, const std::function<void()>& paintIt);

void pushClip(Rect r, float radius = 0);
void popClip();
// Everything drawn until popFade fades out over [top] and [bottom] units at the rect's edges.
void pushFade(Rect r, float top, float bottom);
void popFade();
void pushOpacity(Rect r, float opacity);
void popOpacity();

bool savePng(const wstring& path, const void* bgra, int w, int h);
}
