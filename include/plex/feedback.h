#pragma once
#include "plex/settings.h"
#include <windows.h>
#include <string>

namespace plex {
enum class PlaybackState { playing, paused, stopped, buffering };
struct TimelineReport {
    std::string identifier;
    PlaybackState state = PlaybackState::stopped;
    long long time_ms = 0;
    long long duration_ms = 0;
    bool continuing = false;
};
std::string timeline_path(const TimelineReport& report);
std::string rating_path(const std::string& identifier, double rating);
// Rückgabestatus allein beweist keine Serverwirkung. Integrationstests lesen
// Sitzungszustand, Metadaten und Verlauf über die getrennten Leseendpunkte zurück.
void report_timeline(const PlexConnectionSettings& settings, const TimelineReport& report, HANDLE stop);
void rate_item(const PlexConnectionSettings& settings, const std::string& identifier, double rating, HANDLE stop);
}
