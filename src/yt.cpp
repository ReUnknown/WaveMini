#include "yt.h"

static const char* Origin = "https://music.youtube.com";
static const char* BrowserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36";

string yt::watchUrl(const string& videoId) { return string(Origin) + "/watch?v=" + urlEncode(videoId); }

static std::mutex gate;
static Session current;
static string clientVersion = "1.20261020.10.00";
static bool warmed = false;

// ---------------------------------------------------------------- Small text checks

static bool isYear(const string& t) {
    return t.size() == 4 && isdigit((unsigned char)t[0]) && isdigit((unsigned char)t[1]) && isdigit((unsigned char)t[2]) && isdigit((unsigned char)t[3]);
}

static bool isDuration(const string& t) {
    if (t.empty() || !isdigit((unsigned char)t[0]) || !contains(t, ":")) return false;
    for (char c : t) if (!isdigit((unsigned char)c) && c != ':') return false;
    return true;
}

static bool isCount(const string& text) {
    string t = lower(text);
    return contains(t, " plays") || contains(t, " views") || contains(t, " subscribers") || contains(t, "monthly audience") || contains(t, " songs") || contains(t, " likes");
}

static bool isTypeWord(const string& text) {
    static const char* words[] = {"song", "video", "episode", "album", "single", "ep", "playlist", "artist", "podcast", "profile", "station", "audiobook"};
    string t = lower(text);
    for (auto* w : words) if (t == w) return true;
    return false;
}

static long long parseDuration(const string& text) {
    if (!isDuration(text)) return 0;
    long long total = 0;
    for (auto& part : split(trim(text), ":")) total = total * 60 + atoll(part.c_str());
    return total * 1000;
}

string Item::artistText() const {
    string out;
    for (auto& a : artists) { if (!out.empty()) out += ", "; out += a.name; }
    return out;
}

string Item::subtitle(bool withKind) const {
    auto join = [](std::initializer_list<string> parts) {
        string out;
        for (auto& p : parts) if (!p.empty()) { if (!out.empty()) out += " \xC2\xB7 "; out += p; }
        return out;
    };
    switch (kind) {
        case Kind::Song: return join({withKind ? (video ? "Video" : "Song") : "", artistText(), withKind && durationMs ? formatTime(durationMs / 1000.0) : ""});
        case Kind::Album: return join({"Album", artistText(), year});
        case Kind::Artist: return join({profile ? "Profile" : "Artist", note});
        default: return join({podcast ? "Podcast" : "Playlist", note});
    }
}

string artAt(const string& url, int px) {
    if (url.empty()) return url;
    // Google's image servers size by a suffix: =w120-h120-...
    size_t eq = url.rfind("=w");
    if (eq != string::npos && url.find('/', eq) == string::npos) {
        char b[48];
        wsprintfA(b, "=w%d-h%d-l90-rj", px, px);
        return url.substr(0, eq) + b;
    }
    eq = url.rfind("=s");
    if (eq != string::npos && url.find('/', eq) == string::npos) {
        char b[32];
        wsprintfA(b, "=s%d", px);
        return url.substr(0, eq) + b;
    }
    return url;
}

// ---------------------------------------------------------------- Requests

void yt::useSession(const Session& s) {
    std::lock_guard<std::mutex> lock(gate);
    string visitor = current.visitorData;
    current = s;
    if (current.visitorData.empty()) current.visitorData = visitor;
    size_t bar = current.dataSyncId.find("||");
    if (bar != string::npos) current.dataSyncId = current.dataSyncId.substr(0, bar);
}

Session yt::session() {
    std::lock_guard<std::mutex> lock(gate);
    return current;
}

static string cookieValue(const string& cookie, const char* name) {
    for (auto& part : split(cookie, ";")) {
        string p = trim(part);
        size_t n = strlen(name);
        if (p.size() > n && p.compare(0, n, name) == 0 && p[n] == '=') return p.substr(n + 1);
    }
    return string();
}

static Headers headersFor(const Session& s, const string& version) {
    Headers h = {
        {"User-Agent", BrowserAgent}, {"Origin", Origin}, {"Referer", string(Origin) + "/"}, {"X-Origin", Origin},
        {"X-Youtube-Client-Name", "67"}, {"X-Youtube-Client-Version", version}, {"Accept-Language", "en"},
        {"Content-Type", "application/json"},
    };
    if (!s.visitorData.empty()) h.push_back({"X-Goog-Visitor-Id", s.visitorData});
    if (!s.cookie.empty()) h.push_back({"Cookie", s.cookie});
    if (s.signedIn()) {
        string sapisid = cookieValue(s.cookie, "SAPISID");
        if (sapisid.empty()) sapisid = cookieValue(s.cookie, "__Secure-3PAPISID");
        if (!sapisid.empty()) {
            string now = std::to_string((long long)time(nullptr));
            h.push_back({"Authorization", "SAPISIDHASH " + now + "_" + sha1Hex(now + " " + sapisid + " " + Origin)});
            h.push_back({"X-Goog-AuthUser", s.authUser.empty() ? "0" : s.authUser});
        }
    }
    return h;
}

static string between(const string& text, const string& before, char until) {
    size_t at = text.find(before);
    if (at == string::npos) return string();
    at += before.size();
    size_t end = text.find(until, at);
    return end == string::npos ? string() : text.substr(at, end - at);
}

