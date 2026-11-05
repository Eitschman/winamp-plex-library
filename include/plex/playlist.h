#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace plex {
struct PlaylistEntry {
    std::string url;
    std::string title;
    int seconds = -1;
    bool local_file = false;
};
// M3U8 überträgt Unicode-Metadaten ohne versionsabhängige Winamp-Strukturen.
std::string make_playlist(const std::vector<PlaylistEntry>& entries, std::string_view local_directory = {});
}
