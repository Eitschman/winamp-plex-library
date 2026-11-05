#include "plex/auth.h"
#include "plex/catalog.h"
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
    if (operation != L"--inspect" && operation != L"--rate" && operation != L"--restore" && operation != L"--timeline" && operation != L"--prepare-ui" && operation != L"--check-test") return 2;
    if ((operation == L"--rate" || operation == L"--restore" || operation == L"--prepare-ui" || operation == L"--check-test") != (count == 3)) return 2;
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
        if (operation == L"--restore" || operation == L"--check-test") {
            std::ifstream file(std::filesystem::path(arguments[2]), std::ios::binary);
            const auto saved = nlohmann::json::parse(file);
            file.close();
            check(saved.at("server_identifier") == settings.server_identifier, "restore_server_identity");
            const auto identifier = saved.at("identifier").get<std::string>();
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
        plex::BrowseQuery query; query.kind = plex::BrowseKind::tracks; query.library_identifier = settings.library_identifier; query.limit = 250;
        const auto page = plex::PlexCatalog(settings, stop.value).page(query);
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
        if (operation == L"--prepare-ui") {
            const auto original = before.value("userRating", 0.0);
            write_restore(arguments[2], {{"server_identifier", settings.server_identifier}, {"identifier", track.identifier},
                {"rating", original}, {"test_rating", original == 6.0 ? 8.0 : 6.0},
                {"row_index", std::distance(page.items.begin(), selected)}, {"title", track.title}, {"artist", track.artist}, {"album", track.album}});
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