void yt::warmUp() {
    Session s = session();
    string version;
    { std::lock_guard<std::mutex> lock(gate); if (warmed && !s.visitorData.empty()) return; version = clientVersion; }
    Headers h = {{"User-Agent", BrowserAgent}, {"Accept-Language", "en"}};
    if (!s.cookie.empty()) h.push_back({"Cookie", s.cookie});
    HttpRes page = http("GET", string(Origin) + "/", h);
    if (!page.ok()) return;
    string v = between(page.body, "\"INNERTUBE_CLIENT_VERSION\":\"", '"');
    string visitor = between(page.body, "\"VISITOR_DATA\":\"", '"');
    std::lock_guard<std::mutex> lock(gate);
    if (!v.empty()) clientVersion = v;
    if (current.visitorData.empty() && !visitor.empty()) current.visitorData = visitor;
    warmed = true;
}

// POSTs to youtubei/v1/[endpoint]; [fields] is the body's own members ("browseId":"x").
static Json post(const string& endpoint, const string& fields, const string& query = "") {
    yt::warmUp();
    Session s;
    string version;
    { std::lock_guard<std::mutex> lock(gate); s = current; version = clientVersion; }
    string body = "{\"context\":{\"client\":{\"clientName\":\"WEB_REMIX\",\"clientVersion\":" + jq(version) + ",\"hl\":\"en\",\"gl\":\"US\"";
    if (!s.visitorData.empty()) body += ",\"visitorData\":" + jq(s.visitorData);
    body += "}";
    if (s.signedIn() && !s.dataSyncId.empty()) body += ",\"user\":{\"onBehalfOfUser\":" + jq(s.dataSyncId) + "}";
    body += "}";
    if (!fields.empty()) body += "," + fields;
    body += "}";
    string url = string(Origin) + "/youtubei/v1/" + endpoint + "?prettyPrint=false" + (query.empty() ? "" : "&" + query);
    for (int attempt = 0; attempt < 3; attempt++) {
        HttpRes res = http("POST", url, headersFor(s, version), body);
        if (res.status >= 500 || res.status == 0) { Sleep(400 * (attempt + 1)); continue; }
        if (!res.ok()) return Json();
        return Json::parse(res.body);
    }
    return Json();
}

static string field(const char* name, const string& value) { return "\"" + string(name) + "\":" + jq(value); }

// ---------------------------------------------------------------- Items

static std::vector<std::vector<const Json*>> groupsOf(const std::vector<const Json*>& runs) {
    std::vector<std::vector<const Json*>> groups(1);
    for (auto* run : runs) {
        string t = (*run)["text"].str();
        if (t == " \xE2\x80\xA2 " || t == "\xE2\x80\xA2") groups.emplace_back();
        else groups.back().push_back(run);
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const std::vector<const Json*>& g) { return g.empty(); }), groups.end());
    return groups;
}

static string groupText(const std::vector<const Json*>& group) {
    string out;
    for (auto* r : group) out += (*r)["text"].str();
    return out;
}

static std::vector<const Json*> runsOf(const Json& holder) {
    std::vector<const Json*> out;
    for (auto& r : holder["runs"].each()) out.push_back(&r);
    return out;
}

static std::vector<Ref> artistsFrom(const std::vector<const Json*>& runs) {
    std::vector<Ref> linked;
    for (auto* r : runs) {
        string type = (*r)["navigationEndpoint"].pageType();
        if (type == "MUSIC_PAGE_TYPE_ARTIST" || type == "MUSIC_PAGE_TYPE_USER_CHANNEL")
            linked.push_back({(*r)["text"].str(), r->at("navigationEndpoint", "browseEndpoint", "browseId").str()});
    }
    if (!linked.empty()) return linked;
    // Unlinked credits: the first group that isn't a type word, a year, a count or a time.
    for (auto& group : groupsOf(runs)) {
        string text = groupText(group);
        if (isTypeWord(text) || isYear(text) || isCount(text) || isDuration(text)) continue;
        size_t at = 0;
        for (;;) {
            size_t comma = text.find(", ", at), amp = text.find(" & ", at);
            size_t cut = std::min(comma, amp);
            string name = trim(text.substr(at, cut == string::npos ? string::npos : cut - at));
            if (!name.empty()) linked.push_back({name, ""});
            if (cut == string::npos) break;
            at = cut + (cut == comma ? 2 : 3);
        }
        break;
    }
    return linked;
}

static Ref albumFrom(const std::vector<const Json*>& runs) {
    for (auto* r : runs)
        if ((*r)["navigationEndpoint"].pageType() == "MUSIC_PAGE_TYPE_ALBUM")
            return {(*r)["text"].str(), r->at("navigationEndpoint", "browseEndpoint", "browseId").str()};
    return Ref();
}

static string lastMatching(const std::vector<const Json*>& runs, bool (*test)(const string&)) {
    string out;
    for (auto* r : runs) { string t = (*r)["text"].str(); if (test(t)) out = t; }
    return out;
}

static void videoType(const Json& watch, Item& item) {
    string type = watch.at("watchEndpointMusicSupportedConfigs", "watchEndpointMusicConfig", "musicVideoType").str();
    item.video = type == "MUSIC_VIDEO_TYPE_UGC" || type == "MUSIC_VIDEO_TYPE_OMV";
    item.episode = type == "MUSIC_VIDEO_TYPE_PODCAST_EPISODE";
}

