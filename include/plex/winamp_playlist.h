#pragma once
#include <windows.h>
#include <cstddef>
#include <string>

namespace plex {
enum class PlaylistPlacement { append, after_current };
// Übergibt eine vorbereitete M3U8 synchron auf dem Winamp-UI-Thread. Der Host
// behält Wiedergabezustand und aktuellen Titel; zurückgegeben wird der Startindex.
int send_playlist_to_winamp(HWND player, const std::wstring& path, std::size_t count, PlaylistPlacement placement);
}
