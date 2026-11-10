#pragma once
#ifdef __MINGW32__
// MinGW gives BYTE and boolean the same C++ type, so its two specializations clash.
#define ____FIReference_1_BYTE_INTERFACE_DEFINED__
#endif
#include <windows.media.playback.h>
#include <windows.storage.h>

// The cross compiler's SDK only projects the first MediaPlayer interfaces.
// These are the prefixes of the newer WinRT interfaces used for local playback.
// The unused methods preserve the slots published in Microsoft's Windows metadata.
namespace media_abi {

using TimeSpan = ABI::Windows::Foundation::TimeSpan;
using StorageFile = ABI::Windows::Storage::IStorageFile;
using FileOperation = ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>;

inline constexpr IID storageStaticsId{0x5984c710,0xdaf2,0x43c8,{0x8b,0xb4,0xa4,0xd3,0xea,0xcf,0xd0,0x3f}};
struct StorageStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetFileFromPathAsync(HSTRING, FileOperation**) = 0;
};

inline constexpr IID sourceStaticsId{0xf77d6fa4,0x4652,0x410e,{0xb1,0xd8,0xe9,0xa5,0xe2,0x45,0xa4,0x5c}};
struct SourceStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFromAdaptiveMediaSource(IInspectable*, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromMediaStreamSource(IInspectable*, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromMseStreamSource(IInspectable*, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromIMediaSource(IUnknown*, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromStorageFile(StorageFile*, IInspectable**) = 0;
};

inline constexpr IID playbackSourceId{0xef9dc2bc,0x9317,0x4696,{0xb0,0x51,0x2b,0xad,0x64,0x31,0x77,0xb5}};
inline constexpr IID playerSourceId{0x82449b9f,0x7322,0x4c0b,{0xb0,0x3b,0x3e,0x69,0xa4,0x82,0x60,0xc5}};
struct PlayerSource : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Source(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Source(IInspectable*) = 0;
};

struct CommandManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean) = 0;
};

struct Session : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackStateChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackStateChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackRateChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackRateChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SeekCompleted(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SeekCompleted(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_BufferingStarted(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_BufferingStarted(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_BufferingEnded(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_BufferingEnded(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_BufferingProgressChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_BufferingProgressChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_DownloadProgressChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_DownloadProgressChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NaturalDurationChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NaturalDurationChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PositionChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PositionChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NaturalVideoSizeChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NaturalVideoSizeChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MediaPlayer(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_NaturalDuration(TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Position(TimeSpan) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackState(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanSeek(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CanPause(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsProtected(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(double*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackRate(double) = 0;
};

inline constexpr IID player3Id{0xee0660da,0x031b,0x4feb,{0xbd,0x9b,0x92,0xe0,0xa0,0xa8,0xd2,0x99}};
struct Player3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_IsMutedChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_IsMutedChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AudioBalance(double*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AudioBalance(double) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RealTimePlayback(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RealTimePlayback(boolean) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_StereoscopicVideoRenderMode(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StereoscopicVideoRenderMode(int) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BreakManager(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CommandManager(CommandManager**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AudioDevice(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AudioDevice(IInspectable*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TimelineController(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TimelineController(IInspectable*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TimelineControllerPositionOffset(TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TimelineControllerPositionOffset(TimeSpan) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackSession(Session**) = 0;
};

} // namespace media_abi
