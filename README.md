# WaveMini

The Wave mini player as its own tiny Windows app: an ad-free YouTube Music client that lives entirely in one small window. No big app behind it: sign-in, Up next, lyrics, search, artists, albums, your library and account all fit inside the mini player.

Built in native Windows code to be small, fast and light on memory.

## Build on Windows

1. Install Visual Studio 2022 with **Desktop development with C++**, the **MSVC v143** toolset, and a Windows SDK.
2. Open `WaveMini.sln`. If Visual Studio does not restore automatically, right-click the solution and select **Restore NuGet Packages**.
3. Select **Release | x64** and build the solution. The executable is at `build/vs/x64/Release/WaveMini.exe`. Use **Debug | x64** to step through the source.

The project links the WebView2 loader statically, so the build has one EXE. Google sign-in and verification use the installed Microsoft Edge WebView2 Runtime. The existing CMake build also works with an unpacked WebView2 NuGet package passed as `-DWEBVIEW2_DIR=...`.

## Windows playback

WaveMini adjusts short leading edits in fragmented audio files before handing them to Windows MediaPlayer. The adjustment applies to new downloads and existing cached tracks. If Windows still ends a track early, WaveMini tries another audio source.

## Plan

- **Windows only** for now
- **Native**: C++ with Win32, Direct2D for the interface, and Windows MediaPlayer for audio
- **Light**: aiming for about 25 MB of memory while playing, and about 10 MB tucked away
- **Same look as WaveMini in [Wave](https://github.com/ReUnknown/Wave)**: the player on top, tabs for Up next, Lyrics, Search, Library and Account, with colors from the song's cover

## Related

- [Wave](https://github.com/ReUnknown/Wave): the full app for Android, Windows, macOS and Linux

## License

GPL-3.0, same as Wave.
