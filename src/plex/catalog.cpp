#include "plex/catalog.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace plex {
namespace {
std::string identifier(const nlohmann::json& value) {
    std::string result;
    if (value.is_string()) result = value.get<std::string>();
    else if (value.is_number_unsigned()) result = std::to_string(value.get<unsigned long long>());
    else if (value.is_number_integer() && value.get<long long>() >= 0) result = std::to_string(value.get<long long>());
    if (result.empty() || result.size() > 32 || result.find_first_not_of("0123456789") != result.npos)
        throw std::invalid_argument("catalog_identifier");
    return result;
}
std::string field(const nlohmann::json& item, const char* key) {
    const auto result = item.value(key, std::string{});
    if (result.size() > 16384 || result.find('\0') != result.npos) throw std::invalid_argument("catalog_text");
    return result;
}
long long number(const nlohmann::json& item, const char* key) {
    if (!item.contains(key)) return 0;
    const auto& value = item.at(key);
    if (!value.is_number_integer()) throw std::invalid_argument("catalog_number");
    const auto result = value.get<long long>();
    if (result < 0 || (std::string_view(key) != "duration" && result > 2147483647)) throw std::invalid_argument("catalog_number");
    return result;
}
PlexItem parse_item(const nlohmann::json& item) {
    const auto id = identifier(item.at("ratingKey"));
    const auto title = field(item, "title"), thumb = field(item, "thumb"), type = field(item, "type");
    // PMS kann vorhandene Titel ohne Titeltext liefern. Die UI kennzeichnet diese.
    if (type == "artist") return PlexArtist{id, title, thumb, static_cast<unsigned>(number(item, "childCount"))};
    if (type == "album") return PlexAlbum{id, title, field(item, "parentTitle"), thumb,
        static_cast<unsigned>(number(item, "year")), static_cast<unsigned>(number(item, "leafCount"))};
    if (type == "playlist" && field(item, "playlistType") == "audio")
        return PlexPlaylist{id, title, thumb, static_cast<unsigned>(number(item, "leafCount")), number(item, "duration")};
    if (type != "track") throw std::invalid_argument("catalog_type");
    PlexTrack track{id, title, field(item, "grandparentTitle"), field(item, "parentTitle"), thumb,
        static_cast<unsigned>(number(item, "year")), static_cast<unsigned>(number(item, "index")),
        static_cast<unsigned>(number(item, "parentIndex")), number(item, "duration"), {}};
    // Bei Samplern gehört originalTitle zum Titel, grandparentTitle zum Album.
    track.album_artist = track.artist;
    const auto track_artist = field(item, "originalTitle");
    if (!track_artist.empty()) track.artist = track_artist;
    if (item.contains("userRating")) {
        const auto& value = item.at("userRating");
        if (!value.is_number()) throw std::invalid_argument("catalog_rating");
        const auto rating = value.get<double>();
        if (!std::isfinite(rating) || rating < 0 || rating > 10) throw std::invalid_argument("catalog_rating");
        track.user_rating = rating;
    }
    if (item.contains("Media")) {
        const auto& media = item.at("Media");
        if (!media.is_array() || media.size() > 32) throw std::invalid_argument("catalog_media");
        for (std::size_t media_index = 0; media_index < media.size(); ++media_index) {
            const auto& variant = media.at(media_index);
            if (!variant.contains("Part")) continue;
            const auto& parts = variant.at("Part");
            if (!parts.is_array() || parts.size() > 256) throw std::invalid_argument("catalog_parts");
            for (std::size_t part_index = 0; part_index < parts.size(); ++part_index) {
                const auto& part = parts.at(part_index);
                const auto key = field(part, "key");
                validate_media_path(key);
                auto duration = number(part, "duration");
                if (!duration) duration = number(variant, "duration");
                if (!duration && parts.size() == 1) duration = track.duration_ms;
                track.parts.push_back({key, field(part, "container"), field(variant, "audioCodec"), duration,
                    static_cast<unsigned>(media_index), static_cast<unsigned>(part_index)});
            }
        }
    }
    if (item.contains("parentRatingKey")) track.album_identifier = identifier(item.at("parentRatingKey"));
    track.album_thumb = field(item, "parentThumb");
    return track;
}
}

const std::string& item_identifier(const PlexItem& item) { return std::visit([](const auto& value) -> const std::string& { return value.identifier; }, item); }
const std::string& item_title(const PlexItem& item) { return std::visit([](const auto& value) -> const std::string& { return value.title; }, item); }
const std::string& track_artwork(const PlexTrack& track) { return track.thumb.empty() ? track.album_thumb : track.thumb; }

std::vector<PlexMediaPart> direct_parts(const PlexTrack& track) {
    // Eine vorhandene Variante auswählen, ohne Medien zu transcodieren oder nach Fehlern auszuweichen.
    for (unsigned media_index = 0; media_index < 32; ++media_index) {
        std::vector<PlexMediaPart> selected;
        bool supported = true;
        for (const auto& part : track.parts) if (part.media_index == media_index) {
            const auto dot = part.key.rfind('.');
            std::string extension = dot == part.key.npos ? "" : part.key.substr(dot);
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            if (extension != ".mp3" && extension != ".flac" && extension != ".aac" && extension != ".m4a" && extension != ".ogg") supported = false;
            selected.push_back(part);
        }
        if (supported && !selected.empty()) return selected;
    }
    throw std::invalid_argument("unsupported_media");
}

