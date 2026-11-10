#pragma once
#include <algorithm>

// A player can jump its reported position to the end on failure. Only accept an
// end signal after enough real playback time has elapsed since opening/seeking.
struct PlaybackClock {
    double seekPosition = 0;
    double played = 0;
    double rate = 1;
    long long startedAt = 0;
    bool running = false;

    double elapsed(long long now) const {
        return played + (running ? (std::max(0LL, now - startedAt) / 1000.0) * rate : 0);
    }

    void play(long long now) {
        if (!running) { startedAt = now; running = true; }
    }

    void pause(long long now) {
        played = elapsed(now);
        running = false;
    }

    void seek(double position, long long now) {
        seekPosition = position;
        played = 0;
        startedAt = now;
    }

    void speed(double value, long long now) {
        played = elapsed(now);
        startedAt = now;
        rate = value;
    }

    bool finished(double duration, long long now) const {
        if (duration <= 0 || seekPosition >= duration) return false;
        const double remaining = duration - seekPosition;
        return elapsed(now) >= remaining - std::min(0.75, remaining * 0.2);
    }
};
