#pragma once
#include "plex/settings.h"
#include <windows.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace plex {
struct ArtworkPixels {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
};
void validate_artwork_path(const std::string& path);
// Der aufrufende Worker initialisiert COM. Decoder und Ausgabe bleiben größenbegrenzt.
ArtworkPixels decode_artwork(const std::vector<std::uint8_t>& bytes, unsigned edge = 48);
// Kanonisches BMP für die Winamp-Cover-Schnittstelle, ohne Serveradresse oder Token.
std::vector<std::uint8_t> encode_artwork_bmp(const ArtworkPixels& pixels);
// Fehlender thumb-Schlüssel oder HTTP 404 bedeutet kein Cover, andere Fehler bleiben sichtbar.
std::optional<ArtworkPixels> fetch_artwork(const PlexConnectionSettings& settings,
                                         const std::string& thumb, HANDLE stop, unsigned edge = 48);
}
