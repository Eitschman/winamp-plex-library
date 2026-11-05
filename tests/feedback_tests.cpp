#include "plex/feedback.h"
#include "session_checks.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Action> void rejects(Action action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, name);
}
}
int main() {
    try {
        const nlohmann::json session{{"Player", {{"machineIdentifier", "synthetic-player"}}}};
        plex::test::require_no_transcode_session(session);
        rejects([&] { auto transcoded = session; transcoded["TranscodeSession"] = {{"audioDecision", "transcode"}};
                      plex::test::require_no_transcode_session(transcoded); }, "transcode_session_detected");
        rejects([&] { plex::test::require_no_transcode_session(nlohmann::json::object()); }, "missing_player_not_direct_play_proof");
        plex::TimelineReport report{"42", plex::PlaybackState::playing, 12000, 240000};
        check(plex::timeline_path(report) == "/:/timeline?key=%2Flibrary%2Fmetadata%2F42&ratingKey=42&state=playing&time=12000&duration=240000", "timeline_uses_milliseconds_and_metadata_key");
        report.state = plex::PlaybackState::paused;
        check(plex::timeline_path(report).find("state=paused") != std::string::npos, "paused_timeline");
        report.state = plex::PlaybackState::stopped; report.continuing = true;
        check(plex::timeline_path(report).find("continuing=1") != std::string::npos, "track_change_continuing");
        report.continuing = false;
        check(plex::timeline_path(report).find("continuing=0") != std::string::npos, "explicit_stop_not_continuing");
        report.time_ms = -1; rejects([&] { plex::timeline_path(report); }, "negative_position_rejected");
        report.time_ms = report.duration_ms + 1; rejects([&] { plex::timeline_path(report); }, "position_beyond_duration_rejected");
        report.time_ms = 0; report.duration_ms = 0; rejects([&] { plex::timeline_path(report); }, "unknown_duration_rejected");
        report.duration_ms = 240000; report.state = static_cast<plex::PlaybackState>(99);
        rejects([&] { plex::timeline_path(report); }, "invalid_state_rejected");
        report.state = plex::PlaybackState::playing; report.continuing = true;
        rejects([&] { plex::timeline_path(report); }, "continuing_requires_stopped");
        check(plex::rating_path("42", 7.5) == "/:/rate?identifier=com.plexapp.plugins.library&key=42&rating=7.5", "rating_preserves_half_star_without_locale_comma");
        check(plex::rating_path("42", 0).find("rating=0") != std::string::npos, "unrated_value");
        check(plex::rating_path("42", 10).find("rating=10") != std::string::npos, "maximum_rating");
        for (const auto value : {-1.0, 10.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
            rejects([&] { plex::rating_path("42", value); }, "invalid_rating_rejected");
        for (const auto* identifier : {"", "../42", "42&rating=10", "42\r\nX-Plex-Token: wrong", "https://foreign.example"}) {
            report.identifier = identifier;
            rejects([&] { plex::rating_path(identifier, 5); }, "rating_identifier_injection_rejected");
            rejects([&] { plex::timeline_path(report); }, "timeline_identifier_injection_rejected");
        }
        plex::PlexConnectionSettings insecure; insecure.server_origin = "http://127.0.0.1:1";
        report = {"42", plex::PlaybackState::playing, 1000, 2000};
        rejects([&] { plex::report_timeline(insecure, report, nullptr); }, "timeline_never_sends_credentials_over_http");
        rejects([&] { plex::rate_item(insecure, "42", 5, nullptr); }, "rating_never_sends_credentials_over_http");
        std::cout << checks << " feedback checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
