// Playback: Windows' own media engine (audio only) fed by WaveMini's own download of the song.
#pragma once
#include "core.h"

enum class AudioEvent { Ready, Playing, Paused, Ended, Failed, DownloadFailed, Unavailable };

namespace audio {
// [notify] gets [message] with wParam = AudioEvent, lParam = the song's token.
bool start(HWND notify, UINT message);
void stop();

// Opens a song once the download has been checked and saved to the cache.
void open(const string& videoId, const string& url, const string& userAgent, long long length, LPARAM token, double expectedDuration);
void close();
void discard();
void play();
void pause();
void seek(double seconds);
double position();
double duration();
bool finished();
void observePlayback();
bool paused();
void setVolume(double volume);    // 0..1
void setRate(double rate);
// How much of the open song has been downloaded, 0..1.
double buffered();
}
