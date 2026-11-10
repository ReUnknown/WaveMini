#include "gfx.h"
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <shlwapi.h>

namespace {

ID2D1Factory* d2d;
IDWriteFactory* dwrite;
IWICImagingFactory* wic;
ID2D1DCRenderTarget* target;
ID2D1RenderTarget* rt;          // where drawing goes: the window's target, or an offscreen picture being cached
ID2D1Bitmap* cachedPicture;
string cachedKey;
ID2D1SolidColorBrush* brush;
ID2D1StrokeStyle* roundStroke;
const wchar_t* fontFamily = L"Segoe UI";
std::map<int, IDWriteTextFormat*> formats;
ID2D1PathGeometry* icons[(int)Icon::Count];
std::function<void()> imageArrived;
bool lost;

template <typename T> void drop(T*& p) { if (p) { p->Release(); p = nullptr; } }

D2D1_COLOR_F cf(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
D2D1_RECT_F rf(Rect r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
ID2D1SolidColorBrush* paint(Color c) { brush->SetColor(cf(c)); return brush; }

// ---------------------------------------------------------------- Icons

struct IconDef { const char* path; char mode; float stroke; float box; };   // mode: s = stroked, f = filled

// Paths in SVG's language, plus "O cx cy r" for a circle.
const IconDef iconDefs[(int)Icon::Count] = {
    /* Queue */     {"M4 6h12M4 11h12M4 16h7M18.5 18.5V11l3 1O16.5 18.5 2", 's', 2.2f, 24},
    /* Lyrics */    {"M4 5h16v11H9l-5 4zM8 9h8M8 12h5", 's', 2.2f, 24},
    /* Search */    {"O11 11 6M20 20l-4.5-4.5", 's', 2.4f, 24},
    /* Library */   {"M5 4v16M10 4v16M14.5 5l4.5 15", 's', 2.2f, 24},
    /* More */      {"O12 5 2.2O12 12 2.2O12 19 2.2", 'f', 0, 24},
    /* Minus */     {"M6 12h12", 's', 2.4f, 24},
    /* Close */     {"M6 6l12 12M18 6L6 18", 's', 2.4f, 24},
    /* Like */      {"M7 11v9H4v-9zM7 11l4-7c1.5 0 2.5 1 2.5 2.5V10h5a2 2 0 0 1 2 2.3l-1.2 6A2 2 0 0 1 17.3 20H7", 's', 2, 24},
    /* Liked */     {"M7 11v9H4v-9zM7 11l4-7c1.5 0 2.5 1 2.5 2.5V10h5a2 2 0 0 1 2 2.3l-1.2 6A2 2 0 0 1 17.3 20H7z", 'f', 0, 24},
    /* Prev */      {"M5 4h2.4v16H5zM19 5L9 12l10 7z", 'f', 0, 24},
    /* Next */      {"M16.6 4H19v16h-2.4zM5 5l10 7-10 7z", 'f', 0, 24},
    /* Pause */     {"M6 5h4v14H6zM14 5h4v14h-4z", 'f', 0, 24},
    /* Play */      {"M7 4l13 8-13 8z", 'f', 0, 24},
    /* Shuffle */   {"M4 7h3l10 10h3M4 17h3l3-3M14 10l3-3h3M18 4l3 3-3 3M18 14l3 3-3 3", 's', 2, 24},
    /* Repeat */    {"M5 11V9a2 2 0 0 1 2-2h11M15 4l3 3-3 3M19 13v2a2 2 0 0 1-2 2H6M9 20l-3-3 3-3", 's', 2, 24},
    /* RepeatOne */ {"M5 11V9a2 2 0 0 1 2-2h11M15 4l3 3-3 3M19 13v2a2 2 0 0 1-2 2H6M9 20l-3-3 3-3M11.2 10.6l1.3-.8v4.6", 's', 2, 24},
    /* Back */      {"M15 5l-7 7 7 7", 's', 2.4f, 24},
    /* Chevron */   {"M9 5l7 7-7 7", 's', 2.2f, 24},
    /* Heart */     {"M12 20C12 20 5 15.6 5 10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z", 's', 2.2f, 24},
    /* HeartFull */ {"M12 20C12 20 5 15.6 5 10a4 4 0 0 1 7-2.6A4 4 0 0 1 19 10c0 5.6-7 10-7 10z", 'f', 0, 24},
    /* Person */    {"O12 8 4M4 21c1.5-4 4.5-6 8-6s6.5 2 8 6", 's', 2.2f, 24},
    /* Disc */      {"O12 12 8O12 12 2.5", 's', 2, 24},
    /* Radio */     {"O12 12 2M8 8a5.5 5.5 0 0 0 0 8M16 8a5.5 5.5 0 0 1 0 8M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14", 's', 2, 24},
    /* AddList */   {"M4 6h12M4 11h12M4 16h7M18 14v7M14.5 17.5h7", 's', 2, 24},
    /* Link */      {"M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1", 's', 2, 24},
    /* Volume */    {"M4 9h4l5-4v14l-5-4H4zM16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11", 's', 2, 24},
    /* Speed */     {"M5 18a8 8 0 1 1 14 0M12 13l4-4", 's', 2, 24},
    /* Moon */      {"M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z", 's', 2, 24},
    /* Clock */     {"O12 12 8M12 8v4l3 2", 's', 2.2f, 24},
    /* Wave */      {"M30 46Q42 34 54 46T78 46M30 63Q42 51 54 63T78 63", 's', 7, 108},
    /* Check */     {"M5 12.5l4.5 4.5L19 7.5", 's', 2.4f, 24},
};

struct PathReader {
    const char* p;
    void skip() { while (*p == ' ' || *p == ',' || *p == '\n') p++; }
    bool numberAhead() { skip(); return isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.'; }
    float number() {
        skip();
        char* end = nullptr;
        float v = strtof(p, &end);
        p = end == p ? p + 1 : end;
        return v;
    }
};

ID2D1PathGeometry* buildPath(const char* text) {
    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(d2d->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) { drop(geometry); return nullptr; }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    PathReader in{text};
    float x = 0, y = 0, startX = 0, startY = 0, ctrlX = 0, ctrlY = 0;
    bool open = false;
    char command = 0, last = 0;
    auto begin = [&](float bx, float by) {
        if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(D2D1::Point2F(bx, by), D2D1_FIGURE_BEGIN_FILLED);
        open = true;
        x = startX = bx;
        y = startY = by;
    };
    for (;;) {
        in.skip();
        if (!*in.p) break;
        if (isalpha((unsigned char)*in.p)) command = *in.p++;
        else if (!in.numberAhead()) { in.p++; continue; }
        else if (command == 'M') command = 'L';
        else if (command == 'm') command = 'l';
        bool rel = islower((unsigned char)command) != 0;
        float ox = rel ? x : 0, oy = rel ? y : 0;
        switch (toupper(command)) {
            case 'M': { float nx = ox + in.number(), ny = oy + in.number(); begin(nx, ny); break; }
            case 'L': { x = ox + in.number(); y = oy + in.number(); sink->AddLine(D2D1::Point2F(x, y)); break; }
            case 'H': { x = ox + in.number(); sink->AddLine(D2D1::Point2F(x, y)); break; }
            case 'V': { y = oy + in.number(); sink->AddLine(D2D1::Point2F(x, y)); break; }
            case 'C': {
                float x1 = ox + in.number(), y1 = oy + in.number(), x2 = ox + in.number(), y2 = oy + in.number();
                x = ox + in.number(); y = oy + in.number();
                sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), D2D1::Point2F(x, y)));
                ctrlX = x2; ctrlY = y2;
                break;
            }
            case 'S': {
                float x1 = (last == 'C' || last == 'S') ? 2 * x - ctrlX : x, y1 = (last == 'C' || last == 'S') ? 2 * y - ctrlY : y;
                float x2 = ox + in.number(), y2 = oy + in.number();
                x = ox + in.number(); y = oy + in.number();
                sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), D2D1::Point2F(x, y)));
                ctrlX = x2; ctrlY = y2;
                break;
            }
            case 'Q': {
                ctrlX = ox + in.number(); ctrlY = oy + in.number();
                x = ox + in.number(); y = oy + in.number();
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(ctrlX, ctrlY), D2D1::Point2F(x, y)));
                break;
            }
            case 'T': {
                if (last == 'Q' || last == 'T') { ctrlX = 2 * x - ctrlX; ctrlY = 2 * y - ctrlY; } else { ctrlX = x; ctrlY = y; }
                x = ox + in.number(); y = oy + in.number();
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(ctrlX, ctrlY), D2D1::Point2F(x, y)));
                break;
            }
            case 'A': {
                float rx = in.number(), ry = in.number(), rotation = in.number();
                bool large = in.number() != 0, sweep = in.number() != 0;
                x = ox + in.number(); y = oy + in.number();
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(rx, ry), rotation,
                    sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                break;
            }
            case 'O': {
                float cx = in.number(), cy = in.number(), r = in.number();
                begin(cx - r, cy);
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r, cy), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - r, cy), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false;
                break;
            }
            case 'Z': {
                if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false;
                x = startX; y = startY;
                break;
            }
            default: break;
        }
        last = (char)toupper(command);
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    return geometry;
}