std::string catalog_path(const BrowseQuery& query) {
    if (!query.limit || query.limit > 500 || query.offset > 10000000 || query.search.size() > 1024)
        throw std::invalid_argument("catalog_query");
    if (query.sort != "titleSort" && query.sort != "year" && query.sort != "duration" && query.sort != "index" &&
        query.sort != "artist.titleSort" && query.sort != "album.titleSort" && query.sort != "albumOrder") throw std::invalid_argument("catalog_sort");
    std::string path;
    std::vector<std::string> parameters;
    if (!query.parent_identifier.empty()) {
        const auto parent = identifier(query.parent_identifier);
        path = query.playlist_items ? "/playlists/" + parent + "/items" : "/library/metadata/" + parent + "/children";
    } else if (query.kind == BrowseKind::playlists) { path = "/playlists"; parameters.push_back("playlistType=audio"); }
    else {
        const auto library = identifier(query.library_identifier);
        const auto type = query.kind == BrowseKind::artists ? 8 : query.kind == BrowseKind::albums ? 9 : 10;
        path = "/library/sections/" + library + "/all"; parameters.push_back("type=" + std::to_string(type));
    }
    if (!query.search.empty()) parameters.push_back("title=" + encode_query_component(query.search));
    // Album-Kinder liefert Plex bereits in Disc-/Titelreihenfolge. Ein zusätzlicher
    // parentIndex-Sortierschlüssel wird nicht von allen Musikbibliotheken akzeptiert.
    if (!query.playlist_items && query.sort != "albumOrder")
        parameters.push_back("sort=" + encode_query_component(query.sort + (query.descending ? ":desc" : ":asc")));
    parameters.push_back("X-Plex-Container-Start=" + std::to_string(query.offset));
    parameters.push_back("X-Plex-Container-Size=" + std::to_string(query.limit));
    char separator = '?';
    for (const auto& value : parameters) { path += separator; path += value; separator = '&'; }
    return path;
}

CatalogPage parse_catalog_page(const nlohmann::json& document, std::size_t requested_offset) {
    const auto& container = document.at("MediaContainer");
    CatalogPage page;
    page.offset = static_cast<std::size_t>(number(container, "offset"));
    if (requested_offset != page.offset) throw std::invalid_argument("pagination_offset");
    const auto declared = static_cast<std::size_t>(number(container, "size"));
    if (container.contains("totalSize")) page.total = static_cast<std::size_t>(number(container, "totalSize"));
    if (container.contains("Metadata")) {
        const auto& metadata = container.at("Metadata");
        if (!metadata.is_array() || metadata.size() > 5000) throw std::invalid_argument("catalog_page_limit");
        for (const auto& item : metadata) {
            auto parsed = parse_item(item);
            // Playlists können denselben Titel mehrfach enthalten; Reihenfolge bleibt erhalten.
            page.items.push_back(std::move(parsed));
        }
    }
    if (declared != page.items.size() || (page.total && page.offset + declared > *page.total))
        throw std::invalid_argument("pagination_size");
    page.more = page.total ? page.offset + declared < *page.total : container.contains("offset") && declared != 0;
    if (declared == 0 && page.more) throw std::invalid_argument("pagination_empty");
    return page;
}

PlexCatalog::PlexCatalog(PlexConnectionSettings settings, HANDLE stop) : settings_(std::move(settings)), stop_(stop) {
    parse_server_address(settings_.server_origin); validate_token(settings_.server_token);
}
// Dynamische Playlists besitzen in der Übersicht mitunter veraltete Zähler.
// Die Detailantwort muss weiterhin genau zur angefragten Audio-Playlist gehören.
PlexPlaylist parse_playlist_details(const nlohmann::json& document, const std::string& expected_identifier) {
    const auto page = parse_catalog_page(document, 0);
    if (page.items.size() != 1 || page.more || !std::holds_alternative<PlexPlaylist>(page.items.front()))
        throw std::invalid_argument("playlist_details_type");
    const auto result = std::get<PlexPlaylist>(page.items.front());
    if (result.identifier != identifier(expected_identifier)) throw std::invalid_argument("playlist_details_identity");
    return result;
}
PlexPlaylist PlexCatalog::playlist(const std::string& playlist_identifier) const {
    const PlexAuth client(settings_.client_identifier, stop_);
    return parse_playlist_details(client.get_server(settings_.server_origin, settings_.server_token,
        "/playlists/" + identifier(playlist_identifier)), playlist_identifier);
}
CatalogPage PlexCatalog::page(const BrowseQuery& query) const {
    const PlexAuth client(settings_.client_identifier, stop_);
    return parse_catalog_page(client.get_server(settings_.server_origin, settings_.server_token, catalog_path(query)), query.offset);
}
std::vector<PlexTrack> PlexCatalog::tracks(const BrowseQuery& input) const {
    auto query = input;
    query.offset = 0;
    std::vector<PlexTrack> result;
    for (;;) {
        const auto loaded = page(query);
        for (const auto& item : loaded.items) {
            if (!std::holds_alternative<PlexTrack>(item)) throw std::invalid_argument("expected_tracks");
            result.push_back(std::get<PlexTrack>(item));
            if (result.size() > 10000) throw std::invalid_argument("queue_limit");
        }
        if (!loaded.more) return result;
        query.offset += loaded.items.size();
    }
}
}
