#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
class api_service;

namespace plex::plugin {
using ArtworkBytes = std::shared_ptr<const std::vector<std::uint8_t>>;
using PlaybackArtwork = std::unordered_map<std::string, ArtworkBytes>;
void initialize_artwork_provider(api_service* services);
void shutdown_artwork_provider() noexcept;
void bind_playback_artwork(const std::wstring& url, ArtworkBytes bytes);
// Gemeinsames, unveränderliches Bild für native Titelinformationen. Kein HTTP.
ArtworkBytes playback_artwork(const std::wstring& filename);
void unbind_playback_artwork(const std::wstring& url) noexcept;
void clear_playback_artwork() noexcept;
// Nur numerische Diagnosezähler, ohne Medienpfade oder Zugangsdaten.
unsigned artwork_provider_diagnostic(unsigned counter) noexcept;
constexpr UINT artwork_diagnostic_message = WM_APP + 44;
}