// ---------------------------------------------------------------- Text

IDWriteTextFormat* format(float size, int weight) {
    int key = (int)(size * 10) * 1000 + weight;
    auto found = formats.find(key);
    if (found != formats.end()) return found->second;
    IDWriteTextFormat* f = nullptr;
    dwrite->CreateTextFormat(fontFamily, nullptr, (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    formats[key] = f;
    return f;
}

IDWriteTextLayout* makeLayout(const string& s, float width, float height, float size, int weight, bool wrap, float spacing) {
    IDWriteTextFormat* f = format(size, weight);
    if (!f) return nullptr;
    wstring w = W(s);
    IDWriteTextLayout* l = nullptr;
    if (FAILED(dwrite->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, std::max(width, 0.0f), std::max(height, 0.0f), &l))) return nullptr;
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap) {
        DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject* dots = nullptr;
        dwrite->CreateEllipsisTrimmingSign(f, &dots);
        l->SetTrimming(&trimming, dots);
        if (dots) dots->Release();
    }
    if (spacing != 0) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(l->QueryInterface(__uuidof(IDWriteTextLayout1), (void**)&l1))) {
            l1->SetCharacterSpacing(0, spacing, 0, DWRITE_TEXT_RANGE{0, (UINT32)w.size()});
            l1->Release();
        }
    }
    return l;
}

