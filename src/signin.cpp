#include "signin.h"
#include <shellapi.h>

#ifdef HAVE_WEBVIEW2
#include <WebView2.h>

#ifdef _MSC_VER
#define IID_OF(Type) __uuidof(Type)
#else
#define IID_OF(Type) IID_##Type
#endif

namespace {

const wchar_t* SignInUrl = L"https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fmusic.youtube.com";
const int Header = 40;   // the strip above Google's page, in design units

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
ICoreWebView2Controller* controller;
ICoreWebView2* webview;
std::function<void(bool, Session)> finish;
bool checking;
float hostScale = 1;

void teardown() {
    if (controller) { controller->Close(); controller->Release(); controller = nullptr; }
    if (webview) { webview->Release(); webview = nullptr; }
    if (host) { HWND h = host; host = nullptr; DestroyWindow(h); }
}

void done(bool ok, Session session) {
    auto fn = finish;
    finish = nullptr;
    if (!fn) return;
    // Not from inside one of the browser's own callbacks.
    postUi([fn, ok, session] { teardown(); fn(ok, session); });
}

void layout() {
    if (!host || !controller) return;
    RECT r;
    GetClientRect(host, &r);
    r.top += (LONG)(Header * hostScale);
    controller->put_Bounds(r);
}

// On YouTube Music after signing in? Then take the session and finish.
void check() {
    if (!webview || checking || !finish) return;
    LPWSTR source = nullptr;
    if (FAILED(webview->get_Source(&source)) || !source) return;
    bool there = wcsncmp(source, L"https://music.youtube.com", 25) == 0;
    CoTaskMemFree(source);
    if (!there) return;
    checking = true;
    auto* script = new Handler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(IID_OF(ICoreWebView2ExecuteScriptCompletedHandler),
        [](HRESULT hr, LPCWSTR result) -> HRESULT {
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
                checking = false;
                return S_OK;
            }
            auto* got = new Handler<ICoreWebView2GetCookiesCompletedHandler, HRESULT, ICoreWebView2CookieList*>(IID_OF(ICoreWebView2GetCookiesCompletedHandler),
                [session](HRESULT hr2, ICoreWebView2CookieList* list) -> HRESULT {
                    Session s = session;
                    UINT count = 0;
                    if (SUCCEEDED(hr2) && list) list->get_Count(&count);
                    for (UINT i = 0; i < count; i++) {
                        ICoreWebView2Cookie* cookie = nullptr;
                        if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie) continue;
                        LPWSTR name = nullptr, value = nullptr;
                        cookie->get_Name(&name);
                        cookie->get_Value(&value);
                        if (name && value) s.cookie += (s.cookie.empty() ? "" : "; ") + U(name) + "=" + U(value);
                        if (name) CoTaskMemFree(name);
                        if (value) CoTaskMemFree(value);
                        cookie->Release();
                    }
                    checking = false;
                    // SAPISID is what says you're signed in.
                    if (s.cookie.find("SAPISID=") != string::npos) done(true, s);
                    return S_OK;
                });
            cookies->GetCookies(L"https://music.youtube.com", got);
            got->Release();
            cookies->Release();
            modern->Release();
            return S_OK;
        });
    webview->ExecuteScript(L"(function(){var c=(window.yt&&window.yt.config_)||{};return JSON.stringify({v:c.VISITOR_DATA||'',d:c.DATASYNC_ID||'',a:String(c.SESSION_INDEX||0)});})()", script);
    script->Release();
}

void startBrowser() {
    wstring folder = dataDir() + L"\\webview";
    auto* environmentReady = new Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
        IID_OF(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler), [](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment || !host) { done(false, Session()); return S_OK; }
            auto* controllerReady = new Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                IID_OF(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler), [](HRESULT hr2, ICoreWebView2Controller* made) -> HRESULT {
                    if (FAILED(hr2) || !made || !host) { done(false, Session()); return S_OK; }
                    controller = made;
                    controller->AddRef();
                    controller->get_CoreWebView2(&webview);
                    layout();
                    if (!webview) { done(false, Session()); return S_OK; }
                    auto* navigated = new Handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                        IID_OF(ICoreWebView2NavigationCompletedEventHandler), [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                            check();
                            return S_OK;
                        });
                    EventRegistrationToken token;
                    webview->add_NavigationCompleted(navigated, &token);
                    navigated->Release();
                    webview->Navigate(SignInUrl);
                    return S_OK;
                });
            environment->CreateCoreWebView2Controller(host, controllerReady);
            controllerReady->Release();
            return S_OK;
        });
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.c_str(), nullptr, environmentReady);
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
            RECT title = {(LONG)(14 * hostScale), 0, r.right - h, h};
            DrawTextW(dc, L"Sign in with Google", -1, &title, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            RECT cross = {r.right - h, 0, r.right, h};
            DrawTextW(dc, L"\x2715", -1, &cross, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            RECT waiting = {0, h, r.right, r.bottom};
            if (!controller) DrawTextW(dc, L"Opening Google...", -1, &waiting, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            SelectObject(dc, before);
            DeleteObject(font);
            EndPaint(hwnd, &ps);
            return 0;
        }
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

void signin::open(HWND owner, RECT over, std::function<void(bool, Session)> finished) {
    if (host) return;
    finish = std::move(finished);
    checking = false;
    WNDCLASSW cls = {};
    cls.lpfnWndProc = hostProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"WaveMiniSignIn";
    RegisterClassW(&cls);
    hostScale = std::max(1.0f, (over.right - over.left) / 420.0f);
    host = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, L"Sign in to WaveMini", WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                           over.left, over.top, over.right - over.left, over.bottom - over.top, owner, nullptr, cls.hInstance, nullptr);
    if (!host) { done(false, Session()); return; }
    SetForegroundWindow(host);
    startBrowser();
}

void signin::close() {
    finish = nullptr;
    teardown();
}

#else

// Built without WebView2 (a compile check on another system): there's no way to sign in.
void signin::open(HWND, RECT, std::function<void(bool, Session)> finished) {
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