// The grey line of a playlist or artist: its groups minus the kind word.
static string noteFrom(const std::vector<const Json*>& runs) {
    string out;
    for (auto& g : groupsOf(runs)) {
        string t = groupText(g);
        if (isTypeWord(t)) continue;
        if (!out.empty()) out += " \xC2\xB7 ";
        out += t;
    }
    return out;
}

static bool byPageType(const string& pageType, const string& browseId, const std::vector<const Json*>& runs, Item& item) {
    item.id = browseId;
    if (pageType == "MUSIC_PAGE_TYPE_ALBUM") {
        item.kind = Kind::Album;
        item.artists = artistsFrom(runs);
        item.year = lastMatching(runs, isYear);
    } else if (pageType == "MUSIC_PAGE_TYPE_ARTIST" || pageType == "MUSIC_PAGE_TYPE_USER_CHANNEL") {
        item.kind = Kind::Artist;
        item.profile = pageType == "MUSIC_PAGE_TYPE_USER_CHANNEL";
        item.note = noteFrom(runs);
    } else if (pageType == "MUSIC_PAGE_TYPE_PLAYLIST") {
        item.kind = Kind::Playlist;
        if (startsWith(item.id, "VL")) item.id = item.id.substr(2);
        item.note = noteFrom(runs);
    } else if (pageType == "MUSIC_PAGE_TYPE_PODCAST_SHOW_PAGE") {
        item.kind = Kind::Playlist;
        item.podcast = true;
    } else return false;
    return !item.id.empty();
}

static bool twoRow(const Json& r, Item& item) {
    item.title = r["title"].text();
    if (item.title.empty()) return false;
    item.art = r["thumbnailRenderer"].thumb();
    const Json& nav = r["navigationEndpoint"];
    auto runs = runsOf(r["subtitle"]);
    string videoId = nav.at("watchEndpoint", "videoId").str();
    if (!videoId.empty()) {
        item.kind = Kind::Song;
        item.id = videoId;
        item.artists = artistsFrom(runs);
        item.album = albumFrom(runs);
        videoType(nav["watchEndpoint"], item);
        return true;
    }
    if (!byPageType(nav.pageType(), nav.at("browseEndpoint", "browseId").str(), runs, item)) return false;
    if (item.kind == Kind::Album)
        item.playlistId = r.at("thumbnailOverlay", "musicItemThumbnailOverlayRenderer", "content", "musicPlayButtonRenderer", "playNavigationEndpoint", "watchPlaylistEndpoint", "playlistId").str();
    return true;
}

static bool row(const Json& r, Item& item) {
    std::vector<const Json*> columns;
    for (auto& c : r["flexColumns"].each()) columns.push_back(&c.at("musicResponsiveListItemFlexColumnRenderer", "text"));
    if (columns.empty()) return false;
    item.title = columns[0]->text();
    if (item.title.empty()) return false;
    item.art = r["thumbnail"].thumb();
    std::vector<const Json*> rest;
    for (size_t i = 1; i < columns.size(); i++) for (auto& run : (*columns[i])["runs"].each()) rest.push_back(&run);
    const Json& nav = r["navigationEndpoint"];
    string pageType = nav.pageType();
    const Json* watch = &r.at("overlay", "musicItemThumbnailOverlayRenderer", "content", "musicPlayButtonRenderer", "playNavigationEndpoint", "watchEndpoint");
    if (!watch->has()) watch = &columns[0]->at("runs", 0, "navigationEndpoint", "watchEndpoint");
    string videoId = r.at("playlistItemData", "videoId").str();
    if (videoId.empty()) videoId = (*watch)["videoId"].str();
    if (!videoId.empty() && pageType.empty()) {
        item.kind = Kind::Song;
        item.id = videoId;
        item.artists = artistsFrom(rest);
        item.album = albumFrom(rest);
        string time = r.at("fixedColumns", 0, "musicResponsiveListItemFixedColumnRenderer", "text").text();
        if (time.empty()) time = lastMatching(rest, isDuration);
        item.durationMs = parseDuration(time);
        item.setVideoId = r.at("playlistItemData", "playlistSetVideoId").str();
        videoType(*watch, item);
        return true;
    }
    return byPageType(pageType, nav.at("browseEndpoint", "browseId").str(), rest, item);
}

static bool panelSong(const Json& r, Item& item) {
    item.kind = Kind::Song;
    item.id = r["videoId"].str();
    item.title = r["title"].text();
    if (item.id.empty() || item.title.empty()) return false;
    auto byline = runsOf(r["longBylineText"]);
    if (byline.empty()) byline = runsOf(r["shortBylineText"]);
    item.artists = artistsFrom(byline);
    item.album = albumFrom(byline);
    item.art = r["thumbnail"].thumb();
    item.durationMs = parseDuration(r["lengthText"].text());
    videoType(r.at("navigationEndpoint", "watchEndpoint"), item);
    return true;
}

