#include "plex/catalog.h"
#include "plex/page_cache.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
void rejects(const std::function<void()>& action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}
}
int main() {
    try {
        using nlohmann::json;
        const auto fixture = json::parse(R"({"MediaContainer":{"offset":0,"size":1,"totalSize":3,"Metadata":[{"type":"track","ratingKey":"42","title":"Grüße 東京","grandparentTitle":"Björk","parentTitle":"Album","index":2,"parentIndex":1,"year":2024,"duration":123456,"Media":[{"audioCodec":"flac","Part":[{"key":"/library/parts/7/123/file.flac","container":"flac"}]}]}]}})");
        const auto page = plex::parse_catalog_page(fixture, 0);
        check(page.more && page.total == std::size_t{3} && page.items.size() == 1, "server_page_size_respected");
        const auto track = std::get<plex::PlexTrack>(page.items.front());
        check(track.identifier == "42" && track.artist == "Björk" && track.title == "Grüße 東京", "typed_unicode_track");
        check(track.album_artist == "Björk", "ordinary_track_album_artist");
        auto compilation = fixture;
        compilation["MediaContainer"]["Metadata"][0]["grandparentTitle"] = "Various Artists";
        compilation["MediaContainer"]["Metadata"][0]["originalTitle"] = "Björk";
        const auto compilation_track = std::get<plex::PlexTrack>(plex::parse_catalog_page(compilation, 0).items.front());
        check(compilation_track.artist == "Björk" && compilation_track.album_artist == "Various Artists", "compilation_separates_track_and_album_artist");
        compilation["MediaContainer"]["Metadata"][0]["originalTitle"] = "";
        check(std::get<plex::PlexTrack>(plex::parse_catalog_page(compilation, 0).items.front()).artist == "Various Artists", "empty_original_artist_uses_album_artist");
        check(track.number == 2 && track.disc == 1 && track.duration_ms == 123456, "typed_track_numbers");
        check(track.parts.size() == 1 && track.parts.front().duration_ms == 123456, "media_part_model");
        check(!track.user_rating, "missing_user_rating_remains_unrated");
        auto rated = fixture;
        rated["MediaContainer"]["Metadata"][0]["userRating"] = 7.5;
        check(std::get<plex::PlexTrack>(plex::parse_catalog_page(rated, 0).items.front()).user_rating == 7.5, "fractional_plex_rating_preserved");
        for (const auto value : {json(-1), json(11), json("8"), json(nullptr)}) {
            rated["MediaContainer"]["Metadata"][0]["userRating"] = value;
            rejects([&] { plex::parse_catalog_page(rated, 0); }, "invalid_user_rating_not_silently_accepted");
        }
        auto inherited = fixture;
        inherited["MediaContainer"]["Metadata"][0]["parentRatingKey"] = "8";
        inherited["MediaContainer"]["Metadata"][0]["parentThumb"] = "/library/metadata/8/thumb/123";
        auto inherited_track = std::get<plex::PlexTrack>(plex::parse_catalog_page(inherited, 0).items.front());
        check(inherited_track.album_identifier == "8" && plex::track_artwork(inherited_track) == "/library/metadata/8/thumb/123", "track_inherits_album_cover");
        inherited_track.thumb = "/library/metadata/42/thumb/456";
        check(plex::track_artwork(inherited_track) == inherited_track.thumb, "track_specific_cover_has_priority");
        check(plex::track_artwork(track).empty(), "missing_track_and_album_cover");
        auto untitled = fixture; untitled["MediaContainer"]["Metadata"][0]["title"] = "";
        check(plex::item_title(plex::parse_catalog_page(untitled, 0).items.front()).empty(), "real_pms_empty_title_preserved");
        for (const auto* type : {"artist", "album", "playlist"}) {
            auto document = fixture;
            auto& item = document["MediaContainer"]["Metadata"][0];
            item["type"] = type; item["playlistType"] = "audio";
            if (std::string(type) == "playlist") item["duration"] = 4000000000LL;
            check(plex::parse_catalog_page(document, 0).items.size() == 1, "typed_catalog_item");
            if (std::string(type) == "playlist") check(std::get<plex::PlexPlaylist>(plex::parse_catalog_page(document, 0).items.front()).duration_ms == 4000000000LL, "long_playlist_duration");
        }
        auto invalid = fixture; invalid["MediaContainer"]["offset"] = 5;
        auto playlist = fixture;
        auto& playlist_item = playlist["MediaContainer"]["Metadata"][0];
        playlist["MediaContainer"]["totalSize"] = 1;
        playlist_item["type"] = "playlist"; playlist_item["playlistType"] = "audio";
        playlist_item["leafCount"] = 22; playlist_item["duration"] = 4000000000LL;
        const auto details = plex::parse_playlist_details(playlist, "42");
        check(details.track_count == 22 && details.duration_ms == 4000000000LL, "playlist_detail_count_and_long_duration");
        playlist_item["leafCount"] = 0; playlist_item["duration"] = 0;
        check(plex::parse_playlist_details(playlist, "42").track_count == 0, "empty_playlist_details");
        rejects([&] { plex::parse_playlist_details(playlist, "43"); }, "wrong_playlist_details_identity");
        rejects([&] { plex::parse_playlist_details(fixture, "42"); }, "track_not_playlist_details");
        rejects([&] { plex::parse_playlist_details(json::parse(R"({"MediaContainer":{"size":0}})"), "42"); }, "missing_playlist_details_rejected");
        playlist_item["playlistType"] = "video";
        rejects([&] { plex::parse_playlist_details(playlist, "42"); }, "video_playlist_details_rejected");
        rejects([&] { plex::parse_catalog_page(invalid, 0); }, "wrong_offset_detected");
        invalid = fixture; invalid["MediaContainer"]["size"] = 0;
        rejects([&] { plex::parse_catalog_page(invalid, 0); }, "wrong_size_detected");
        invalid = fixture; invalid["MediaContainer"]["Metadata"][0]["ratingKey"] = "../secret";
        rejects([&] { plex::parse_catalog_page(invalid, 0); }, "unsafe_identifier_detected");
        invalid = fixture; invalid["MediaContainer"]["Metadata"][0]["Media"][0]["Part"][0]["key"] = "https://foreign.example/media";
        rejects([&] { plex::parse_catalog_page(invalid, 0); }, "foreign_media_host_detected");
        rejects([&] { plex::parse_catalog_page(json::parse(R"({"MediaContainer":{"offset":1,"size":0,"totalSize":5}})"), 1); }, "empty_nonterminal_page_detected");
        const auto empty = plex::parse_catalog_page(json::parse(R"({"MediaContainer":{"size":0}})"), 0);
        check(empty.items.empty() && !empty.more, "empty_library");
        auto unpaged = fixture; unpaged["MediaContainer"].erase("offset"); unpaged["MediaContainer"].erase("totalSize");
        check(!plex::parse_catalog_page(unpaged, 0).more, "unpaged_response_terminates");
        plex::BrowseQuery query; query.library_identifier = "5"; query.search = "Björk & AC/DC";
        const auto path = plex::catalog_path(query);
        check(path.find("Bj%C3%B6rk%20%26%20AC%2FDC") != path.npos, "query_encoding");
        check(path.find("X-Plex-Container-Start=0&X-Plex-Container-Size=250") != path.npos, "pagination_query");
        query.parent_identifier = "42"; query.kind = plex::BrowseKind::tracks; query.sort = "albumOrder";
        check(plex::catalog_path(query).find("sort=") == std::string::npos, "album_preserves_server_order");
        query.search.clear();
        check(plex::catalog_path(query).find("/children?X-Plex-Container-Start=0") != std::string::npos, "album_valid_first_parameter");
        query.parent_identifier = "../42";
        rejects([&] { plex::catalog_path(query); }, "query_path_injection_detected");
        const auto now = plex::PageCache::Clock::now();
        plex::PageCache cache(2, std::chrono::seconds(10));
        cache.put("one", page, now); cache.put("two", page, now);
        check(cache.get("one", now).has_value(), "cache_hit");
        cache.put("three", page, now);
        check(!cache.get("two", now) && cache.get("one", now), "least_recently_used_eviction");
        check(!cache.get("one", now + std::chrono::seconds(10)), "cache_expiry");
        cache.clear(); check(!cache.get("three", now), "cache_clear");
        std::cout << checks << " catalog and cache checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
