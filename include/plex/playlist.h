#pragma once
#include <string>
#include <vector>

namespace plex {
struct PlaylistEntry {
    std::string url;
    std::string title;
    int seconds = -1;
};
// M3U8 überträgt Unicode-Metadaten ohne versionsabhängige Winamp-Strukturen.
std::string make_playlist(const std::vector<PlaylistEntry>& entries);
}
