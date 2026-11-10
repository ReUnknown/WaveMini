#include "signin.h"
#include <shellapi.h>

#ifdef HAVE_WEBVIEW2
#include <WebView2.h>
#ifdef WAVEMINI_EMBEDDED_WEBVIEW2
#include "../res/resource.h"
#endif

#ifdef _MSC_VER
#define IID_OF(Type) __uuidof(Type)
#else
#define IID_OF(Type) IID_##Type
#endif

namespace {

const wchar_t* SignInUrl = L"https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fmusic.youtube.com";
const int Header = 40;   // the strip above Google's page, in design units
const int RetryButton = 1;

// A COM callback from a lambda.
template <typename I, typename... A> struct Handler : I {
    LONG refs = 1;
    IID iid;
    std::function<HRESULT(A...)> fn;
    Handler(REFIID id, std::function<HRESULT(A...)> f) : iid(id), fn(std::move(f)) {}
    virtual ~Handler() {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == iid) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP Invoke(A... args) override { return fn(args...); }
};

HWND host;
HWND retry;
ICoreWebView2Controller* controller;
ICoreWebView2* webview;
std::function<void(bool, Session)> finish;
bool checking;
bool verification, requireAccount;
unsigned requestId;
wstring startUrl;
const wchar_t* headerText;
float hostScale = 1;

#ifdef WAVEMINI_EMBEDDED_WEBVIEW2
// The SDK's static loader needs MSVC. Cross builds carry the official loader in the EXE.
struct EmbeddedLoader {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = {};

    ~EmbeddedLoader() {
        if (module) FreeLibrary(module);
        if (*path) DeleteFileW(path);
    }

    HMODULE load() {
        if (module) return module;
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(WEBVIEW2_LOADER_RESOURCE), RT_RCDATA);
        if (!resource) return nullptr;
        DWORD size = SizeofResource(nullptr, resource);
        HGLOBAL contents = LoadResource(nullptr, resource);
        const void* bytes = contents ? LockResource(contents) : nullptr;
        if (!size || !bytes) return nullptr;

        wchar_t folder[MAX_PATH];
        DWORD folderLength = GetTempPathW(MAX_PATH, folder);
        if (!folderLength || folderLength >= MAX_PATH || !GetTempFileNameW(folder, L"WVM", 0, path)) return nullptr;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        DWORD written = 0;
        if (file != INVALID_HANDLE_VALUE) {
            if (!WriteFile(file, bytes, size, &written, nullptr)) written = 0;
            CloseHandle(file);
        }
        if (written == size) module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { DeleteFileW(path); *path = 0; }
        return module;
    }
};
#endif

