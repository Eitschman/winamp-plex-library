#include "plex/auth.h"
#include "plex/catalog.h"
#include "plex/artwork.h"
#include "plex/feedback.h"
#include "plex/http_stream.h"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
struct ProbeFailure : std::exception {
    explicit ProbeFailure(const char* value) : stage(value) {}
    const char* stage;
};
void check(bool condition, const char* name) { if (!condition) throw ProbeFailure(name); }
void write_restore(const std::filesystem::path& path, const nlohmann::json& document) {
    const auto bytes = document.dump();
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("restore_file_create");
    DWORD written = 0;
    const auto success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const auto closed = CloseHandle(file);
    if (!success || written != bytes.size() || !closed) {
        DeleteFileW(path.c_str()); throw std::runtime_error("restore_file_write");
    }
}
}
// Bewusster, freigegebener Integrationstest. Keine Titel, Adressen oder Tokens ausgeben.
int wmain(int count, wchar_t** arguments) {
    if (count < 2 || count > 3) return 2;
    const std::wstring operation = arguments[1];
    const bool playlist_sample = operation == L"--prepare-playlist";
    const bool sort_samples = operation == L"--prepare-sorts";
    const bool native_check = operation == L"--check-playing" || operation == L"--check-paused" || operation == L"--check-stopped" || operation == L"--check-seek" || operation == L"--check-history" || operation == L"--finish-timeline";
    if (operation != L"--inspect" && operation != L"--rate" && operation != L"--restore" && operation != L"--timeline" && operation != L"--prepare-ui" && operation != L"--check-test" && operation != L"--prepare-timeline" && operation != L"--prepare-library" && !playlist_sample && !sort_samples && !native_check) return 2;
    if ((operation == L"--rate" || operation == L"--restore" || operation == L"--prepare-ui" || operation == L"--check-test" || operation == L"--prepare-timeline" || operation == L"--prepare-library" || playlist_sample || sort_samples || native_check) != (count == 3)) return 2;
    struct Stop { HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr); ~Stop() { if (value) CloseHandle(value); } } stop;
    try {
        check(stop.value != nullptr, "stop_event");
        auto settings = plex::load_settings(plex::settings_path());
        const plex::PlexAuth client(settings.client_identifier, stop.value);
        const auto connection = client.connect_server({settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections}, settings.preferred_origin);
        settings.server_origin = connection.origin;
        const auto metadata = [&](const std::string& identifier) {
            const auto response = client.get_server(settings.server_origin, settings.server_token, "/library/metadata/" + identifier);
            const auto& items = response.at("MediaContainer").at("Metadata");
            check(items.is_array() && items.size() == 1 && items.at(0).at("ratingKey") == identifier, "metadata_identity");
            return items.at(0);
        };
        const auto history_key = [&](const std::string& identifier) {
            const auto response = client.get_server(settings.server_origin, settings.server_token,
                "/status/sessions/history/all?metadataItemID=" + identifier + "&sort=viewedAt%3Adesc&X-Plex-Container-Start=0&X-Plex-Container-Size=1");
            const auto& container = response.at("MediaContainer");
            if (!container.contains("Metadata") || container.at("Metadata").empty()) return std::string{};
            return container.at("Metadata").at(0).at("historyKey").get<std::string>();
        };
        if (operation == L"--restore" || operation == L"--check-test" || native_check) {
            std::ifstream file(std::filesystem::path(arguments[2]), std::ios::binary);
            const auto saved = nlohmann::json::parse(file);
            file.close();
            check(saved.at("server_identifier") == settings.server_identifier, "restore_server_identity");
            const auto identifier = saved.at("identifier").get<std::string>();
            plex::rating_path(identifier, 0);
            if (native_check) {
                if (operation == L"--check-history") {
                    const auto current = metadata(identifier);
                    check(current.value("viewCount", 0LL) > saved.at("view_count").get<long long>() &&
                          history_key(identifier) != saved.at("history_key").get<std::string>(), "natural_playback_history_and_count_readback");
                    std::cout << "native_playback_history_and_count_read_back=1\n"; return 0;
                }
                const auto* state = operation == L"--check-paused" ? "paused" : "playing";
                const bool stopped = operation == L"--check-stopped" || operation == L"--finish-timeline";
                bool matched = false;
                for (unsigned attempt = 0; attempt < 20 && !matched; ++attempt) {
                    const auto response = client.get_server(settings.server_origin, settings.server_token, "/status/sessions");
                    const auto& container = response.at("MediaContainer");
                    bool found = false;
                    if (container.contains("Metadata")) for (const auto& item : container.at("Metadata")) {
                        if (item.value("ratingKey", std::string{}) != identifier || !item.contains("Player")) continue;
                        const auto& player = item.at("Player");
                        if (player.value("machineIdentifier", std::string{}) != settings.client_identifier) continue;
                        found = true;
                        if (!stopped && player.value("state", std::string{}) == state &&
                            item.value("viewOffset", 0LL) >= (operation == L"--check-seek" ? 9000 : operation == L"--check-paused" ? 500 : 0)) matched = true;
                    }
                    if (stopped && !found) matched = true;
                    if (!matched) std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                check(matched, "native_playback_session_readback");
                if (operation == L"--finish-timeline") check(DeleteFileW(arguments[2]) != FALSE, "timeline_marker_cleanup");
                std::cout << "native_playback_state_read_back=1\n"; return 0;
            }
            const auto rating = saved.at(operation == L"--restore" ? "rating" : "test_rating").get<double>();
            if (operation == L"--check-test") {
                check(metadata(identifier).value("userRating", 0.0) == rating, "native_rating_readback");
                std::cout << "native_rating_read_back=1\n"; return 0;
            }
            plex::rate_item(settings, identifier, rating, stop.value);
            check(metadata(identifier).value("userRating", 0.0) == rating, "rating_restore_readback");
            check(DeleteFileW(arguments[2]) != FALSE, "restore_marker_cleanup");
            std::cout << "rating_restored=1\n"; return 0;
        }
        if (sort_samples) {
            const plex::PlexCatalog catalog(settings, stop.value);
            nlohmann::json cases = nlohmann::json::array();
            const std::vector<std::vector<std::string>> fields{{"titleSort"}, {"titleSort", "artist.titleSort", "year"},
                {"titleSort", "artist.titleSort", "album.titleSort", "duration", "index", "year"}};
            for (std::size_t kind = 0; kind < fields.size(); ++kind) {
                for (std::size_t column = 0; column < fields[kind].size(); ++column) {
                    for (const bool descending : {column == 0, column != 0}) {
                        plex::BrowseQuery query; query.kind = static_cast<plex::BrowseKind>(kind);
                        query.library_identifier = settings.library_identifier; query.sort = fields[kind][column]; query.descending = descending;
                        const auto page = catalog.page(query);
                        nlohmann::json rows = nlohmann::json::array();
                        for (const auto& item : page.items) {
                            nlohmann::json cells = nlohmann::json::array({plex::item_title(item)});
                            if (const auto* album = std::get_if<plex::PlexAlbum>(&item)) {
                                cells.push_back(album->artist); cells.push_back(album->year ? std::to_string(album->year) : "");
                            } else if (const auto* track = std::get_if<plex::PlexTrack>(&item)) {
                                cells.push_back(track->artist); cells.push_back(track->album);
                                const auto seconds = track->duration_ms / 1000;
                                cells.push_back(std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60));
                                cells.push_back(track->number ? std::to_string(track->number) : "");
                                cells.push_back(track->year ? std::to_string(track->year) : "");
                            }
                            rows.push_back(std::move(cells));
                        }
                        cases.push_back({{"kind", kind}, {"column", column}, {"descending", descending}, {"rows", rows}});
                    }
                }
            }
            write_restore(arguments[2], {{"cases", cases}});
            std::cout << "native_sort_snapshots_read=1 cases=" << cases.size() << '\n'; return 0;
        }
        if (playlist_sample) {
            plex::BrowseQuery query; query.kind = plex::BrowseKind::playlists; query.library_identifier = settings.library_identifier; query.limit = 250;
            const plex::PlexCatalog catalog(settings, stop.value);
            const auto page = catalog.page(query);
            nlohmann::json empty, selected;
            std::size_t largest = 0, nonempty = 0;
            for (std::size_t index = 0; index < page.items.size(); ++index) {
                const auto& item = std::get<plex::PlexPlaylist>(page.items[index]);
                std::cout << "playlist_fixture_track_count=" << item.track_count << '\n';
                nlohmann::json sample{{"row_index", index}, {"title", item.title}, {"count", item.track_count}};
                if (item.track_count == 0) { if (empty.is_null()) empty = sample; continue; }
                ++nonempty;
                if (item.track_count > 250 || item.track_count <= largest) continue;
                auto children = query; children.kind = plex::BrowseKind::tracks;
                children.parent_identifier = item.identifier; children.playlist_items = true;
                const auto tracks = catalog.tracks(children);
                auto single = children; single.limit = 1;
                const auto first = catalog.page(single);
                const auto detail = client.get_server(settings.server_origin, settings.server_token, "/playlists/" + item.identifier);
                const auto& description = detail.at("MediaContainer").at("Metadata").at(0);
                std::cout << "playlist_fixture_single_total=" << first.total.value_or(0)
                          << " metadata_count=" << description.value("leafCount", 0)
                          << " smart=" << description.value("smart", false) << '\n';
                std::size_t supported = 0;
                for (const auto& track : tracks) {
                    try { supported += !plex::direct_parts(track).empty(); }
                    catch (const std::invalid_argument&) { }
                }
                std::cout << "playlist_fixture_actual_tracks=" << tracks.size() << " supported=" << supported << '\n';
                if (tracks.empty() || tracks.size() <= largest || supported != tracks.size()) continue;
                check(first.total == tracks.size() && description.at("leafCount") == tracks.size(), "playlist_detail_count_readback");
                sample["listed_count"] = item.track_count; sample["count"] = tracks.size();
                nlohmann::json titles = nlohmann::json::array();
                for (const auto& track : tracks) titles.push_back(track.title);
                sample["titles"] = std::move(titles); selected = std::move(sample); largest = tracks.size();
            }
            write_restore(arguments[2], {{"empty", empty}, {"selected", selected}, {"playlist_count", page.items.size()}});
            std::cout << "native_playlist_samples_read=1 playlists=" << page.items.size() << " nonempty=" << nonempty << " selected_tracks=" << largest << '\n';
            return 0;
        }
        plex::BrowseQuery query; query.kind = plex::BrowseKind::tracks; query.library_identifier = settings.library_identifier; query.limit = 250;
        const auto page = plex::PlexCatalog(settings, stop.value).page(query);
        if (operation == L"--prepare-library") {
            nlohmann::json samples = nlohmann::json::array();
            nlohmann::json bulk = nlohmann::json::array();
            nlohmann::json missing, covered;
            for (std::size_t index = 0; index < page.items.size(); ++index) {
                if (!std::holds_alternative<plex::PlexTrack>(page.items[index])) continue;
                const auto& item = std::get<plex::PlexTrack>(page.items[index]);
                if (item.title.empty() || item.duration_ms <= 0) continue;
                nlohmann::json sample{{"row_index", index}, {"title", item.title}, {"artist", item.artist}, {"album", item.album}};
                if (plex::track_artwork(item).empty() && missing.is_null()) missing = sample;
                if (!plex::track_artwork(item).empty() && covered.is_null()) covered = sample;
                if (item.parts.size() == 1 && item.parts.front().key.size() > 4 && item.parts.front().key.substr(item.parts.front().key.size() - 4) == ".mp3") {
                    if (samples.size() < 3) samples.push_back(sample);
                    bulk.push_back(sample);
                }
            }
            check(samples.size() == 3 && !missing.is_null() && !covered.is_null(), "native_library_samples_available");
            write_restore(arguments[2], {{"tracks", samples}, {"bulk", bulk}, {"without_artwork", missing}, {"with_artwork", covered}});
            std::cout << "native_library_samples_prepared_without_mutation=1\n"; return 0;
        }
        const auto selected = std::find_if(page.items.begin(), page.items.end(), [](const auto& item) {
            if (!std::holds_alternative<plex::PlexTrack>(item)) return false;
            const auto& track = std::get<plex::PlexTrack>(item);
            return track.duration_ms > 3000 && std::any_of(track.parts.begin(), track.parts.end(), [](const auto& part) {
                return part.key.size() > 4 && part.key.substr(part.key.size() - 4) == ".mp3";
            });
        });
        check(selected != page.items.end(), "test_track_available");
        const auto track = std::get<plex::PlexTrack>(*selected);
        const auto before = metadata(track.identifier);
        if (operation == L"--prepare-ui" || operation == L"--prepare-timeline") {
            const auto original = before.value("userRating", 0.0);
            nlohmann::json saved{{"server_identifier", settings.server_identifier}, {"identifier", track.identifier},
                {"rating", original}, {"test_rating", original == 6.0 ? 8.0 : 6.0},
                {"row_index", std::distance(page.items.begin(), selected)}, {"title", track.title}, {"artist", track.artist}, {"album", track.album}};
            if (operation == L"--prepare-timeline") {
                saved["view_count"] = before.value("viewCount", 0LL); saved["history_key"] = history_key(track.identifier);
            }
            write_restore(arguments[2], saved);
            std::cout << "native_rating_test_prepared_without_mutation=1\n"; return 0;
        }
        if (operation == L"--inspect") {
            std::cout << "track_available=1 rating_present=" << before.contains("userRating")
                      << " duration_available=" << (track.duration_ms > 0) << '\n'; return 0;
        }
        if (operation == L"--rate") {
            const auto original = before.value("userRating", 0.0);
            const auto test_rating = original == 6.0 ? 8.0 : 6.0;
            write_restore(arguments[2], {{"server_identifier", settings.server_identifier}, {"identifier", track.identifier}, {"rating", original}});
            try {
                plex::rate_item(settings, track.identifier, test_rating, stop.value);
                check(metadata(track.identifier).value("userRating", 0.0) == test_rating, "rating_write_readback");
                std::cout << "rating_changed_and_read_back=1\n";
            } catch (...) {
                plex::rate_item(settings, track.identifier, original, stop.value);
                check(metadata(track.identifier).value("userRating", 0.0) == original, "rating_restore_after_failure");
                check(DeleteFileW(arguments[2]) != FALSE, "restore_marker_cleanup");
                throw;
            }
            plex::rate_item(settings, track.identifier, original, stop.value);
            check(metadata(track.identifier).value("userRating", 0.0) == original, "rating_restore_readback");
            check(DeleteFileW(arguments[2]) != FALSE, "restore_marker_cleanup");
            std::cout << "rating_restored_and_read_back=1\n";
            return 0;
        }
        const auto session_matches = [&](const char* state) {
            const auto sessions = client.get_server(settings.server_origin, settings.server_token, "/status/sessions");
            const auto& container = sessions.at("MediaContainer");
            if (container.contains("Metadata")) for (const auto& item : container.at("Metadata")) {
                if (item.value("ratingKey", std::string{}) != track.identifier || !item.contains("Player")) continue;
                const auto& player = item.at("Player");
                if (player.value("machineIdentifier", std::string{}) != settings.client_identifier) continue;
                if (!state) return false;
                return player.value("state", std::string{}) == state && item.value("viewOffset", 0LL) >= 1000;
            }
            return state == nullptr;
        };
        const auto wait_state = [&](const char* state) {
            for (unsigned attempt = 0; attempt < 20; ++attempt) {
                if (session_matches(state)) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            throw std::runtime_error("timeline_session_readback");
        };
        check(track.duration_ms > 3000, "timeline_track_duration");
        try {
            plex::report_timeline(settings, {track.identifier, plex::PlaybackState::playing, 1000, track.duration_ms}, stop.value);
            wait_state("playing");
            plex::report_timeline(settings, {track.identifier, plex::PlaybackState::paused, 2000, track.duration_ms}, stop.value);
            wait_state("paused");
        } catch (...) {
            plex::report_timeline(settings, {track.identifier, plex::PlaybackState::stopped, 2000, track.duration_ms}, stop.value);
            throw;
        }
        plex::report_timeline(settings, {track.identifier, plex::PlaybackState::stopped, 2000, track.duration_ms}, stop.value);
        wait_state(nullptr);
        std::cout << "timeline_playing_paused_stopped_read_back=1 synthetic_api_states=1\n";
        return 0;
    } catch (const ProbeFailure& error) { std::cerr << "feedback_probe_stage=" << error.stage << '\n'; }
      catch (const plex::HttpError& error) { std::cerr << "feedback_probe_http_code=" << error.code << '\n'; }
      catch (...) { std::cerr << "feedback_probe_failed_without_response_details\n"; }
    return 1;
}
