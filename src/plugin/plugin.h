#pragma once

#include <windows.h>
#include <gen_ml/ml.h>
#include "plex/proxy.h"
#include "plex/settings.h"
#include <memory>

namespace plex::plugin {
winampMediaLibraryPlugin& descriptor() noexcept;
bool german() noexcept;
void set_german(bool enabled) noexcept;
void start_playback(PlaybackSource source, const std::wstring& title, int seconds);
void stop_playback() noexcept;
ProxyStatus playback_status() noexcept;
bool playback_active() noexcept;
HWND create_view(HWND parent) noexcept;
void show_playback_test(HWND parent) noexcept;
void destroy_views() noexcept;
void show_login(HWND parent) noexcept;
PlexConnectionSettings connection_settings();
void update_connection_settings(PlexConnectionSettings settings);
}
