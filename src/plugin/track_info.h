#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace plex::plugin {
// Ausschließlich sichtbare Musikdaten, keine Verbindung, URLs oder Zugangsdaten.
struct TrackInformation {
    std::wstring title, artist, album, album_artist, genre, year, track, duration, format;
};
bool show_track_information(HWND parent, const TrackInformation& information);

// Eigene gebundene Titel erhalten den nativen Plex-Dialog. Fremde Dateien und
// Radios bleiben bei Winamps Decoder; gemischte Mehrfachauswahl behält ihre Reihenfolge.
class TrackInformationController {
public:
    using Lookup = std::function<std::optional<TrackInformation>(const std::wstring&)>;
    using Show = std::function<bool(HWND, const TrackInformation&)>;
    TrackInformationController(HWND player, Lookup lookup, Show show);
    ~TrackInformationController();
    TrackInformationController(const TrackInformationController&) = delete;
    TrackInformationController& operator=(const TrackInformationController&) = delete;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
