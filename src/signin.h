// Google sign-in, on Windows' own Edge WebView2. It only exists while you sign in: once
// YouTube Music has your session, the browser is closed again and its memory given back.
#pragma once
#include "yt.h"

namespace signin {
// With a video ID, opens the official song page for playback verification.
void open(HWND owner, RECT over, std::function<void(bool ok, Session session)> done,
          const string& videoId = "", bool requireAccount = false);
void close();
// Forgets Google's cookies kept by the sign-in window (after signing out).
void forget();
}
