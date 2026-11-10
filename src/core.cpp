#include "core.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <process.h>

wstring W(const string& s) {
    if (s.empty()) return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

string U(const wstring& w) {
    if (w.empty()) return string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

string urlEncode(const string& s) {
    static const char* hex = "0123456789ABCDEF";
    string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

string jq(const string& s) {
    string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; wsprintfA(b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    return out + "\"";
}

string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool startsWith(const string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
bool contains(const string& s, const char* p) { return s.find(p) != string::npos; }
string lower(string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

std::vector<string> split(const string& s, const string& sep) {
    std::vector<string> out;
    size_t at = 0;
    for (;;) {
        size_t next = s.find(sep, at);
        if (next == string::npos) { out.push_back(s.substr(at)); break; }
        out.push_back(s.substr(at, next - at));
        at = next + sep.size();
    }
    return out;
}

string formatTime(double seconds) {
    if (!(seconds > 0)) seconds = 0;
    int total = (int)seconds;
    char b[32];
    if (total >= 3600) wsprintfA(b, "%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    else wsprintfA(b, "%d:%02d", total / 60, total % 60);
    return b;
}

long long nowMs() { return (long long)GetTickCount64(); }

// ---------------------------------------------------------------- JSON

const Json& Json::none() { static const Json j; return j; }

const Json& Json::operator[](const char* key) const {
    if (type == Obj) for (auto& kv : o) if (kv.first == key) return kv.second;
    return none();
}

const Json& Json::operator[](int i) const {
    if (type != Arr) return none();
    int n = (int)a.size();
    if (i < 0) i += n;
    return i >= 0 && i < n ? a[i] : none();
}

double Json::num(double fallback) const {
    if (type == Num) return n;
    if (type == Str && !s.empty()) return atof(s.c_str());
    return fallback;
}

string Json::text() const {
    const Json& runs = (*this)["runs"];
    if (runs.type == Arr) {
        string out;
        for (auto& r : runs.a) out += r["text"].str();
        return out;
    }
    return (*this)["simpleText"].str();
}

string Json::thumb() const {
    const Json* lists[] = {
        &at("musicThumbnailRenderer", "thumbnail", "thumbnails"),
        &at("croppedSquareThumbnailRenderer", "thumbnail", "thumbnails"),
        &at("thumbnail", "thumbnails"),
        &(*this)["thumbnails"],
    };
    for (auto* l : lists) {
        if (l->type == Arr && !l->a.empty()) {
            string url = l->a.back()["url"].str();
            return startsWith(url, "//") ? "https:" + url : url;
        }
    }
    return string();
}

string Json::pageType() const {
    return at("browseEndpoint", "browseEndpointContextSupportedConfigs", "browseEndpointContextMusicConfig", "pageType").str();
}

namespace {
struct Parser {
    const char* p;
    const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    static void utf8(string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 63)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
    }
    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < end; i++, p++) {
            char c = *p;
            v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
        }
        return v;
    }
    string str() {
        string out;
        p++;   // opening quote
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p++) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: out += p[-1];
                }
            } else out += *p++;
        }
        if (p < end) p++;
        return out;
    }
    Json value(int depth) {
        Json j;
        ws();
        if (p >= end || depth > 200) return j;
        char c = *p;
        if (c == '{') {
            j.type = Json::Obj;
            p++;
            for (;;) {
                ws();
                if (p >= end) break;
                if (*p == '}') { p++; break; }
                if (*p == ',') { p++; continue; }
                if (*p != '"') { p = end; break; }
                string key = str();
                ws();
                if (p < end && *p == ':') p++;
                j.o.emplace_back(std::move(key), value(depth + 1));
            }
        } else if (c == '[') {
            j.type = Json::Arr;
            p++;
            for (;;) {
                ws();
                if (p >= end) break;
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                j.a.push_back(value(depth + 1));
            }
        } else if (c == '"') {
            j.type = Json::Str;
            j.s = str();
        } else if (c == 't' || c == 'f') {
            j.type = Json::Bool;
            j.b = c == 't';
            p += c == 't' ? 4 : 5;
        } else if (c == 'n') {
            p += 4;
        } else {
            char* stop = nullptr;
            j.type = Json::Num;
            j.n = strtod(p, &stop);
            if (stop == p) { j.type = Json::Null; p = end; }
            else p = stop;
        }
        if (p > end) p = end;
        return j;
    }
};
}

Json Json::parse(const string& text) {
    Parser parser{text.data(), text.data() + text.size()};
    return parser.value(0);
}

// ---------------------------------------------------------------- HTTP