static bool parseItem(const Json& node, Item& item) {
    if (node["musicTwoRowItemRenderer"].has()) return twoRow(node["musicTwoRowItemRenderer"], item);
    if (node["musicResponsiveListItemRenderer"].has()) return row(node["musicResponsiveListItemRenderer"], item);
    if (node["playlistPanelVideoRenderer"].has()) return panelSong(node["playlistPanelVideoRenderer"], item);
    const Json& wrapped = node.at("playlistPanelVideoWrapperRenderer", "primaryRenderer", "playlistPanelVideoRenderer");
    if (wrapped.has()) return panelSong(wrapped, item);
    return false;
}

static std::vector<Item> itemsOf(const Json& list) {
    std::vector<Item> out;
    for (auto& node : list.each()) { Item item; if (parseItem(node, item)) out.push_back(std::move(item)); }
    return out;
}

// A carousel, shelf or grid as a Section.
static bool carousel(const Json& node, Section& section) {
    const Json* shelf = &node["musicCarouselShelfRenderer"];
    if (!shelf->has()) shelf = &node["musicImmersiveCarouselShelfRenderer"];
    if (shelf->has()) {
        const Json* header = &shelf->at("header", "musicCarouselShelfBasicHeaderRenderer");
        if (!header->has()) header = &shelf->at("header", "musicImmersiveCarouselShelfBasicHeaderRenderer");
        section.title = (*header)["title"].text();
        section.items = itemsOf((*shelf)["contents"]);
        const Json& more = header->at("moreContentButton", "buttonRenderer", "navigationEndpoint", "browseEndpoint");
        section.moreId = more["browseId"].str();
        section.moreParams = more["params"].str();
        return !section.title.empty() && !section.items.empty();
    }
    const Json& music = node["musicShelfRenderer"];
    if (music.has()) {
        section.title = music["title"].text();
        section.items = itemsOf(music["contents"]);
        const Json* more = &music.at("bottomEndpoint", "browseEndpoint");
        if (!more->has()) more = &music.at("title", "runs", 0, "navigationEndpoint", "browseEndpoint");
        section.moreId = (*more)["browseId"].str();
        section.moreParams = (*more)["params"].str();
        return !section.items.empty();
    }
    const Json& grid = node["gridRenderer"];
    if (grid.has()) {
        section.title = grid.at("header", "gridHeaderRenderer", "title").text();
        section.items = itemsOf(grid["items"]);
        return !section.items.empty();
    }
    return false;
}

// ---------------------------------------------------------------- Search

static bool topResult(const Json& card, Item& item) {
    const Json& titleRun = card.at("title", "runs", 0);
    item.title = titleRun["text"].str();
    const Json* nav = &titleRun["navigationEndpoint"];
    if (!nav->has()) nav = &card["onTap"];
    if (item.title.empty() || !nav->has()) return false;
    item.art = card["thumbnail"].thumb();
    auto runs = runsOf(card["subtitle"]);
    string videoId = nav->at("watchEndpoint", "videoId").str();
    if (!videoId.empty()) {
        item.kind = Kind::Song;
        item.id = videoId;
        item.artists = artistsFrom(runs);
        item.album = albumFrom(runs);
        item.durationMs = parseDuration(lastMatching(runs, isDuration));
        videoType((*nav)["watchEndpoint"], item);
        return true;
    }
    return byPageType(nav->pageType(), nav->at("browseEndpoint", "browseId").str(), runs, item);
}

std::vector<Section> yt::search(const string& query, SearchFilter filter) {
    string fields = field("query", query);
    const char* params = nullptr;
    switch (filter) {
        case SearchFilter::Songs: params = "EgWKAQIIAWoKEAkQBRAKEAMQBA=="; break;
        case SearchFilter::Albums: params = "EgWKAQIYAWoKEAkQChAFEAMQBA=="; break;
        case SearchFilter::Artists: params = "EgWKAQIgAWoKEAkQChAFEAMQBA=="; break;
        case SearchFilter::Playlists: params = "EgeKAQQoADgBagwQDhAKEAMQBRAJEAQ="; break;
        default: break;
    }
    if (params) fields += "," + field("params", params);
    Json root = post("search", fields);
    const Json& contents = root.at("contents", "tabbedSearchResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents");
    std::vector<Section> sections;
    std::vector<std::pair<string, Item>> loose;
    for (auto& node : contents.each()) {
        const Json& card = node["musicCardShelfRenderer"];
        const Json& itemSection = node["itemSectionRenderer"];
        Section section;
        if (card.has()) {
            Item top;
            bool hasTop = topResult(card, top);
            if (hasTop) section.items.push_back(top);
            for (auto& item : itemsOf(card["contents"])) {
                // Songs listed under an artist's card don't repeat the artist's name.
                if (item.kind == Kind::Song && item.artists.empty() && hasTop && top.kind == Kind::Artist) item.artists.push_back({top.title, top.id});
                section.items.push_back(item);
            }
            section.title = "Top result";
            if (!section.items.empty()) sections.push_back(std::move(section));
        } else if (itemSection.has()) {
            for (auto& item : itemsOf(itemSection["contents"])) {
                const char* group = item.kind == Kind::Song ? (item.video ? "Videos" : item.episode ? "Episodes" : "Songs")
                    : item.kind == Kind::Album ? "Albums" : item.kind == Kind::Artist ? (item.profile ? "Profiles" : "Artists")
                    : item.podcast ? "Podcasts" : "Playlists";
                loose.emplace_back(group, item);
            }
        } else if (carousel(node, section)) {
            sections.push_back(std::move(section));
        }
    }
    for (auto& entry : loose) {
        auto found = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.title == entry.first; });
        if (found == sections.end()) { sections.push_back(Section{entry.first, {}, "", ""}); found = sections.end() - 1; }
        found->items.push_back(entry.second);
    }
    return sections;
}

