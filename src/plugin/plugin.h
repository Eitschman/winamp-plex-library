#pragma once

#include <windows.h>
#include <gen_ml/ml.h>
#include "plex/proxy.h"
#include "plex/settings.h"
#include "plex/catalog.h"
#include <memory>
#include "artwork_provider.h"

namespace plex::plugin {
winampMediaLibraryPlugin& descriptor() noexcept;
bool german() noexcept;
void set_german(bool enabled) noexcept;
void enqueue_tracks(const std::vector<PlexTrack>& tracks, bool play, const PlaybackArtwork& artwork = {});
void stop_playback() noexcept;
ProxyStatus playback_status() noexcept;
bool playback_active() noexcept;
HWND create_view(HWND parent, BrowseKind kind = BrowseKind::artists) noexcept;
INT_PTR library_view_message(HWND dialog, UINT message, WPARAM parameter, LPARAM detail);
void destroy_views() noexcept;
void show_login(HWND parent) noexcept;
PlexConnectionSettings connection_settings();
void update_connection_settings(PlexConnectionSettings settings);
void clear_library_cache();
}