// Laying text out is the costliest part of a frame, and most text is the same from one frame
// to the next: layouts used in this frame or the last are kept, the rest are let go.
std::map<string, IDWriteTextLayout*> layoutsNow, layoutsBefore;

void turnLayouts(bool all) {
    for (auto& kv : layoutsBefore) kv.second->Release();
    layoutsBefore.clear();
    if (all) { for (auto& kv : layoutsNow) kv.second->Release(); layoutsNow.clear(); }
    else layoutsBefore.swap(layoutsNow);
}

// The caller releases what it gets, as before.
IDWriteTextLayout* layout(const string& s, float width, float height, float size, int weight, bool wrap, float spacing) {
    char tail[80];
    snprintf(tail, sizeof(tail), "\x1f%.1f|%.1f|%.1f|%d|%d|%.2f", width, height, size, weight, wrap ? 1 : 0, spacing);
    string key = s + tail;
    IDWriteTextLayout* l = nullptr;
    auto found = layoutsNow.find(key);
    if (found != layoutsNow.end()) l = found->second;
    else {
        auto before = layoutsBefore.find(key);
        if (before != layoutsBefore.end()) { l = before->second; layoutsBefore.erase(before); }
        else l = makeLayout(s, width, height, size, weight, wrap, spacing);
        if (!l) return nullptr;
        layoutsNow[key] = l;
    }
    l->AddRef();
    return l;
}

// ---------------------------------------------------------------- Images

struct CachedImage {
    ID2D1Bitmap* bitmap = nullptr;
    IWICBitmap* pixels = nullptr;   // decoded, waiting to become a bitmap for the target
    Color color;
    bool hasColor = false, loading = false, failed = false;
    long long used = 0;
    size_t bytes = 0;               // of pixels held
};
std::map<string, CachedImage> images;

