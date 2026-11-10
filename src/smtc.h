// Windows' own media controls: the play/pause/next/previous keys (from any app), the media
// popup by the volume slider and the lock screen, showing what WaveMini is playing.
#pragma once
#include "yt.h"

namespace smtc {
// [notify] gets [message] with wParam = the button: 0 play, 1 pause, 6 next, 7 previous.
void start(HWND window, UINT message);
// What's playing now (null = nothing) and whether it's playing or paused.
void update(const Item* song, bool playing);
void stop();
}