HRESULT createEnvironment(const wchar_t* folder, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* completed) {
#ifdef WAVEMINI_EMBEDDED_WEBVIEW2
    static EmbeddedLoader loader;
    HMODULE module = loader.load();
    if (!module) return E_FAIL;
    auto create = reinterpret_cast<decltype(&CreateCoreWebView2EnvironmentWithOptions)>(
        GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions"));
    return create ? create(nullptr, folder, nullptr, completed) : E_NOINTERFACE;
#else
    return CreateCoreWebView2EnvironmentWithOptions(nullptr, folder, nullptr, completed);
#endif
}

void teardown() {
    ++requestId;
    if (controller) { controller->Close(); controller->Release(); controller = nullptr; }
    if (webview) { webview->Release(); webview = nullptr; }
    if (host) { HWND h = host; host = nullptr; DestroyWindow(h); }
    retry = nullptr;
}

void done(bool ok, Session session) {
    auto fn = finish;
    finish = nullptr;
    if (!fn) return;
    // Not from inside one of the browser's own callbacks.
    unsigned id = requestId;
    postUi([fn, ok, session, id] {
        if (id != requestId) return;
        teardown();
        fn(ok, session);
    });
}

void layout() {
    if (!host || !controller) return;
    RECT r;
    GetClientRect(host, &r);
    int h = (int)(Header * hostScale);
    int buttonW = (int)(130 * hostScale);
    if (retry) MoveWindow(retry, r.right - h - buttonW, 5, buttonW, h - 10, TRUE);
    r.top += h;
    controller->put_Bounds(r);
}

void endCheck(const wchar_t* text) {
    checking = false;
    if (!retry) return;
    headerText = text;
    EnableWindow(retry, TRUE);
    InvalidateRect(host, nullptr, TRUE);
}

// Capture a Music session after sign-in or when the user requests a playback retry.
void check() {
    if (!webview || checking || !finish) return;
    LPWSTR source = nullptr;
    if (FAILED(webview->get_Source(&source)) || !source) return;
    bool there = wcscmp(source, L"https://music.youtube.com") == 0 ||
                 wcsncmp(source, L"https://music.youtube.com/", 26) == 0;
    CoTaskMemFree(source);
    if (!there) {
        endCheck(L"Finish on YouTube Music before retrying.");
        return;
    }
    checking = true;
    if (retry) EnableWindow(retry, FALSE);
    unsigned id = requestId;
    auto* script = new Handler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(IID_OF(ICoreWebView2ExecuteScriptCompletedHandler),
        [id](HRESULT hr, LPCWSTR result) -> HRESULT {
            if (id != requestId || !finish) return S_OK;
            Session session;
            if (SUCCEEDED(hr) && result) {
                // A JSON string holding JSON.
                Json config = Json::parse(Json::parse(U(result)).str());
                session.visitorData = config["v"].str();
                session.dataSyncId = config["d"].str();
                session.authUser = config["a"].str();
            }
            ICoreWebView2_2* modern = nullptr;
            ICoreWebView2CookieManager* cookies = nullptr;
            if (!webview || FAILED(webview->QueryInterface(IID_OF(ICoreWebView2_2), (void**)&modern)) || FAILED(modern->get_CookieManager(&cookies))) {
                if (modern) modern->Release();
                endCheck(L"Couldn't read the YouTube session. Try again.");
                return S_OK;
            }
            auto* got = new Handler<ICoreWebView2GetCookiesCompletedHandler, HRESULT, ICoreWebView2CookieList*>(IID_OF(ICoreWebView2GetCookiesCompletedHandler),
                [session, id](HRESULT hr2, ICoreWebView2CookieList* list) -> HRESULT {
                    if (id != requestId || !finish) return S_OK;
                    Session s = session;
                    UINT count = 0;
                    if (FAILED(hr2) || !list || FAILED(list->get_Count(&count))) {
                        endCheck(L"Couldn't read the YouTube session. Try again.");
                        return S_OK;
                    }
                    bool hasAccount = false;
                    for (UINT i = 0; i < count; i++) {
                        ICoreWebView2Cookie* cookie = nullptr;
                        if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie) continue;
                        LPWSTR name = nullptr, value = nullptr;
                        cookie->get_Name(&name);
                        cookie->get_Value(&value);
                        if (name && value) {
                            s.cookie += (s.cookie.empty() ? "" : "; ") + U(name) + "=" + U(value);
                            if (*value && (wcscmp(name, L"SAPISID") == 0 || wcscmp(name, L"__Secure-3PAPISID") == 0)) hasAccount = true;
                        }
                        if (name) CoTaskMemFree(name);
                        if (value) CoTaskMemFree(value);
                        cookie->Release();
                    }
                    if (hasAccount) done(true, s);
                    else if (verification && !requireAccount && (!s.visitorData.empty() || !s.cookie.empty())) {
                        s.guest = true;
                        done(true, s);
                    } else {
                        endCheck(L"Sign in on YouTube Music, then retry playback.");
                    }
                    return S_OK;
                });
            if (FAILED(cookies->GetCookies(L"https://music.youtube.com/", got)))
                endCheck(L"Couldn't read the YouTube session. Try again.");
            got->Release();
            cookies->Release();
            modern->Release();
            return S_OK;
        });
    if (FAILED(webview->ExecuteScript(L"(function(){var get=function(k){return (window.ytcfg&&window.ytcfg.get(k))||(window.yt&&window.yt.config_&&window.yt.config_[k]);};return JSON.stringify({v:get('VISITOR_DATA')||'',d:get('DATASYNC_ID')||'',a:String(get('SESSION_INDEX')||0)});})()", script)))
        endCheck(L"Couldn't read the YouTube session. Try again.");
    script->Release();
}