void decode(const string& key, const string& bytes, bool soft) {
    CachedImage& entry = images[key];
    entry.loading = false;
    entry.failed = true;
    if (bytes.empty() || !wic) return;
    IStream* stream = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size());
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    if (stream && SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut))) {
        IWICBitmapSource* source = converter;
        UINT w = 0, h = 0;
        converter->GetSize(&w, &h);
        if (soft && std::min(w, h) > 12 && SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
            // Average nearby pixels once. Linear interpolation softens the preview when drawn.
            double ratio = 12.0 / std::min(w, h);
            if (SUCCEEDED(scaler->Initialize(converter, (UINT)lround(w * ratio), (UINT)lround(h * ratio), WICBitmapInterpolationModeFant)))
                source = scaler;
        }
        entry.failed = FAILED(wic->CreateBitmapFromSource(source, WICBitmapCacheOnLoad, &entry.pixels));
    }
    if (!entry.failed && !soft) {
        // The overall color: the average of its livelier pixels.
        UINT w = 0, h = 0;
        entry.pixels->GetSize(&w, &h);
        IWICBitmapLock* lock = nullptr;
        WICRect all = {0, 0, (INT)w, (INT)h};
        if (w && h && SUCCEEDED(entry.pixels->Lock(&all, WICBitmapLockRead, &lock))) {
            UINT size = 0, stride = 0;
            BYTE* data = nullptr;
            lock->GetStride(&stride);
            lock->GetDataPointer(&size, &data);
            double r = 0, g = 0, b = 0, weight = 0;
            UINT step = std::max<UINT>(1, std::min(w, h) / 24);
            for (UINT y = 0; y < h; y += step) for (UINT x = 0; x < w; x += step) {
                BYTE* px = data + y * stride + x * 4;
                double pb = px[0] / 255.0, pg = px[1] / 255.0, pr = px[2] / 255.0;
                double hi = std::max(pr, std::max(pg, pb)), lo = std::min(pr, std::min(pg, pb));
                double k = 0.05 + (hi - lo) * (hi > 0.15 ? 1 : 0.2);
                r += pr * k; g += pg * k; b += pb * k; weight += k;
            }
            if (weight > 0) { entry.color = Color{(float)(r / weight), (float)(g / weight), (float)(b / weight), 1}; entry.hasColor = true; }
            lock->Release();
        }
    }
    drop(scaler);
    drop(converter);
    drop(frame);
    drop(decoder);
    drop(stream);
}

CachedImage* imageFor(const string& url, bool soft) {
    if (url.empty()) return nullptr;
    string key = soft ? "soft:" + url : url;
    CachedImage& entry = images[key];
    entry.used = nowMs();
    if (!entry.bitmap && !entry.pixels && !entry.loading && !entry.failed) {
        entry.loading = true;
        // A budget of pixels rather than a count: past it, the covers not drawn for the
        // longest are forgotten (they come back from the network if they're needed again).
        size_t held = 0;
        for (auto& kv : images) held += kv.second.bytes;
        if (held > 4 * 1024 * 1024 || images.size() > 110) {
            std::vector<std::pair<long long, string>> byAge;
            for (auto& kv : images) if (!kv.second.loading) byAge.emplace_back(kv.second.used, kv.first);
            std::sort(byAge.begin(), byAge.end());
            for (size_t i = 0; i < byAge.size() && (held > 3 * 1024 * 1024 || images.size() > 80); i++) {
                CachedImage& old = images[byAge[i].second];
                held -= std::min(held, old.bytes);
                drop(old.bitmap);
                drop(old.pixels);
                images.erase(byAge[i].second);
            }
        }
        string address = url;
        async<string>([address] { HttpRes res = http("GET", address); return res.ok() ? res.body : string(); },
            [key, soft](string bytes) { decode(key, bytes, soft); if (imageArrived) imageArrived(); });
    }
    if (!entry.bitmap && entry.pixels && target) {
        UINT pw = 0, ph = 0;
        entry.pixels->GetSize(&pw, &ph);
        entry.bytes = (size_t)pw * ph * 4;
        rt->CreateBitmapFromWicBitmap(entry.pixels, nullptr, &entry.bitmap);
        drop(entry.pixels);
    }
    return &entry;
}