// ---------------------------------------------------------------- Pages

static const Json& headerOf(const Json& root) {
    const Json& first = root.at("contents", "twoColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents", 0);
    const Json& plain = first["musicResponsiveHeaderRenderer"];
    return plain.has() ? plain : first.at("musicEditablePlaylistDetailHeaderRenderer", "header", "musicResponsiveHeaderRenderer");
}

static const Json& secondary(const Json& root) {
    return root.at("contents", "twoColumnBrowseResultsRenderer", "secondaryContents", "sectionListRenderer");
}

ListPage yt::album(const string& browseId) {
    ListPage page;
    Json root = post("browse", field("browseId", browseId));
    const Json& header = headerOf(root);
    page.head.kind = Kind::Album;
    page.head.id = browseId;
    page.head.title = header["title"].text();
    page.head.artists = artistsFrom(runsOf(header["straplineTextOne"]));
    page.head.year = lastMatching(runsOf(header["subtitle"]), isYear);
    page.head.art = header["thumbnail"].thumb();
    string canonical = root.at("microformat", "microformatDataRenderer", "urlCanonical").str();
    size_t list = canonical.find("list=");
    if (list != string::npos) page.head.playlistId = canonical.substr(list + 5);
    for (auto& song : itemsOf(secondary(root).at("contents", 0, "musicShelfRenderer", "contents"))) {
        if (song.kind != Kind::Song) continue;
        // Album tracks come without art or (often) artists: they're the album's.
        Item s = song;
        if (s.art.empty()) s.art = page.head.art;
        if (s.artists.empty()) s.artists = page.head.artists;
        s.album = {page.head.title, browseId};
        page.songs.push_back(std::move(s));
    }
    page.ok = !page.head.title.empty();
    return page;
}

static void playlistItems(const Json& contents, std::vector<Item>& songs, string& continuation) {
    for (auto& node : contents.each()) {
        string token = node.at("continuationItemRenderer", "continuationEndpoint", "continuationCommand", "token").str();
        Item item;
        if (!token.empty()) continuation = token;
        else if (parseItem(node, item) && item.kind == Kind::Song) songs.push_back(std::move(item));
    }
}

ListPage yt::playlist(const string& playlistId) {
    ListPage page;
    string id = startsWith(playlistId, "VL") ? playlistId.substr(2) : playlistId;
    Json root = post("browse", field("browseId", "VL" + id));
    const Json& header = headerOf(root);
    page.head.kind = Kind::Playlist;
    page.head.id = id;
    page.head.title = header["title"].text();
    if (page.head.title.empty() && id == "LM") page.head.title = "Liked songs";
    page.head.note = header["straplineTextOne"].text();
    page.head.art = header["thumbnail"].thumb();
    const Json& shelf = secondary(root).at("contents", 0, "musicPlaylistShelfRenderer");
    playlistItems(shelf["contents"], page.songs, page.continuation);
    if (page.continuation.empty()) page.continuation = shelf.at("continuations", 0, "nextContinuationData", "continuation").str();
    page.ok = root.has();
    return page;
}

std::vector<Item> yt::playlistMore(string& continuation) {
    std::vector<Item> songs;
    Json root = post("browse", field("continuation", continuation));
    continuation.clear();
    const Json* items = &root.at("onResponseReceivedActions", 0, "appendContinuationItemsAction", "continuationItems");
    if (!items->has()) items = &root.at("continuationContents", "musicPlaylistShelfContinuation", "contents");
    playlistItems(*items, songs, continuation);
    if (continuation.empty()) continuation = root.at("continuationContents", "musicPlaylistShelfContinuation", "continuations", 0, "nextContinuationData", "continuation").str();
    return songs;
}

ArtistPage yt::artist(const string& browseId) {
    ArtistPage page;
    Json root = post("browse", field("browseId", browseId));
    const Json* header = &root.at("header", "musicImmersiveHeaderRenderer");
    if (!header->has()) header = &root.at("header", "musicVisualHeaderRenderer");
    if (!header->has()) header = &root.at("header", "musicHeaderRenderer");
    page.artist.kind = Kind::Artist;
    page.artist.id = browseId;
    page.artist.title = (*header)["title"].text();
    page.artist.art = (*header)["thumbnail"].thumb();
    if (page.artist.art.empty()) page.artist.art = (*header)["foregroundThumbnail"].thumb();
    const Json& sub = header->at("subscriptionButton", "subscribeButtonRenderer");
    page.artist.note = sub["longSubscriberCountText"].text();
    if (page.artist.note.empty()) page.artist.note = sub["subscriberCountText"].text();
    page.channelId = sub["channelId"].str();
    if (page.channelId.empty()) page.channelId = browseId;
    page.subscribed = sub["subscribed"].truthy();
    page.shuffleId = header->at("playButton", "buttonRenderer", "navigationEndpoint", "watchEndpoint", "playlistId").str();
    page.radioId = header->at("startRadioButton", "buttonRenderer", "navigationEndpoint", "watchEndpoint", "playlistId").str();
    if (page.radioId.empty()) page.radioId = header->at("startRadioButton", "buttonRenderer", "navigationEndpoint", "watchPlaylistEndpoint", "playlistId").str();
    for (auto& node : root.at("contents", "singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents").each()) {
        Section section;
        if (carousel(node, section)) page.sections.push_back(std::move(section));
    }
    page.ok = !page.artist.title.empty();
    return page;
}

