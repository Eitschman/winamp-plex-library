#include "plex/catalog.h"
#include "plex/http_stream.h"
#include "plex/artwork.h"
#include <objbase.h>
#include <iostream>
#include <stdexcept>

// Bewusster Integrationstest, nicht Teil von CTest; keine privaten Inhalte ausgeben.
int main(int count, char** arguments) {
    if (count != 1 && count != 3) return 2;
    const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int stage = 0;
    try {
        const auto settings = plex::load_settings(plex::settings_path());
        const plex::PlexCatalog client(settings, stop);
        for (const auto kind : {plex::BrowseKind::artists, plex::BrowseKind::albums, plex::BrowseKind::tracks, plex::BrowseKind::playlists}) {
            stage = 10 * static_cast<int>(kind) + 1;
            plex::BrowseQuery query; query.kind = kind; query.library_identifier = settings.library_identifier; query.limit = 2;
            const auto first = client.page(query);
            if (first.more) {
                ++stage;
                query.offset = first.items.size();
                const auto second = client.page(query);
                if (second.items.empty() || plex::item_identifier(second.items.front()) == plex::item_identifier(first.items.front()))
                    throw std::runtime_error("live_pagination");
            }
            if (!first.items.empty()) {
                stage = 10 * static_cast<int>(kind) + 3;
                query.offset = 0;
                auto search_item = first.items.front();
                if (plex::item_title(search_item).empty()) {
                    query.descending = true;
                    const auto last = client.page(query);
                    if (!last.items.empty()) search_item = last.items.front();
                    query.descending = false;
                }
                if (!plex::item_title(search_item).empty()) {
                    query.search = plex::item_title(search_item);
                    const auto searched = client.page(query);
                    if (searched.items.empty()) throw std::runtime_error("live_search");
                }
                query.search.clear(); query.parent_identifier = plex::item_identifier(first.items.front());
                if (kind == plex::BrowseKind::artists || kind == plex::BrowseKind::albums || kind == plex::BrowseKind::playlists) {
                    ++stage;
                    query.playlist_items = kind == plex::BrowseKind::playlists;
                    query.kind = kind == plex::BrowseKind::artists ? plex::BrowseKind::albums : plex::BrowseKind::tracks;
                    if (query.kind == plex::BrowseKind::tracks) query.sort = "albumOrder";
                    const auto children = client.page(query);
                    if (kind == plex::BrowseKind::albums) {
                        const auto tracks = client.tracks(query);
                        for (std::size_t index = 1; index < tracks.size(); ++index) {
                            if (std::pair{tracks[index].disc, tracks[index].number} < std::pair{tracks[index - 1].disc, tracks[index - 1].number})
                                throw std::runtime_error("live_album_order");
                        }
                    }
                    std::cout << "children kind=" << static_cast<int>(kind) << " count=" << children.items.size() << '\n';
                }
            }
            std::cout << "catalog kind=" << static_cast<int>(kind) << " count=" << first.items.size() << " more=" << first.more << '\n';
        }
        if (count == 3) {
        stage = 41;
        plex::BrowseQuery cover_query; cover_query.kind = plex::BrowseKind::albums;
        cover_query.library_identifier = settings.library_identifier; cover_query.search = arguments[2];
        const auto albums = client.page(cover_query);
        bool matched = false;
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) throw std::runtime_error("artwork_com");
        try {
            for (const auto& item : albums.items) {
                const auto* album = std::get_if<plex::PlexAlbum>(&item);
                if (!album || album->artist != arguments[1]) continue;
                matched = true;
                const auto cover = plex::fetch_artwork(settings, album->thumb, stop);
                if (!cover) throw std::runtime_error("live_album_cover_missing");
                std::cout << "Requested album matched; cover decoded " << cover->width << 'x' << cover->height << '\n';
                plex::BrowseQuery tracks_query; tracks_query.kind = plex::BrowseKind::tracks;
                tracks_query.parent_identifier = album->identifier; tracks_query.sort = "albumOrder";
                const auto album_tracks = client.tracks(tracks_query);
                if (album_tracks.empty()) throw std::runtime_error("live_album_tracks_empty");
                for (const auto& track : album_tracks)
                    if (track.album_identifier != album->identifier || plex::track_artwork(track).empty())
                        throw std::runtime_error("live_track_artwork_missing");
                std::cout << "Track album identity and artwork verified for " << album_tracks.size() << " tracks\n";
            }
        } catch (...) { CoUninitialize(); throw; }
        CoUninitialize();
        if (!matched) throw std::runtime_error("live_album_not_found");
        }
        CloseHandle(stop);
        std::cout << "Live catalog, pagination and search passed\n";
        return 0;
    } catch (const plex::HttpError& error) {
        if (stop) CloseHandle(stop); std::cerr << "Live catalog HTTP failure; stage=" << stage << "; code=" << error.code << '\n'; return 1;
    } catch (const nlohmann::json::exception& error) {
        if (stop) CloseHandle(stop); std::cerr << "Live catalog JSON shape failure; stage=" << stage << "; id=" << error.id << '\n'; return 1;
    } catch (const std::exception& error) {
        if (stop) CloseHandle(stop);
        const char* code = "unclassified";
        for (const auto* known : {"pagination_size", "pagination_offset", "catalog_type", "catalog_identifier", "catalog_number", "live_search", "live_pagination", "api_response_error", "media_path", "media_id", "media_version", "media_filename", "catalog_media", "catalog_parts", "catalog_text", "catalog_title"})
            if (std::string(error.what()) == known) code = known;
        std::cerr << "Live catalog failure; stage=" << stage << "; code=" << code << '\n'; return 1;
    }
}