ID2D1RoundedRectangleGeometry* roundedGeometry(Rect r, float radius) {
    ID2D1RoundedRectangleGeometry* g = nullptr;
    d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rf(r), radius, radius), &g);
    return g;
}

std::vector<ID2D1Geometry*> clipGeometries;   // null = an axis-aligned clip

}   // namespace

// ---------------------------------------------------------------- Colors

Color hex(unsigned rgb, float alpha) { return Color{((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, alpha}; }
Color white(float alpha) { return Color{1, 1, 1, alpha}; }
Color mix(Color a, Color b, float t) { return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }

void toHsl(Color c, float& h, float& s, float& l) {
    float hi = std::max(c.r, std::max(c.g, c.b)), lo = std::min(c.r, std::min(c.g, c.b));
    l = (hi + lo) / 2;
    float d = hi - lo;
    if (d < 1e-5f) { h = 0; s = 0; return; }
    s = l > 0.5f ? d / (2 - hi - lo) : d / (hi + lo);
    if (hi == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6 : 0);
    else if (hi == c.g) h = (c.b - c.r) / d + 2;
    else h = (c.r - c.g) / d + 4;
    h *= 60;
}

Color fromHsl(float h, float s, float l, float alpha) {
    h = fmodf(fmodf(h, 360) + 360, 360) / 360;
    auto channel = [](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1 / 6.0f) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2 / 3.0f) return p + (q - p) * (2 / 3.0f - t) * 6;
        return p;
    };
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    return Color{channel(p, q, h + 1 / 3.0f), channel(p, q, h), channel(p, q, h - 1 / 3.0f), alpha};
}

// ---------------------------------------------------------------- Setup

bool gfx::start() {
    if (d2d) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dwrite))) return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    // Windows 11's own interface font, when it's there.
    IDWriteFontCollection* fonts = nullptr;
    if (SUCCEEDED(dwrite->GetSystemFontCollection(&fonts))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) fontFamily = L"Segoe UI Variable Text";
        fonts->Release();
    }
    D2D1_STROKE_STYLE_PROPERTIES round = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    d2d->CreateStrokeStyle(round, nullptr, 0, &roundStroke);
    return true;
}

bool gfx::begin(HDC dc, int pxW, int pxH, float scale, bool clearAll) {
    if (!d2d) return false;
    if (!target) {
        // Software on purpose: a graphics driver in the process costs tens of megabytes.
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        if (FAILED(d2d->CreateDCRenderTarget(&props, &target))) return false;
        rt = target;
        rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
    rt = target;
    RECT all = {0, 0, pxW, pxH};
    if (FAILED(target->BindDC(dc, &all))) return false;
    rt->SetDpi(96 * scale, 96 * scale);
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (clearAll) rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    turnLayouts(false);
    lost = false;
    return true;
}

bool gfx::end() {
    if (!target) return false;
    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        for (auto& kv : images) drop(kv.second.bitmap);
        images.clear();
        drop(cachedPicture);
        cachedKey.clear();
        drop(brush);
        drop(target);
        rt = nullptr;
        return false;
    }
    return true;
}

void gfx::onImage(std::function<void()> fn) { imageArrived = std::move(fn); }

void gfx::forgetImages() {
    for (auto& kv : images) { drop(kv.second.bitmap); drop(kv.second.pixels); }
    for (auto it = images.begin(); it != images.end();) it = it->second.loading ? std::next(it) : images.erase(it);
}

void gfx::trimImages(long long keepMs) {
    long long now = nowMs();
    for (auto it = images.begin(); it != images.end();) {
        CachedImage& entry = it->second;
        if (entry.loading || now - entry.used <= keepMs) { ++it; continue; }
        // The color stays known (it's a few bytes); the pixels go.
        drop(entry.bitmap);
        drop(entry.pixels);
        entry.bytes = 0;
        if (entry.hasColor) ++it;
        else it = images.erase(it);
    }
    drop(cachedPicture);
    cachedKey.clear();
    turnLayouts(true);
}

void gfx::clear() { rt->Clear(D2D1::ColorF(0, 0, 0, 0)); }

