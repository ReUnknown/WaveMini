// YouTube Music, spoken the way music.youtube.com speaks it, turned into WaveMini's small models.
#pragma once
#include "core.h"

struct Ref { string name, id; };

enum class Kind { Song, Album, Artist, Playlist };

struct Item {
    Kind kind = Kind::Song;
    string id;            // song: video id · album, artist: browse id · playlist: playlist id
    string title, art;
    string note;          // an extra grey line YouTube gave (subscribers, owner, song count)
    std::vector<Ref> artists;
    Ref album;
    string playlistId;    // an album's own playlist
    string year, setVideoId;
    long long durationMs = 0;
    bool video = false, episode = false, profile = false, podcast = false;

    string artistText() const;
    string subtitle(bool withKind = false) const;
};

struct Section {
    string title;
    std::vector<Item> items;
    string moreId, moreParams;
};

struct ListPage {          // an album or a playlist
    Item head;
    std::vector<Item> songs;
    string continuation;
    bool ok = false;
};

struct ArtistPage {
    Item artist;
    std::vector<Section> sections;
    string channelId, shuffleId, radioId;
    bool subscribed = false, ok = false;
};

struct NextResult {
    std::vector<Item> songs;
    string continuation, lyricsId;
    int index = 0;
};

enum class AudioClient : unsigned { None = 0, Vision = 1, Vr = 2, Music = 4, Android = 8 };

struct AudioPick {
    string url, userAgent, error;
    long long length = 0;
    double loudnessDb = 0;
    bool hasLoudness = false, needsVerification = false;
    AudioClient client = AudioClient::None;
};

struct Session {
    string cookie, visitorData, dataSyncId, authUser = "0", name, email, photo;
    bool guest = false;
    bool signedIn() const { return !guest && !cookie.empty(); }
};

enum class SearchFilter { All, Songs, Artists, Albums, Playlists };

namespace yt {
string watchUrl(const string& videoId);
void useSession(const Session& s);
Session session();
// Reads YouTube Music's current client version and a visitor id. Safe to call again.
void warmUp();

std::vector<Section> search(const string& query, SearchFilter filter);
// YouTube Music's home feed. [continuation] in: where to carry on from ("" = the start); out: the next one.
std::vector<Section> home(string& continuation);
struct Suggestions { std::vector<string> queries; std::vector<Item> items; };
Suggestions suggest(const string& input);
ListPage album(const string& browseId);
ListPage playlist(const string& playlistId);
std::vector<Item> playlistMore(string& continuation);
ArtistPage artist(const string& browseId);
std::vector<Item> browseItems(const string& browseId, const string& params);
NextResult next(const string& videoId, const string& playlistId = "", const string& continuation = "", const string& params = "");
string plainLyrics(const string& lyricsBrowseId);

std::vector<Item> libraryPlaylists();
std::vector<Item> libraryAlbums();
std::vector<Item> libraryArtists();
std::vector<Section> history();
bool accountInfo(Session& into);

bool like(const string& videoId, bool liked);
bool subscribe(const string& channelId, bool on);
bool addToPlaylist(const string& playlistId, const string& videoId);

// A playable audio link for a song. [saver] picks the smallest audio.
AudioPick audio(const string& videoId, bool saver, bool retryVerified = false, unsigned rejected = 0);
// Fetches the resolved AAC in verified segments. The sink receives media bytes only.
bool pullAudio(const string& url, const string& agent, long long length,
               const std::function<bool(const char*, size_t)>& sink, const std::atomic<bool>* cancel = nullptr);
// Counts the play on your account (history, recommendations).
void reportPlay(const string& videoId);
}

// A cover image address at [px] pixels (square).
string artAt(const string& url, int px);