std::vector<Item> yt::browseItems(const string& browseId, const string& params) {
    string fields = field("browseId", browseId);
    if (!params.empty()) fields += "," + field("params", params);
    Json root = post("browse", fields);
    Section section;
    carousel(root.at("contents", "singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents", 0), section);
    return section.items;
}

// ---------------------------------------------------------------- Playback

NextResult yt::next(const string& videoId, const string& playlistId, const string& continuation, const string& params) {
    NextResult result;
    string fields = "\"isAudioOnly\":true,\"enablePersistentPlaylistPanel\":true,\"tunerSettingValue\":\"AUTOMIX_SETTING_NORMAL\"";
    if (!videoId.empty()) fields += "," + field("videoId", videoId);
    if (!playlistId.empty()) fields += "," + field("playlistId", playlistId);
    if (!params.empty()) fields += "," + field("params", params);
    if (!continuation.empty()) fields += "," + field("continuation", continuation);
    Json root = post("next", fields);
    auto continuationOf = [](const Json& panel) {
        string c = panel.at("continuations", 0, "nextRadioContinuationData", "continuation").str();
        return c.empty() ? panel.at("continuations", 0, "nextContinuationData", "continuation").str() : c;
    };
    if (!continuation.empty()) {
        const Json& more = root.at("continuationContents", "playlistPanelContinuation");
        result.songs = itemsOf(more["contents"]);
        result.continuation = continuationOf(more);
        return result;
    }
    const Json& tabs = root.at("contents", "singleColumnMusicWatchNextResultsRenderer", "tabbedRenderer", "watchNextTabbedResultsRenderer", "tabs");
    const Json& panel = tabs.at(0, "tabRenderer", "content", "musicQueueRenderer", "content", "playlistPanelRenderer");
    result.songs = itemsOf(panel["contents"]);
    result.continuation = continuationOf(panel);
    result.lyricsId = tabs.at(1, "tabRenderer", "endpoint", "browseEndpoint", "browseId").str();
    for (size_t i = 0; i < result.songs.size(); i++) if (result.songs[i].id == videoId) { result.index = (int)i; break; }
    return result;
}

string yt::plainLyrics(const string& lyricsBrowseId) {
    if (lyricsBrowseId.empty()) return string();
    Json root = post("browse", field("browseId", lyricsBrowseId));
    return root.at("contents", "sectionListRenderer", "contents", 0, "musicDescriptionShelfRenderer", "description").text();
}

static string rangeUrl(string url, const string& bounds) {
    size_t key = url.find("?range=");
    if (key == string::npos) key = url.find("&range=");
    if (key == string::npos) {
        url += (url.find('?') == string::npos ? "?range=" : "&range=") + bounds;
    } else {
        size_t start = key + 7;
        size_t end = url.find('&', start);
        url.replace(start, end == string::npos ? string::npos : end - start, bounds);
    }
    return url;
}

static string audioRange(const string& url, const string& agent, long long length,
                         long long start, long long end, const std::atomic<bool>* cancel = nullptr) {
    string bounds = std::to_string(start) + "-" + std::to_string(end);
    string expected = "bytes " + bounds + "/" + std::to_string(length);
    size_t size = (size_t)(end - start + 1);
    string bytes;
    bytes.reserve(size);
    bool oversized = false;
    auto collect = [&](const char* data, size_t count) {
        if (count > size - bytes.size()) {
            oversized = true;
            return false;
        }
        bytes.append(data, count);
        return true;
    };

    // YoutubeExplode uses the CDN's range query. It commonly returns 200 for just that segment.
    Headers headers = {{"User-Agent", agent}};
    HttpRes res = http("GET", rangeUrl(url, bounds), headers, "", collect, cancel);
    if (!oversized && bytes.size() == size &&
        ((res.status == 206 && res.contentRange == expected) ||
         (res.status == 200 && (res.contentRange.empty() || res.contentRange == expected))))
        return bytes;
    if (cancel && cancel->load()) return "";

    // Some URLs support the regular HTTP range header instead.
    bytes.clear();
    oversized = false;
    headers.emplace_back("Range", "bytes=" + bounds);
    res = http("GET", url, headers, "", collect, cancel);
    if (!oversized && bytes.size() == size &&
        ((res.status == 206 && res.contentRange == expected) ||
         (res.status == 200 && start == 0 && end == length - 1 && res.contentLength == length)))
        return bytes;
    return "";
}

bool yt::pullAudio(const string& url, const string& agent, long long length,
                   const std::function<bool(const char*, size_t)>& sink, const std::atomic<bool>* cancel) {
    if (length < 8 || !sink) return false;
    constexpr long long piece = 1024 * 1024;
    for (long long start = 0; start < length;) {
        long long end = start + std::min(piece, length - start) - 1;
        string bytes;
        for (int attempt = 0; attempt < 3 && !(cancel && cancel->load()); ++attempt) {
            bytes = audioRange(url, agent, length, start, end, cancel);
            if (!bytes.empty()) break;
        }
        if ((cancel && cancel->load()) || bytes.empty() ||
            (start == 0 && bytes.compare(4, 4, "ftyp") != 0) ||
            !sink(bytes.data(), bytes.size()))
            return false;
        start = end + 1;
    }
    return true;
}

