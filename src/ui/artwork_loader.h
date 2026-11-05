#pragma once
#include "plex/artwork.h"
#include "plex/catalog.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>

namespace plex::plugin {
constexpr UINT artwork_ready = WM_APP + 43;
struct ArtworkResult {
    std::size_t row = 0;
    unsigned generation = 0;
    std::optional<ArtworkPixels> pixels;
    std::optional<unsigned> track_count;
    std::optional<long long> duration_ms;
    DWORD error = 0;
};

// Ein eigener begrenzter Worker hält Cover-Anfragen von Navigation und UI fern.
class ArtworkLoader {
public:
    explicit ArtworkLoader(HWND owner);
    ~ArtworkLoader();
    void cancel();
    void reset(unsigned generation);
    void submit(std::size_t row, PlexAlbum album, PlexConnectionSettings settings, std::string cache_scope);
    void submit_playlist(std::size_t row, PlexPlaylist playlist, PlexConnectionSettings settings, std::string cache_scope);
    std::deque<ArtworkResult> take();
    static void clear_cache();
private:
    struct Job { std::size_t row; unsigned generation; PlexAlbum album; std::string key; PlexConnectionSettings settings; bool playlist = false; };
    struct Cached {
        std::string key;
        std::optional<ArtworkPixels> pixels;
        std::optional<unsigned> track_count;
        std::optional<long long> duration_ms;
        std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    };
    void run() noexcept;
    HWND owner_;
    HANDLE stop_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool closing_ = false;
    unsigned generation_ = 0;
    std::deque<Job> pending_;
    std::deque<ArtworkResult> completed_;
    // Die begrenzten Bilddaten überleben kurzlebige Ansichts-Worker.
    static std::mutex cache_mutex_;
    static std::deque<Cached> cache_;
    static unsigned long long cache_generation_;
};
}
