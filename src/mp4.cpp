#include "mp4.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {

struct Box {
    size_t start, end;
};

uint32_t big32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

Box findBox(const unsigned char* bytes, Box parent, const char* type, size_t offset = 0) {
    for (size_t at = offset ? offset : parent.start; parent.end - at >= 8;) {
        uint64_t length = big32(bytes + at);
        size_t header = 8;
        if (length == 1) {
            header = 16;
            if (parent.end - at < header) break;
            length = (uint64_t(big32(bytes + at + 8)) << 32) | big32(bytes + at + 12);
        } else if (length == 0) {
            length = parent.end - at;
        }
        if (length < header || length > parent.end - at) break;
        size_t end = at + size_t(length);
        if (memcmp(bytes + at + 4, type, 4) == 0) return {at + header, end};
        at = end;
    }
    return {};
}

unsigned char* leadingEdit(unsigned char* bytes, size_t size) {
    Box file{0, size};
    Box movie = findBox(bytes, file, "moov");
    if (!findBox(bytes, movie, "mvex").start || !findBox(bytes, file, "moof").start) return nullptr;
    Box track = findBox(bytes, movie, "trak");
    if (!track.start || findBox(bytes, movie, "trak", track.end).start) return nullptr;
    Box media = findBox(bytes, track, "mdia");
    Box handler = findBox(bytes, media, "hdlr");
    if (handler.end - handler.start < 12 || memcmp(bytes + handler.start + 8, "soun", 4) != 0)
        return nullptr;

    Box header = findBox(bytes, media, "mdhd");
    if (header.end - header.start < 16 || bytes[header.start] > 1) return nullptr;
    size_t scaleAt = bytes[header.start] == 1 ? 20 : 12;
    if (header.end - header.start < scaleAt + 4) return nullptr;
    uint32_t scale = big32(bytes + header.start + scaleAt);

    Box edits = findBox(bytes, track, "edts");
    Box list = findBox(bytes, edits, "elst");
    size_t length = list.end - list.start;
    if (length < 8 || big32(bytes + list.start + 4) != 1) return nullptr;
    unsigned char* entry = bytes + list.start;
    bool wide = entry[0] == 1;
    if (entry[0] > 1 || length != (wide ? 28 : 20)) return nullptr;
    size_t mediaTimeAt = wide ? 16 : 12;
    if (wide && big32(entry + mediaTimeAt) != 0) return nullptr;
    size_t offsetAt = mediaTimeAt + (wide ? 4 : 0);
    uint32_t mediaTime = big32(entry + offsetAt);
    if (!mediaTime || mediaTime > scale / 10 || big32(entry + offsetAt + 4) != 0x00010000)
        return nullptr;
    return entry + offsetAt;
}

}  // namespace

bool normalizeMp4ForWindows(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    HANDLE mapping = nullptr;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 0 && uint64_t(length.QuadPart) <= SIZE_MAX)
        mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    bool ok = false;
    if (mapping) {
        auto* bytes = static_cast<unsigned char*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0));
        if (bytes) {
            // MediaPlayer can immediately end a fragmented audio file with a short
            // priming edit. Start that edit at zero without moving or re-encoding samples.
            unsigned char* offset = leadingEdit(bytes, size_t(length.QuadPart));
            if (offset) memset(offset, 0, 4);
            ok = !offset || FlushViewOfFile(offset, 4);
            UnmapViewOfFile(bytes);
        }
        CloseHandle(mapping);
    }
    CloseHandle(file);
    return ok;
}
