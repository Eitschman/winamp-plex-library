#pragma once
#include "plex/browse_query.h"
#include <optional>

namespace plex {
// Auswahl und Suchtext sind flüchtig. Nur ausdrücklich gespeicherte Ansichten
// dürfen Einschränkungen beim erneuten Öffnen eines Bereichs wiederherstellen.
struct BrowserSelection {
    std::string artist, album, playlist;
    void clear() { artist.clear(); album.clear(); playlist.clear(); }
};
struct BrowserQueries {
    std::optional<BrowseQuery> primary, albums, tracks;
};
inline BrowserQueries browser_queries(const BrowseQuery& source, const BrowserSelection& selection) {
    BrowserQueries result;
    auto tracks = source;
    tracks.kind = BrowseKind::tracks;
    tracks.parent_identifier.clear(); tracks.playlist_items = false;
    tracks.artist_identifier.clear(); tracks.search_field = "title";
    if (source.kind == BrowseKind::tracks) { result.tracks = tracks; return result; }
    auto primary = source;
    primary.parent_identifier.clear(); primary.playlist_items = false;
    primary.artist_identifier.clear(); primary.search_field = "title";
    primary.offset = 0; primary.sort = "titleSort"; primary.descending = false;
    if (source.kind == BrowseKind::artists) {
        auto albums = primary;
        albums.kind = BrowseKind::albums; albums.search_field = "artist.title";
        albums.artist_identifier = selection.artist;
        result.albums = std::move(albums);
        tracks.artist_identifier = selection.artist;
        tracks.search_field = "artist.title";
        tracks.parent_identifier = selection.album;
    } else if (source.kind == BrowseKind::albums) {
        tracks.parent_identifier = selection.album;
        tracks.search_field = "album.title";
    } else {
        result.primary = std::move(primary);
        if (selection.playlist.empty()) return result;
        tracks.parent_identifier = selection.playlist; tracks.playlist_items = true;
        tracks.search.clear(); tracks.genre.clear(); tracks.year.clear(); tracks.audio_codec.clear();
        tracks.sort = "albumOrder";
        result.tracks = std::move(tracks); return result;
    }
    result.primary = std::move(primary); result.tracks = std::move(tracks);
    return result;
}
inline void reset_browser(BrowseQuery& query, BrowserSelection& selection) {
    const auto kind = query.kind;
    const auto library = query.library_identifier;
    query = BrowseQuery{}; query.kind = kind; query.library_identifier = library;
    selection.clear();
}
}
