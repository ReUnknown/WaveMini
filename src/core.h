// WaveMini core: strings, JSON, HTTP, background work, files and secrets.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using std::string;
using std::wstring;

wstring W(const string& utf8);
string U(const wstring& utf16);
string urlEncode(const string& s);
string jq(const string& s);           // "quoted and escaped" for JSON
string trim(const string& s);
bool startsWith(const string& s, const char* prefix);
bool contains(const string& s, const char* part);
string lower(string s);
std::vector<string> split(const string& s, const string& sep);
string formatTime(double seconds);    // 3:24

// ---------------------------------------------------------------- JSON

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    string s;
    std::vector<Json> a;
    std::vector<std::pair<string, Json>> o;

    static const Json& none();
    static Json parse(const string& text);

    bool has() const { return type != Null; }
    const Json& operator[](const char* key) const;
    const Json& operator[](int index) const;   // negative counts from the end
    const std::vector<Json>& each() const { return a; }
    string str() const { return type == Str ? s : string(); }
    bool truthy() const { return type == Bool && b; }
    double num(double fallback = 0) const;

    template <typename... P> const Json& at(P... path) const {
        const Json* j = this;
        (void)std::initializer_list<int>{(j = &(*j)[path], 0)...};
        return *j;
    }
    // YouTube's "runs" text, joined (or simpleText).
    string text() const;
    // The biggest thumbnail of a thumbnail holder.
    string thumb() const;
    string pageType() const;
};

// ---------------------------------------------------------------- HTTP

typedef std::vector<std::pair<string, string>> Headers;

struct HttpRes {
    int status = 0;          // 0 = couldn't connect
    string body;
    string contentRange;
    long long contentLength = -1;
    bool ok() const { return status >= 200 && status < 300; }
};

// Blocking; call from a worker. [sink] receives the body in pieces instead of HttpRes::body
// (return false to stop). [cancel] aborts between pieces.
HttpRes http(const char* method, const string& url, const Headers& headers = {}, const string& body = "",
             const std::function<bool(const char*, size_t)>& sink = nullptr, const std::atomic<bool>* cancel = nullptr);

// ---------------------------------------------------------------- Work

// Runs [fn] on the window's thread (set once by the app).
void setUiWindow(HWND hwnd, UINT message);
void postUi(std::function<void()> fn);
void runUiCall(LPARAM lparam);
// Runs [work] on a background thread.
void background(std::function<void()> work);

template <typename T> void async(std::function<T()> work, std::function<void(T)> done) {
    background([work, done] {
        auto result = std::make_shared<T>(work());
        postUi([result, done] { done(std::move(*result)); });
    });
}

// ---------------------------------------------------------------- Files and secrets

wstring dataDir();     // %LOCALAPPDATA%\WaveMini
wstring cacheDir();    // ...\cache
string readFile(const wstring& path);
bool writeFile(const wstring& path, const string& data);
string sha1Hex(const string& data);
string protect(const string& secret);     // Windows keeps the key (DPAPI); base64 text
string unprotect(const string& stored);
long long nowMs();
