// WaveMini's state, and what the player, the interface and the window ask of each other.
#pragma once
#include "audio.h"
#include "core.h"
#include "gfx.h"
#include "yt.h"
#include <set>

#define WAVEMINI_VERSION "0.1.0"

enum class Tab { None, UpNext, Lyrics, Search, Library, Account };

struct LyricLine {
    double at = 0;       // seconds; only meaningful when the lyrics are synced
    string text;
    float plainH = 0, sungH = 0;   // measured heights
};

struct Settings {
    double volume = 1, rate = 1;
    bool saver = false;            // smallest audio instead of the best
    bool evenVolume = true, autoplay = true, onTop = true, startWithWindows = false;
    int corner = 3;                // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    Tab tab = Tab::UpNext;
};

// A page opened inside the Search or Library tab.
struct Page {
    enum Type { Artist, List, Items } type = Items;
    string id, title;
    bool loading = true, failed = false, loadingMore = false;
    ArtistPage artist;
    ListPage list;
    std::vector<Item> items;
    float scroll = 0;
};

struct App {
    Settings settings;
    Session session;
    bool hasSession = false;       // signed in, or chose to go without an account

    // What's playing.
    std::vector<Item> queue;
    int index = -1;
    string source, continuation, error;
    bool needsVerification = false;
    bool shuffle = false, loading = false, playing = false, wantPlay = false;
    int repeat = 0;                // 0 off, 1 all, 2 one
    int generation = 0;            // bumps with every song opened; stale answers are dropped
    bool fetchingMore = false;
    double loudnessGain = 1;
    Item last;                     // the last thing played, for the empty player
    bool hasLast = false;
    std::set<string> liked;

    // Lyrics of the current song.
    string lyricsFor;
    std::vector<LyricLine> lyrics;
    bool lyricsSynced = false, lyricsLoading = false, lyricsMeasured = false;
    float lyricsScroll = 0;

    // Search.
    wstring query;
    string searched;
    SearchFilter filter = SearchFilter::All;
    std::vector<Section> results;
    bool searching = false, searchFocus = false;
    std::vector<string> recent;
    float searchScroll = 0;
    std::vector<Page> searchPages;

    // Home (shown in the Search tab until you type).
    std::vector<Section> home;
    std::vector<float> homeX;      // how far each row is scrolled sideways
    string homeMore;
    bool homeLoaded = false, homeLoading = false;
    float homeScroll = 0;

    // Suggestions while typing.
    std::vector<string> suggestQueries;
    std::vector<Item> suggestItems;
    string suggestFor;
    long long suggestDue = 0;

    // Library.
    int libraryTab = 0;            // 0 playlists, 1 albums, 2 artists
    std::vector<Item> library[3];
    bool libraryLoaded[3] = {}, libraryLoading[3] = {};
    float libraryScroll = 0;
    std::vector<Page> libraryPages;

    float queueScroll = 0;

    // The song menu.
    bool menuOpen = false;
    int menuPage = 0;              // 0 the menu, 1 "add to playlist"
    float menuScroll = 0;
    long long sleepAt = 0;         // when to stop (tick count), 0 = off
    int sleepMinutes = 0;

    // The window.
    bool tucked = false, tuckRight = true;
    string toast;
    long long toastUntil = 0;
    bool signingIn = false;

    const Item* current() const { return index >= 0 && index < (int)queue.size() ? &queue[index] : nullptr; }
};

extern App app;

// ---------------------------------------------------------------- player.cpp

void loadState();
void saveState();
void saveSession();
void signedIn(const Session& s);   // a new session (or a guest): saves it and loads its data
void signOut();

void playSongs(const std::vector<Item>& songs, int index, const string& source, const string& continuation = "");
void playRadio(const Item& song);
void playItem(const Item& item, bool shuffled = false);   // a song, an album or a playlist
void playLast(bool shuffled);
void playIndex(int index, bool retryVerified = false);
void togglePlay();
void nextSong(bool byUser);
void previousSong();
void seekTo(double seconds);
void removeFromQueue(int index);
void toggleShuffle();
void cycleRepeat();
void toggleLike();
void applyVolume();
void setSleep(int minutes);
void onAudio(AudioEvent event, LPARAM token);
void tick();                        // a few times a second while the window is alive
void loadLyrics();
void runSearch();
void loadHome(bool more = false);
void loadSuggestions();
void loadLibrary(int which, bool force = false);
void openPage(std::vector<Page>& stack, Page::Type type, const string& id, const string& title, const string& params = "");
void openItem(std::vector<Page>& stack, const Item& item);
void showToast(const string& text);
void setStartWithWindows(bool on);
void checkForUpdates();
void copyText(const string& text);

// ---------------------------------------------------------------- ui.cpp

struct Size2 { float w, h; };
Size2 wantedSize();
// Draws the interface laid out at [w] x [h] into a window currently [shownW] x [shownH]
// (they differ while the window is changing shape).
void draw(float w, float h, float shownW, float shownH);
void uiFrame(float seconds);      // time since the last frame, for what's moving
bool uiAnimating();               // something is still moving: draw again next frame
void uiMouseDown(float x, float y);
void uiMouseMove(float x, float y);
void uiMouseUp(float x, float y);
void uiMouseLeave();
void uiWheel(float x, float y, float delta);
bool uiChar(wchar_t c);
bool uiKey(WPARAM key, bool ctrl);
// True when the point is on something that takes clicks (so it doesn't drag the window).
bool uiTakes(float x, float y);

// ---------------------------------------------------------------- main.cpp

void invalidate();
void windowTuck(bool tuck);
void windowClose();
void startSignIn();
void verifyPlayback();
void openSongInBrowser();
void applyOnTop();