static AudioPick pickAudio(const Json& root, const string& videoId, const string& agent, bool saver) {
    AudioPick pick;
    pick.userAgent = agent;
    string resolved = root.at("videoDetails", "videoId").str();
    if (!resolved.empty() && resolved != videoId) {
        pick.error = "YouTube returned a different song";
        return pick;
    }
    const Json& status = root["playabilityStatus"];
    if (status["status"].str() == "OK") {
        std::vector<const Json*> formats;
        for (auto& f : root.at("streamingData", "adaptiveFormats").each()) {
            const Json& original = f.at("audioTrack", "audioIsDefault");
            if (original.has() && !original.truthy()) continue;
            if (startsWith(f["mimeType"].str(), "audio/mp4") && startsWith(f["url"].str(), "https://"))
                formats.push_back(&f);
        }
        std::stable_sort(formats.begin(), formats.end(), [saver](const Json* a, const Json* b) {
            double first = (*a)["bitrate"].num(), second = (*b)["bitrate"].num();
            return saver ? first < second : first > second;
        });
        for (const Json* format : formats) {
            const Json& f = *format;
            string url = f["url"].str();
            long long length = (long long)f["contentLength"].num();
            if (length <= 0) {
                HttpRes head = http("HEAD", url, {{"User-Agent", agent}});
                if (head.ok()) length = head.contentLength;
            }
            if (length < 8 || audioRange(url, agent, length, length - 2, length - 1).empty()) continue;
            pick.url = std::move(url);
            pick.length = length;
            break;
        }
    }
    const Json& loud = root.at("playerConfig", "audioConfig", "loudnessDb");
    if (loud.has()) { pick.hasLoudness = true; pick.loudnessDb = loud.num(); }
    if (pick.url.empty()) {
        pick.error = status["reason"].str();
        const Json& message = status.at("errorScreen", "playerErrorMessageRenderer");
        string details = lower(pick.error + " " + message["reason"].text() + " " + message["subreason"].text());
        pick.needsVerification = contains(details, "not a bot");
        if (pick.error.empty()) pick.error = status["status"].str() == "OK" ? "No audio for this song" : "This song can't be played";
    }
    return pick;
}

// Wave's Windows resolver (YoutubeExplode) gets a separate visitor ID for native playback.
static string playbackVisitor() {
    static std::mutex visitorGate;
    static string visitor;
    {
        std::lock_guard<std::mutex> lock(visitorGate);
        if (!visitor.empty()) return visitor;
    }
    HttpRes res = http("GET", "https://www.youtube.com/sw.js_data", {{"Accept", "application/json"},
        {"User-Agent", "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip"}});
    if (!res.ok()) return yt::session().visitorData;
    if (startsWith(res.body, ")]}'")) res.body.erase(0, 4);
    string fresh = Json::parse(res.body).at(0, 2, 0, 0, 13).str();
    if (fresh.empty()) return yt::session().visitorData;
    std::lock_guard<std::mutex> lock(visitorGate);
    if (visitor.empty()) visitor = fresh;
    return visitor;
}

static AudioPick requestAudio(const string& videoId, bool saver, const string& visitor,
                              const string& clientFields, const string& agent, Headers headers = {}) {
    string body = "{\"context\":{\"client\":{" + clientFields + ",\"hl\":\"en\",\"gl\":\"US\",\"utcOffsetMinutes\":0";
    if (!visitor.empty()) body += ",\"visitorData\":" + jq(visitor);
    body += "}}," + field("videoId", videoId) + ",\"contentCheckOk\":true,\"racyCheckOk\":true}";
    headers.push_back({"User-Agent", agent});
    headers.push_back({"Content-Type", "application/json"});
    HttpRes res = http("POST", "https://www.youtube.com/youtubei/v1/player?prettyPrint=false", headers, body);
    AudioPick pick = pickAudio(Json::parse(res.body), videoId, agent, saver);
    if (!res.ok() && !pick.needsVerification) {
        pick.url.clear();
        pick.error = res.status == 0 ? "No connection" : "YouTube didn't answer";
    }
    return pick;
}

