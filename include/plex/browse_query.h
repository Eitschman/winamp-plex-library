#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace plex {
enum class BrowseKind { artists, albums, tracks, playlists };
struct BrowseQuery {
    BrowseKind kind = BrowseKind::artists;
    std::string library_identifier, parent_identifier, search, sort = "titleSort";
    bool playlist_items = false, descending = false;
    std::size_t offset = 0, limit = 250;
    std::string genre, year, audio_codec;
};
// UI-Zustände verwenden stabile Plex-IDs, keine Zeiger oder temporären URLs.
struct LibraryNavigation {
    BrowseQuery query;
    std::string caption;
    std::vector<std::size_t> offsets{0};
    std::vector<std::string> selected;
    std::string focused, top;
};
struct LibraryBookmark {
    std::string server, library;
    BrowseKind root = BrowseKind::artists;
    LibraryNavigation current;
    std::vector<LibraryNavigation> history;
};
struct SavedSearch {
    std::string server, library, name;
    BrowseQuery query;
};
struct LibraryPreferences {
    std::vector<LibraryBookmark> bookmarks;
    std::vector<SavedSearch> searches;
};
}