void gfx::shadow(Rect r, float radius, float spread, float alpha) {
    // The shape itself (mostly hidden under the card), then rings fading outward: a soft
    // edge without any blurring.
    rt->FillRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), paint(Color{0, 0, 0, alpha}));
    for (float i = 0.5f; i < spread; i += 1) {
        float fade = 1 - i / spread;
        Rect ring = r.inset(-i);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rf(ring), radius + i, radius + i), paint(Color{0, 0, 0, alpha * fade * fade}), 1.3f);
    }
}

// ---------------------------------------------------------------- Shapes

void gfx::fill(Rect r, Color c, float radius) {
    if (radius <= 0) rt->FillRectangle(rf(r), paint(c));
    else rt->FillRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), paint(c));
}

void gfx::fillCorners(Rect r, Color c, float tl, float tr, float br, float bl) {
    ID2D1PathGeometry* g = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(d2d->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) { drop(g); return; }
    auto arc = [&](float x, float y, float radius) {
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure(D2D1::Point2F(r.x + tl, r.y), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(r.right() - tr, r.y));
    arc(r.right(), r.y + tr, tr);
    sink->AddLine(D2D1::Point2F(r.right(), r.bottom() - br));
    arc(r.right() - br, r.bottom(), br);
    sink->AddLine(D2D1::Point2F(r.x + bl, r.bottom()));
    arc(r.x, r.bottom() - bl, bl);
    sink->AddLine(D2D1::Point2F(r.x, r.y + tl));
    arc(r.x + tl, r.y, tl);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    rt->FillGeometry(g, paint(c));
    g->Release();
}

void gfx::outline(Rect r, Color c, float radius, float width) {
    Rect in = r.inset(width / 2);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rf(in), std::max(0.0f, radius - width / 2), std::max(0.0f, radius - width / 2)), paint(c), width);
}

void gfx::circle(float cx, float cy, float radius, Color c) {
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), paint(c));
}

void gfx::line(float x1, float y1, float x2, float y2, Color c, float width) {
    rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), paint(c), width, roundStroke);
}

void gfx::glow(float cx, float cy, float rx, float ry, Color c) {
    D2D1_GRADIENT_STOP stops[2] = {{0, cf(c)}, {1, cf(c.alpha(0))}};
    ID2D1GradientStopCollection* collection = nullptr;
    ID2D1RadialGradientBrush* radial = nullptr;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, &collection)) &&
        SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rx, ry), collection, &radial)))
        rt->FillRectangle(D2D1::RectF(cx - rx, cy - ry, cx + rx, cy + ry), radial);
    drop(radial);
    drop(collection);
}

void gfx::wave(Rect r, float progress, float phase, float amplitude, Color c) {
    progress = std::max(0.0f, std::min(1.0f, progress));
    float mid = r.y + r.h / 2, end = r.x + r.w * progress;
    const float length = 20, width = 3;
    if (end - r.x > 1) {
        ID2D1PathGeometry* g = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(d2d->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&sink))) {
            auto at = [&](float x) {
                // Flat at both ends so the wave joins the line smoothly.
                float edge = std::min(1.0f, std::min(x - r.x, end - x) / 6);
                return mid + sinf((x - r.x) / length * 6.2831853f + phase) * amplitude * std::max(0.0f, edge);
            };
            sink->BeginFigure(D2D1::Point2F(r.x, at(r.x)), D2D1_FIGURE_BEGIN_HOLLOW);
            for (float x = r.x + 1.5f; x < end; x += 1.5f) sink->AddLine(D2D1::Point2F(x, at(x)));
            sink->AddLine(D2D1::Point2F(end, mid));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            rt->DrawGeometry(g, paint(c), width, roundStroke);
        }
        drop(g);
    }
    if (r.right() - end > 5) line(end + 5, mid, r.right(), mid, c.alpha(c.a * 0.35f), width);
}

// ---------------------------------------------------------------- Text and icons

