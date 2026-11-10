cmake_minimum_required(VERSION 3.20)
project(WaveMini CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No separate runtime to install: everything WaveMini needs is in the one file.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(WaveMini WIN32
    src/core.cpp src/yt.cpp src/audio.cpp src/mp4.cpp src/gfx.cpp src/player.cpp src/ui.cpp src/signin.cpp src/smtc.cpp src/install.cpp src/main.cpp
    res/app.rc)

target_compile_definitions(WaveMini PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
target_link_libraries(WaveMini PRIVATE
    d2d1 dwrite windowscodecs winhttp bcrypt crypt32 shlwapi shell32 ole32 oleaut32 uuid
    runtimeobject dwmapi user32 gdi32 advapi32)

# Google sign-in runs on Edge WebView2 (part of Windows 11). Point WEBVIEW2_DIR at the unpacked
# Microsoft.Web.WebView2 package; without it WaveMini builds with sign-in switched off.
if(WEBVIEW2_DIR)
    target_compile_definitions(WaveMini PRIVATE HAVE_WEBVIEW2)
    target_include_directories(WaveMini PRIVATE "${WEBVIEW2_DIR}/build/native/include")
    if(MSVC)
        target_link_libraries(WaveMini PRIVATE "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib" version)
    else()
        set(WEBVIEW2_LOADER "${WEBVIEW2_DIR}/build/native/x64/WebView2Loader.dll")
        configure_file(res/webview2.rc.in webview2.rc @ONLY)
        target_sources(WaveMini PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/webview2.rc")
        target_compile_definitions(WaveMini PRIVATE WAVEMINI_EMBEDDED_WEBVIEW2)
    endif()
endif()

if(MSVC)
    target_compile_options(WaveMini PRIVATE /W3 /O1 /GL /Gy /utf-8)
    target_link_options(WaveMini PRIVATE /LTCG /OPT:REF /OPT:ICF /ENTRY:wWinMainCRTStartup)
else()
    target_compile_options(WaveMini PRIVATE -Os -municode)
    target_link_options(WaveMini PRIVATE -municode -static -s)
endif()