AudioPick yt::audio(const string& videoId, bool saver, bool retryVerified, unsigned rejected) {
    using Client = AudioClient;
    const Client order[] = {retryVerified ? Client::Music : Client::None, Client::Vision, Client::Android, Client::Vr,
                            retryVerified ? Client::None : Client::Music};
    AudioPick failure;
    for (Client client : order) {
        if (client == Client::None || (rejected & (unsigned)client)) continue;
        AudioPick pick;
        switch (client) {
            case Client::Vision:
                pick = requestAudio(videoId, saver, playbackVisitor(),
                    "\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\",\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\","
                    "\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\"",
                    "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15");
                break;
            case Client::Android:
                pick = requestAudio(videoId, saver, playbackVisitor(),
                    "\"clientName\":\"ANDROID\",\"clientVersion\":\"21.26.364\","
                    "\"androidSdkVersion\":30,\"osName\":\"Android\",\"osVersion\":\"11\"",
                    "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip");
                break;
            case Client::Vr: {
                const string agent = "com.google.android.apps.youtube.vr.oculus/1.62.27 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
                Headers headers = {{"X-Youtube-Client-Name", "28"}, {"X-Youtube-Client-Version", "1.62.27"}};
                string visitor = session().visitorData;
                if (visitor.empty()) visitor = playbackVisitor();
                if (!visitor.empty()) headers.push_back({"X-Goog-Visitor-Id", visitor});
                pick = requestAudio(videoId, saver, visitor,
                    "\"clientName\":\"ANDROID_VR\",\"clientVersion\":\"1.62.27\",\"deviceMake\":\"Oculus\",\"deviceModel\":\"Quest 3\","
                    "\"androidSdkVersion\":32,\"osName\":\"Android\",\"osVersion\":\"12L\"", agent, std::move(headers));
                break;
            }
            case Client::Music:
                if (!retryVerified && !failure.needsVerification && rejected == 0) continue;
                pick = pickAudio(post("player", field("videoId", videoId)), videoId, BrowserAgent, saver);
                break;
            default:
                continue;
        }
        pick.client = client;
        if (!pick.url.empty()) return pick;
        if (pick.needsVerification || (!failure.needsVerification && client != Client::Music) || failure.error.empty())
            failure = std::move(pick);
    }
    return failure;
}

void yt::reportPlay(const string& videoId) {
    Session s = session();
    if (!s.signedIn()) return;
    Json root = post("player", field("videoId", videoId));
    string url = root.at("playbackTracking", "videostatsPlaybackUrl", "baseUrl").str();
    if (url.empty()) return;
    static const char* alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    string cpn;
    for (int i = 0; i < 16; i++) cpn += alphabet[rand() & 63];
    string version;
    { std::lock_guard<std::mutex> lock(gate); version = clientVersion; }
    http("GET", url + "&ver=2&c=WEB_REMIX&cpn=" + cpn, headersFor(s, version));
}

// ---------------------------------------------------------------- Your library

static std::vector<Item> gridAll(const string& browseId) {
    Json root = post("browse", field("browseId", browseId));
    const Json& section = root.at("contents", "singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents", 0);
    const Json& grid = section["gridRenderer"];
    const Json& shelf = section["musicShelfRenderer"];
    std::vector<Item> items = itemsOf(grid.has() ? grid["items"] : shelf["contents"]);
    string next = (grid.has() ? grid : shelf).at("continuations", 0, "nextContinuationData", "continuation").str();
    for (int page = 0; !next.empty() && page < 10; page++) {
        Json more = post("browse", "", "ctoken=" + urlEncode(next) + "&continuation=" + urlEncode(next) + "&type=next");
        const Json& g = more.at("continuationContents", "gridContinuation");
        const Json& s = more.at("continuationContents", "musicShelfContinuation");
        for (auto& item : itemsOf(g.has() ? g["items"] : s["contents"])) items.push_back(item);
        next = (g.has() ? g : s).at("continuations", 0, "nextContinuationData", "continuation").str();
    }
    return items;
}

static std::vector<Item> onlyKind(std::vector<Item> items, Kind kind) {
    std::vector<Item> out;
    for (auto& item : items) {
        if (item.kind != kind || item.podcast || item.profile) continue;
        if (std::any_of(out.begin(), out.end(), [&](const Item& o) { return o.id == item.id; })) continue;
        out.push_back(std::move(item));
    }
    return out;
}

std::vector<Item> yt::libraryPlaylists() { return onlyKind(gridAll("FEmusic_liked_playlists"), Kind::Playlist); }
std::vector<Item> yt::libraryAlbums() { return onlyKind(gridAll("FEmusic_liked_albums"), Kind::Album); }
std::vector<Item> yt::libraryArtists() { return onlyKind(gridAll("FEmusic_library_corpus_artists"), Kind::Artist); }

std::vector<Section> yt::history() {
    std::vector<Section> sections;
    Json root = post("browse", field("browseId", "FEmusic_history"));
    for (auto& node : root.at("contents", "singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content", "sectionListRenderer", "contents").each()) {
        Section section;
        if (carousel(node, section)) sections.push_back(std::move(section));
    }
    return sections;
}

bool yt::accountInfo(Session& into) {
    Json root = post("account/account_menu", "");
    const Json& header = root.at("actions", 0, "openPopupAction", "popup", "multiPageMenuRenderer", "header", "activeAccountHeaderRenderer");
    string name = header["accountName"].text();
    if (name.empty()) return false;
    into.name = name;
    into.email = header["email"].text();
    into.photo = header["accountPhoto"].thumb();
    return true;
}

bool yt::like(const string& videoId, bool liked) {
    return post(liked ? "like/like" : "like/removelike", "\"target\":{" + field("videoId", videoId) + "}").has();
}

bool yt::subscribe(const string& channelId, bool on) {
    return post(on ? "subscription/subscribe" : "subscription/unsubscribe", "\"channelIds\":[" + jq(channelId) + "]").has();
}

bool yt::addToPlaylist(const string& playlistId, const string& videoId) {
    return post("browse/edit_playlist", field("playlistId", playlistId) + ",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\"," + field("addedVideoId", videoId) + ",\"dedupeOption\":\"DEDUPE_OPTION_SKIP\"}]").has();
}