void gfx::text(const string& s, Rect r, float size, int weight, Color c, int align, bool wrap, bool top, float spacing) {
    if (s.empty() || r.w <= 0) return;
    IDWriteTextLayout* l = layout(s, r.w, r.h, size, weight, wrap, spacing);
    if (!l) return;
    l->SetTextAlignment(align == AlignCenter ? DWRITE_TEXT_ALIGNMENT_CENTER : align == AlignRight ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    l->SetParagraphAlignment(top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt->DrawTextLayout(D2D1::Point2F(r.x, r.y), l, paint(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    l->Release();
}

void gfx::textSung(const string& s, Rect r, float size, int weight, Color dim, Color bright, int sungChars, int wordChars, float wordPart) {
    if (s.empty() || r.w <= 0) return;
    IDWriteTextLayout* l = layout(s, r.w, r.h, size, weight, true, 0);
    if (!l) return;
    l->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    rt->DrawTextLayout(D2D1::Point2F(r.x, r.y), l, paint(dim));
    // The sung part again in the bright color, clipped to where those letters are.
    auto over = [&](int from, int count, float part) {
        if (count <= 0 || part <= 0) return;
        DWRITE_HIT_TEST_METRICS boxes[8];
        UINT32 got = 0;
        if (FAILED(l->HitTestTextRange((UINT32)from, (UINT32)count, r.x, r.y, boxes, 8, &got))) return;
        for (UINT32 i = 0; i < got; i++) {
            float w = boxes[i].width * (got == 1 ? part : (i + 1 < got ? 1.0f : part));
            rt->PushAxisAlignedClip(D2D1::RectF(boxes[i].left, boxes[i].top - 2, boxes[i].left + w, boxes[i].top + boxes[i].height + 2), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            rt->DrawTextLayout(D2D1::Point2F(r.x, r.y), l, paint(bright));
            rt->PopAxisAlignedClip();
        }
    };
    over(0, sungChars, 1);
    over(sungChars, wordChars, wordPart);
    l->Release();
}

float gfx::textWidth(const string& s, float size, int weight, float spacing) {
    IDWriteTextLayout* l = layout(s, 4000, 100, size, weight, false, spacing);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m = {};
    l->GetMetrics(&m);
    l->Release();
    return m.widthIncludingTrailingWhitespace;
}

float gfx::textHeight(const string& s, float width, float size, int weight) {
    IDWriteTextLayout* l = layout(s, width, 4000, size, weight, true, 0);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m = {};
    l->GetMetrics(&m);
    l->Release();
    return m.height;
}

void gfx::icon(Icon which, Rect r, Color c) {
    int i = (int)which;
    const IconDef& def = iconDefs[i];
    if (!icons[i]) icons[i] = buildPath(def.path);
    if (!icons[i]) return;
    D2D1_MATRIX_3X2_F before;
    rt->GetTransform(&before);
    float k = r.w / def.box;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(r.x, r.y) * before);
    if (def.mode == 'f') rt->FillGeometry(icons[i], paint(c));
    else rt->DrawGeometry(icons[i], paint(c), def.stroke, roundStroke);
    rt->SetTransform(before);
}

// ---------------------------------------------------------------- Images

bool gfx::imageColor(const string& url, Color& out) {
    auto found = images.find(url);
    if (found == images.end() || !found->second.hasColor) return false;
    out = found->second.color;
    return true;
}

bool gfx::image(const string& url, Rect r, float radius, float fadeFrom, bool soft) {
    CachedImage* entry = imageFor(url, soft);
    if (!entry || !entry->bitmap) return false;
    D2D1_SIZE_F size = entry->bitmap->GetSize();
    // Fill the rect, cropping what doesn't fit (covers are square; video stills aren't).
    float want = r.w / r.h, have = size.width / size.height;
    D2D1_RECT_F source = D2D1::RectF(0, 0, size.width, size.height);
    if (have > want) { float w = size.height * want; source.left = (size.width - w) / 2; source.right = source.left + w; }
    else { float h = size.width / want; source.top = (size.height - h) / 2; source.bottom = source.top + h; }
    ID2D1Geometry* shape = radius > 0 ? roundedGeometry(r, radius) : nullptr;
    ID2D1LinearGradientBrush* fade = nullptr;
    ID2D1GradientStopCollection* stops = nullptr;
    if (fadeFrom < 1) {
        D2D1_GRADIENT_STOP s[3] = {{0, D2D1::ColorF(0, 0, 0, 1)}, {fadeFrom, D2D1::ColorF(0, 0, 0, 1)}, {1, D2D1::ColorF(0, 0, 0, 0)}};
        if (SUCCEEDED(rt->CreateGradientStopCollection(s, 3, &stops)))
            rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, 0), D2D1::Point2F(r.right(), 0)), stops, &fade);
    }
    bool layered = shape || fade;
    if (layered) rt->PushLayer(D2D1::LayerParameters(rf(r), shape, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, fade), nullptr);
    rt->DrawBitmap(entry->bitmap, rf(r), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    if (layered) rt->PopLayer();
    drop(fade);
    drop(stops);
    drop(shape);
    return true;
}

// ---------------------------------------------------------------- Clips and fades

void gfx::pushClip(Rect r, float radius) {
    if (radius <= 0) {
        rt->PushAxisAlignedClip(rf(r), D2D1_ANTIALIAS_MODE_ALIASED);
        clipGeometries.push_back(nullptr);
    } else {
        ID2D1Geometry* g = roundedGeometry(r, radius);
        rt->PushLayer(D2D1::LayerParameters(rf(r), g), nullptr);
        clipGeometries.push_back(g);
    }
}

void gfx::popClip() {
    if (clipGeometries.empty()) return;
    ID2D1Geometry* g = clipGeometries.back();
    clipGeometries.pop_back();
    if (g) { rt->PopLayer(); g->Release(); }
    else rt->PopAxisAlignedClip();
}

void gfx::pushFade(Rect r, float top, float bottom) {
    float a = std::min(0.49f, top / r.h), b = std::max(0.51f, 1 - bottom / r.h);
    D2D1_GRADIENT_STOP s[4] = {{0, D2D1::ColorF(0, 0, 0, top > 0 ? 0.0f : 1.0f)}, {a, D2D1::ColorF(0, 0, 0, 1)}, {b, D2D1::ColorF(0, 0, 0, 1)}, {1, D2D1::ColorF(0, 0, 0, bottom > 0 ? 0.0f : 1.0f)}};
    ID2D1GradientStopCollection* stops = nullptr;
    ID2D1LinearGradientBrush* fade = nullptr;
    if (SUCCEEDED(rt->CreateGradientStopCollection(s, 4, &stops)))
        rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, r.y), D2D1::Point2F(0, r.bottom())), stops, &fade);
    rt->PushLayer(D2D1::LayerParameters(rf(r), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, fade), nullptr);
    drop(fade);
    drop(stops);
}

