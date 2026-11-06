#pragma once
#include "plugin/artwork_provider.h"
#include "plex/catalog.h"
#include "plex/local_media.h"
#include "plex/playlist.h"
#include "plex/replay_references.h"
#include <functional>

namespace plex::plugin {
struct ReplayTrack { ReplayReference reference; PlexTrack track; };
// Der Worker besitzt den dauerhaften Index; ausschließlich der Host-Thread liest
// oder verändert Winamps Playlist. Fertige Ergebnisse passieren eine zweite Prüfung.
class PlaylistReplay {
public:
    using Bind = std::function<std::vector<PlaylistEntry>(const std::vector<ReplayTrack>&, const PlexConnectionSettings&, const PreparedMedia&, const PlaybackArtwork&)>;
    PlaylistReplay(HWND player, HWND notification, HINSTANCE instance,
                   std::function<bool(const std::wstring&)> active, Bind bind,
                   std::function<void(const std::vector<PlaylistEntry>&)> release);
    ~PlaylistReplay();
    void persist(std::vector<ReplayReference> references, std::function<void()> accepted, std::function<void()> rejected);
    void dispatch() noexcept;
    void cancel() noexcept;
    unsigned long long diagnostic(unsigned counter) const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
constexpr UINT replay_ready_message = WM_APP + 46;
}
