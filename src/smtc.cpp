#ifdef __MINGW32__
// MinGW gives BYTE and boolean the same C++ type, so its two specializations clash.
#define ____FIReference_1_BYTE_INTERFACE_DEFINED__
#endif
#include "smtc.h"
#include <windows.foundation.h>
#include <windows.media.h>
#include <windows.storage.streams.h>
#include <systemmediatransportcontrolsinterop.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;
using namespace ABI::Windows::Media;
typedef ABI::Windows::Foundation::ITypedEventHandler<SystemMediaTransportControls*, SystemMediaTransportControlsButtonPressedEventArgs*> PressedHandler;

HWND notifyWindow;
UINT notifyMessage;
ComPtr<ISystemMediaTransportControls> controls;
EventRegistrationToken pressedToken;
string shownId;
int shownState = -1;

// A key or a button in Windows' media popup: passed on to the window's own thread.
struct Pressed : PressedHandler {
    LONG refs = 1;
    virtual ~Pressed() {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(PressedHandler) || id == __uuidof(IAgileObject)) {
            *out = static_cast<PressedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    STDMETHODIMP Invoke(ISystemMediaTransportControls*, ISystemMediaTransportControlsButtonPressedEventArgs* args) override {
        SystemMediaTransportControlsButton button = SystemMediaTransportControlsButton_Play;
        if (args && SUCCEEDED(args->get_Button(&button))) PostMessageW(notifyWindow, notifyMessage, (WPARAM)button, 0);
        return S_OK;
    }
};

struct Text {
    HSTRING value = nullptr;
    explicit Text(const string& utf8) { wstring w = W(utf8); WindowsCreateString(w.c_str(), (UINT32)w.size(), &value); }
    explicit Text(const wchar_t* wide) { WindowsCreateString(wide, (UINT32)wcslen(wide), &value); }
    ~Text() { if (value) WindowsDeleteString(value); }
};

template <typename T> HRESULT factory(const wchar_t* name, ComPtr<T>& out) {
    Text className(name);
    return RoGetActivationFactory(className.value, __uuidof(T), (void**)out.GetAddressOf());
}

}   // namespace

void smtc::start(HWND window, UINT message) {
    notifyWindow = window;
    notifyMessage = message;
    if (controls) return;
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    if (FAILED(factory(L"Windows.Media.SystemMediaTransportControls", interop))) return;
    if (FAILED(interop->GetForWindow(window, __uuidof(ISystemMediaTransportControls), (void**)controls.GetAddressOf())) || !controls) return;
    controls->put_IsEnabled(false);     // nothing to show until a song plays
    controls->put_IsPlayEnabled(true);
    controls->put_IsPauseEnabled(true);
    controls->put_IsNextEnabled(true);
    controls->put_IsPreviousEnabled(true);
    Pressed* handler = new Pressed();
    controls->add_ButtonPressed(handler, &pressedToken);
    handler->Release();
}

void smtc::update(const Item* song, bool playing) {
    if (!controls) return;
    int state = !song ? 0 : playing ? 1 : 2;
    string id = song ? song->id : string();
    if (state == shownState && id == shownId) return;
    bool newSong = id != shownId;
    shownState = state;
    shownId = id;
    controls->put_IsEnabled(song != nullptr);
    controls->put_PlaybackStatus(!song ? MediaPlaybackStatus_Closed : playing ? MediaPlaybackStatus_Playing : MediaPlaybackStatus_Paused);
    if (!song || !newSong) return;

    ComPtr<ISystemMediaTransportControlsDisplayUpdater> display;
    if (FAILED(controls->get_DisplayUpdater(display.GetAddressOf())) || !display) return;
    display->put_Type(MediaPlaybackType_Music);
    ComPtr<IMusicDisplayProperties> music;
    if (SUCCEEDED(display->get_MusicProperties(music.GetAddressOf())) && music) {
        Text title(song->title), artist(song->artistText());
        music->put_Title(title.value);
        music->put_Artist(artist.value);
    }
    // The cover, by its address: Windows fetches it itself.
    string art = artAt(song->art, 300);
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> uris;
    ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReferenceStatics> streams;
    if (!art.empty() && SUCCEEDED(factory(L"Windows.Foundation.Uri", uris)) && SUCCEEDED(factory(L"Windows.Storage.Streams.RandomAccessStreamReference", streams))) {
        Text address(art);
        ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
        ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReference> picture;
        if (SUCCEEDED(uris->CreateUri(address.value, uri.GetAddressOf())) && uri &&
            SUCCEEDED(streams->CreateFromUri(uri.Get(), picture.GetAddressOf())) && picture)
            display->put_Thumbnail(picture.Get());
    }
    display->Update();
}

void smtc::stop() {
    if (!controls) return;
    controls->remove_ButtonPressed(pressedToken);
    controls->put_IsEnabled(false);
    controls.Reset();
}