void startBrowser() {
    wstring folder = dataDir() + L"\\webview";
    unsigned id = requestId;
    auto* environmentReady = new Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        IID_OF(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler), [id](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (id != requestId || !finish) return S_OK;
            if (FAILED(hr) || !environment || !host) { done(false, Session()); return S_OK; }
            auto* controllerReady = new Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                IID_OF(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler), [id](HRESULT hr2, ICoreWebView2Controller* made) -> HRESULT {
                    if (id != requestId || !finish) return S_OK;
                    if (FAILED(hr2) || !made || !host) { done(false, Session()); return S_OK; }
                    controller = made;
                    controller->AddRef();
                    controller->get_CoreWebView2(&webview);
                    layout();
                    if (!webview) { done(false, Session()); return S_OK; }
                    auto* navigated = new Handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                        IID_OF(ICoreWebView2NavigationCompletedEventHandler), [id](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                            if (id != requestId || !finish) return S_OK;
                            if (verification) {
                                if (!checking) EnableWindow(retry, TRUE);
                            } else check();
                            return S_OK;
                        });
                    EventRegistrationToken token;
                    HRESULT registered = webview->add_NavigationCompleted(navigated, &token);
                    navigated->Release();
                    if (FAILED(registered) || FAILED(webview->Navigate(startUrl.c_str()))) done(false, Session());
                    return S_OK;
                });
            if (FAILED(environment->CreateCoreWebView2Controller(host, controllerReady))) done(false, Session());
            controllerReady->Release();
            return S_OK;
        });
    HRESULT hr = createEnvironment(folder.c_str(), environmentReady);
    environmentReady->Release();
    if (FAILED(hr)) done(false, Session());
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH back = CreateSolidBrush(RGB(0x36, 0x61, 0x8E));
            FillRect(dc, &r, back);
            DeleteObject(back);
            int h = (int)(Header * hostScale);
            HFONT font = CreateFontW(-(int)(14 * hostScale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ before = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT title = {(LONG)(14 * hostScale), 0, r.right - h - (retry ? (LONG)(140 * hostScale) : 0), h};
            DrawTextW(dc, headerText, -1, &title, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            RECT cross = {r.right - h, 0, r.right, h};
            DrawTextW(dc, L"\x2715", -1, &cross, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            RECT waiting = {0, h, r.right, r.bottom};
            if (!controller) DrawTextW(dc, L"Opening Google...", -1, &waiting, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            SelectObject(dc, before);
            DeleteObject(font);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == RetryButton && HIWORD(wparam) == BN_CLICKED) check();
            return 0;
        case WM_LBUTTONUP: {
            RECT r;
            GetClientRect(hwnd, &r);
            int h = (int)(Header * hostScale);
            if ((short)HIWORD(lparam) < h && (short)LOWORD(lparam) > r.right - h) done(false, Session());
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_CLOSE: done(false, Session()); return 0;
        default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}   // namespace

void signin::open(HWND owner, RECT over, std::function<void(bool, Session)> finished, const string& videoId, bool signedIn) {
    if (host) return;
    finish = std::move(finished);
    checking = false;
    verification = !videoId.empty();
    requireAccount = signedIn;
    startUrl = verification ? W(yt::watchUrl(videoId)) : SignInUrl;
    headerText = verification ? L"Complete YouTube's check, then retry playback." : L"Sign in with Google";
    WNDCLASSW cls = {};
    cls.lpfnWndProc = hostProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"WaveMiniSignIn";
    RegisterClassW(&cls);
    hostScale = std::max(1.0f, (over.right - over.left) / 420.0f);
    if (verification) {
        MONITORINFO info = {sizeof(info)};
        if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &info)) {
            RECT area = info.rcWork;
            LONG w = std::min(area.right - area.left, (LONG)(840 * hostScale));
            LONG h = std::min(area.bottom - area.top, (LONG)(680 * hostScale));
            LONG x = area.left + (area.right - area.left - w) / 2;
            LONG y = area.top + (area.bottom - area.top - h) / 2;
            over = {x, y, x + w, y + h};
        }
    }
    host = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, verification ? L"Verify on YouTube" : L"Sign in to WaveMini",
                           WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                           over.left, over.top, over.right - over.left, over.bottom - over.top, owner, nullptr, cls.hInstance, nullptr);
    if (!host) { done(false, Session()); return; }
    if (verification) {
        retry = CreateWindowW(L"BUTTON", L"Retry playback", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                              0, 0, 0, 0, host, (HMENU)(INT_PTR)RetryButton, cls.hInstance, nullptr);
        if (!retry) { done(false, Session()); return; }
        SendMessageW(retry, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    SetForegroundWindow(host);
    startBrowser();
}

void signin::close() {
    finish = nullptr;
    teardown();
}

#else

// Built without WebView2 (a compile check on another system): there's no way to sign in.
void signin::open(HWND, RECT, std::function<void(bool, Session)> finished, const string&, bool) {
    postUi([finished] { finished(false, Session()); });
}

void signin::close() {}

#endif

void signin::forget() {
    // Google's own cookies from the sign-in window, so the next sign-in starts clean.
    wstring folder = dataDir() + L"\\webview";
    folder.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = folder.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
}