void gfx::popFade() { rt->PopLayer(); }

void gfx::pushOpacity(Rect r, float opacity) {
    rt->PushLayer(D2D1::LayerParameters(rf(r), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), nullptr);
}

void gfx::popOpacity() { rt->PopLayer(); }

static std::vector<D2D1_MATRIX_3X2_F> transforms;

void gfx::pushTransform(float scale, float dx, float dy) {
    D2D1_MATRIX_3X2_F before;
    rt->GetTransform(&before);
    transforms.push_back(before);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(dx, dy) * before);
}

void gfx::popTransform() {
    if (transforms.empty()) return;
    rt->SetTransform(transforms.back());
    transforms.pop_back();
}

void gfx::cached(const string& key, Rect r, const std::function<void()>& paintIt) {
    if (!cachedPicture || key != cachedKey) {
        drop(cachedPicture);
        ID2D1BitmapRenderTarget* offscreen = nullptr;
        if (SUCCEEDED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(r.w, r.h), &offscreen))) {
            ID2D1RenderTarget* before = rt;
            rt = offscreen;
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            paintIt();
            rt->EndDraw();
            rt = before;
            offscreen->GetBitmap(&cachedPicture);
            offscreen->Release();
            cachedKey = key;
        }
    }
    if (cachedPicture) rt->DrawBitmap(cachedPicture, rf(r), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else paintIt();
}

// ---------------------------------------------------------------- Saving a picture (the render check)

bool gfx::savePng(const wstring& path, const void* bgra, int w, int h) {
    if (!wic) return false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    bool ok = SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
              SUCCEEDED(frame->SetPixelFormat(&pixelFormat)) && SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, (BYTE*)bgra)) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    drop(frame);
    drop(encoder);
    drop(stream);
    return ok;
}
