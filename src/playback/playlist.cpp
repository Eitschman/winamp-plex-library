#include "plex/playlist.h"
#include "plex/security.h"
#include <stdexcept>

namespace plex {
std::string make_playlist(const std::vector<PlaylistEntry>& entries) {
    if (entries.empty() || entries.size() > 10000) throw std::invalid_argument("playlist_count");
    std::string result = "\xef\xbb\xbf#EXTM3U\r\n";
    for (const auto& entry : entries) {
        constexpr auto prefix = "http://127.0.0.1:";
        if (entry.url.substr(0, 17) != prefix || entry.url.size() > 256 || entry.seconds < -1 ||
            entry.title.empty() || entry.title.size() > 4096) throw std::invalid_argument("playlist_entry");
        const auto slash = entry.url.find('/', 17);
        if (slash == entry.url.npos) throw std::invalid_argument("playlist_url");
        parse_server_address(entry.url.substr(0, slash));
        const auto path = entry.url.substr(slash);
        if (path.substr(0, 8) != "/stream/" || path.size() < 64) throw std::invalid_argument("playlist_capability");
        for (const auto character : path)
            if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '/' || character == '.'))
                throw std::invalid_argument("playlist_url");
        auto title = entry.title;
        for (auto& character : title) if (static_cast<unsigned char>(character) < 32 || character == 127) character = ' ';
        result += "#EXTINF:" + std::to_string(entry.seconds) + "," + title + "\r\n" + entry.url + "\r\n";
        if (result.size() > 4 * 1024 * 1024) throw std::invalid_argument("playlist_limit");
    }
    return result;
}
}
