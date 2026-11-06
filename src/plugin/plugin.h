#pragma once

#include <windows.h>
#include <gen_ml/ml.h>
#include "plex/proxy.h"
#include "plex/settings.h"
#include "plex/local_media.h"
#include "plex/catalog.h"
#include "plex/playback_reporter.h"
#include "plex/winamp_playlist.h"
#include <memory>
#include "artwork_provider.h"

namespace plex::plugin {
winampMediaLibraryPlugin& descriptor() noexcept;
bool german() noexcept;
void enqueue_tracks(const std::vector<PlexTrack>& tracks, bool play, const PlaybackArtwork& artwork = {}, const PreparedMedia& prepared = {}, PlaylistPlacement placement = PlaylistPlacement::append);
std::shared_ptr<LocalMediaStore> local_media_store();
void stop_playback() noexcept;
ProxyStatus playback_status() noexcept;
ReporterStatus playback_reporting_status() noexcept;
constexpr UINT playback_diagnostic_message = WM_APP + 45;
unsigned long long playback_diagnostic(unsigned counter) noexcept;
bool playback_active() noexcept;
HWND create_view(HWND parent, BrowseKind kind = BrowseKind::artists) noexcept;
INT_PTR library_view_message(HWND dialog, UINT message, WPARAM parameter, LPARAM detail);
void destroy_views() noexcept;
void initialize_library_workers();
void shutdown_library_workers() noexcept;
bool show_settings(HWND parent) noexcept;
PlexConnectionSettings connection_settings();
PlexConnectionSettings request_connection_settings(bool include_connections = false);
unsigned long long connection_revision() noexcept;
bool connection_verified() noexcept;
bool accept_verified_connection(unsigned long long revision, const std::string& origin);
void update_connection_settings(PlexConnectionSettings settings);
void update_library_preferences(LibraryPreferences preferences);
bool show_library_panel(HWND parent, int resource, DLGPROC procedure, LPARAM context, const wchar_t* caption);
void clear_library_cache();
void skin_library_dialog(HWND dialog);
}