static HINTERNET session() {
    static HINTERNET s = [] {
        HINTERNET h = WinHttpOpen(L"WaveMini", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h) h = WinHttpOpen(L"WaveMini", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
            WinHttpSetTimeouts(h, 15000, 15000, 20000, 30000);
        }
        return h;
    }();
    return s;
}

HttpRes http(const char* method, const string& url, const Headers& headers, const string& body,
             const std::function<bool(const char*, size_t)>& sink, const std::atomic<bool>* cancel) {
    HttpRes res;
    wstring wurl = W(url);
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    if (!session() || !WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &parts)) return res;
    // Path and query, straight from the address.
    size_t scheme = wurl.find(L"://");
    size_t slash = scheme == wstring::npos ? wstring::npos : wurl.find(L'/', scheme + 3);
    wstring path = slash == wstring::npos ? L"/" : wurl.substr(slash);
    HINTERNET connect = WinHttpConnect(session(), host, parts.nPort, 0);
    if (!connect) return res;
    HINTERNET request = WinHttpOpenRequest(connect, W(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (request) {
        DWORD noCookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));
        wstring all;
        for (auto& h : headers) all += W(h.first) + L": " + W(h.second) + L"\r\n";
        if (!all.empty()) WinHttpAddRequestHeaders(request, all.c_str(), (DWORD)all.size(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            res.status = (int)status;
            wchar_t length[32] = {};
            size = sizeof(length);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size, WINHTTP_NO_HEADER_INDEX))
                res.contentLength = _wtoi64(length);
            if (status == 200 || status == 206) {
                wchar_t range[128] = {};
                size = sizeof(range);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, range, &size, WINHTTP_NO_HEADER_INDEX))
                    res.contentRange = U(range);
            }
            char buffer[16384];
            for (;;) {
                if (cancel && cancel->load()) { res.status = 0; break; }
                DWORD got = 0;
                if (!WinHttpReadData(request, buffer, sizeof(buffer), &got)) { res.status = 0; break; }
                if (got == 0) break;
                if (sink) { if (!sink(buffer, got)) break; }
                else res.body.append(buffer, got);
            }
        }
        WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connect);
    return res;
}

// ---------------------------------------------------------------- Work

static HWND uiWindow;
static UINT uiMessage;

void setUiWindow(HWND hwnd, UINT message) { uiWindow = hwnd; uiMessage = message; }

void postUi(std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    if (!uiWindow || !PostMessageW(uiWindow, uiMessage, 0, (LPARAM)boxed)) delete boxed;
}

void runUiCall(LPARAM lparam) {
    auto* boxed = (std::function<void()>*)lparam;
    if (!boxed) return;
    (*boxed)();
    delete boxed;
}

static unsigned __stdcall workerMain(void* arg) {
    auto* boxed = (std::function<void()>*)arg;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (*boxed)();
    delete boxed;
    CoUninitialize();
    return 0;
}

void background(std::function<void()> work) {
    auto* boxed = new std::function<void()>(std::move(work));
    // Small stacks: these threads only wait on the network.
    HANDLE thread = (HANDLE)_beginthreadex(nullptr, 256 * 1024, workerMain, boxed, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (thread) CloseHandle(thread);
    else delete boxed;
}

// ---------------------------------------------------------------- Files and secrets

wstring dataDir() {
    static wstring dir = [] {
        wchar_t base[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) GetTempPathW(MAX_PATH, base);
        wstring d = wstring(base) + L"\\WaveMini";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

wstring cacheDir() {
    static wstring dir = [] {
        wstring d = dataDir() + L"\\cache";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }();
    return dir;
}

string readFile(const wstring& path) {
    string out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        if (!ReadFile(f, &out[0], (DWORD)out.size(), &got, nullptr)) got = 0;
        out.resize(got);
    }
    CloseHandle(f);
    return out;
}

bool writeFile(const wstring& path, const string& data) {
    wstring part = path + L".part";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, nullptr);
    CloseHandle(f);
    return ok && MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

string sha1Hex(const string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char hash[20] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0) {
        BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), hash, sizeof(hash));
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    static const char* hex = "0123456789abcdef";
    string out;
    for (unsigned char c : hash) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

string protect(const string& secret) {
    DATA_BLOB in = {(DWORD)secret.size(), (BYTE*)secret.data()}, out = {};
    if (!CryptProtectData(&in, L"WaveMini", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return string();
    DWORD n = 0;
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    string text(n, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &text[0], &n);
    text.resize(n);
    LocalFree(out.pbData);
    return text;
}

string unprotect(const string& stored) {
    if (stored.empty()) return string();
    DWORD n = 0;
    if (!CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return string();
    std::vector<BYTE> raw(n);
    CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, raw.data(), &n, nullptr, nullptr);
    DATA_BLOB in = {n, raw.data()}, out = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return string();
    string secret((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return secret;
}
