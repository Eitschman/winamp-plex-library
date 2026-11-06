#pragma once
#include "plex/auth.h"
#include "plex/settings.h"
#include "plex/browse_query.h"
#include <optional>
#include <variant>

namespace plex {
struct PlexMediaPart {
    std::string key, container, audio_codec;
    long long duration_ms = 0;
    unsigned media_index = 0, part_index = 0;
};
struct PlexArtist { std::string identifier, title, thumb; unsigned album_count = 0; };
struct PlexAlbum {
    std::string identifier, title, artist, thumb;
    unsigned year = 0, track_count = 0;
};
struct PlexTrack {
    std::string identifier, title, artist, album, thumb;
    unsigned year = 0, number = 0, disc = 0;
    long long duration_ms = 0;
    std::vector<PlexMediaPart> parts;
    std::string album_identifier, album_thumb;
    std::optional<double> user_rating;
    std::string album_artist;
};
// Ein eigener Track-Thumb hat Vorrang vor dem geerbten Albumcover.
const std::string& track_artwork(const PlexTrack& track);
struct PlexPlaylist { std::string identifier, title, thumb; unsigned track_count = 0; long long duration_ms = 0; };
using PlexItem = std::variant<PlexArtist, PlexAlbum, PlexTrack, PlexPlaylist>;
struct CatalogPage {
    std::vector<PlexItem> items;
    std::size_t offset = 0;
    std::optional<std::size_t> total;
    bool more = false;
};
struct FilterChoice { std::string key, title; };
struct CatalogFilters { std::vector<FilterChoice> genres, years, audio_codecs; };
std::vector<FilterChoice> parse_filter_choices(const nlohmann::json& document);
std::string catalog_path(const BrowseQuery& query);
CatalogPage parse_catalog_page(const nlohmann::json& document, std::size_t requested_offset);
PlexPlaylist parse_playlist_details(const nlohmann::json& document, const std::string& expected_identifier);
const std::string& item_identifier(const PlexItem& item);
const std::string& item_title(const PlexItem& item);
std::vector<PlexMediaPart> direct_parts(const PlexTrack& track);

// Jede Anfrage bleibt an die bestätigte Server-Origin und deren Token gebunden.
class PlexCatalog {
public:
    PlexCatalog(PlexConnectionSettings settings, HANDLE stop);
    CatalogPage page(const BrowseQuery& query) const;
    CatalogFilters filters(const BrowseQuery& query) const;
    PlexPlaylist playlist(const std::string& identifier) const;
    std::vector<PlexTrack> tracks(const BrowseQuery& query) const;
private:
    PlexConnectionSettings settings_;
    HANDLE stop_;
};
}
