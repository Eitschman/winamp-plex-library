#include "artwork_loader.h"
#include "plex/http_stream.h"
#include <objbase.h>
#include <algorithm>
#include <stdexcept>

namespace plex::plugin {
std::mutex ArtworkLoader::cache_mutex_;
std::deque<ArtworkLoader::Cached> ArtworkLoader::cache_;
unsigned long long ArtworkLoader::cache_generation_ = 0;

void ArtworkLoader::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    ++cache_generation_; cache_.clear();
}

ArtworkLoader::ArtworkLoader(HWND owner) : owner_(owner), stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!stop_) throw std::runtime_error("artwork_event");
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { CloseHandle(stop_); throw; }
}
ArtworkLoader::~ArtworkLoader() {
    { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; SetEvent(stop_); }
    wake_.notify_one(); if (worker_.joinable()) worker_.join(); CloseHandle(stop_);
}
void ArtworkLoader::reset(unsigned generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = generation; pending_.clear(); completed_.clear(); SetEvent(stop_);
}
void ArtworkLoader::submit(std::size_t row, PlexAlbum album, PlexConnectionSettings settings, std::string cache_scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() >= 500) throw std::invalid_argument("artwork_queue_limit");
    auto key = std::move(cache_scope) + "|" + settings.server_identifier + "|" + album.identifier + "|" + album.thumb;
    pending_.push_back({row, generation_, std::move(album), std::move(key), std::move(settings)});
    wake_.notify_one();
}
std::deque<ArtworkResult> ArtworkLoader::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(completed_); completed_.clear(); return result;
}
// Derselbe begrenzte Worker aktualisiert sichtbare Playlist-Zähler ohne Cover-Zeilenhöhe.
void ArtworkLoader::submit_playlist(std::size_t row, PlexPlaylist playlist, PlexConnectionSettings settings, std::string cache_scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() >= 500) throw std::invalid_argument("artwork_queue_limit");
    auto key = std::move(cache_scope) + "|playlist|" + settings.server_identifier + "|" + playlist.identifier;
    PlexAlbum reference; reference.identifier = std::move(playlist.identifier);
    pending_.push_back({row, generation_, std::move(reference), std::move(key), std::move(settings), true});
    wake_.notify_one();
}
void ArtworkLoader::run() noexcept {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return closing_ || !pending_.empty(); });
                if (closing_) break;
                job = std::move(pending_.front()); pending_.pop_front(); ResetEvent(stop_);
            }
            ArtworkResult result; result.row = job.row; result.generation = job.generation;
            try {
                if (FAILED(initialized)) throw HttpError(static_cast<DWORD>(initialized));
                std::optional<Cached> cached;
                unsigned long long cache_generation = 0;
                if (job.settings.cache_enabled) {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    cache_generation = cache_generation_;
                    const auto now = std::chrono::steady_clock::now();
                    cache_.erase(std::remove_if(cache_.begin(), cache_.end(), [&](const auto& entry) { return now >= entry.expires; }), cache_.end());
                    const auto found = std::find_if(cache_.begin(), cache_.end(), [&](const auto& entry) { return entry.key == job.key; });
                    if (found != cache_.end()) {
                        auto value = std::move(*found); cache_.erase(found);
                        cache_.push_front(std::move(value)); cached = cache_.front();
                    }
                }
                if (cached) {
                    result.pixels = std::move(cached->pixels); result.track_count = cached->track_count; result.duration_ms = cached->duration_ms;
                } else {
                    if (job.playlist) {
                        const auto details = PlexCatalog(job.settings, stop_).playlist(job.album.identifier);
                        result.track_count = details.track_count; result.duration_ms = details.duration_ms;
                    } else {
                        if (job.album.track_count) result.track_count = job.album.track_count;
                        else {
                            // Manche PMS-Antworten lassen leafCount weg. Eine einzige Titelzeile
                            // reicht aus, um die tatsächliche Gesamtzahl des Albums abzufragen.
                            BrowseQuery query; query.kind = BrowseKind::tracks; query.parent_identifier = job.album.identifier;
                            query.sort = "albumOrder"; query.limit = 1;
                            const auto page = PlexCatalog(job.settings, stop_).page(query);
                            if (page.total) result.track_count = static_cast<unsigned>(*page.total);
                            else if (!page.more) result.track_count = static_cast<unsigned>(page.items.size());
                        }
                        result.pixels = fetch_artwork(job.settings, job.album.thumb, stop_);
                    }
                    if (job.settings.cache_enabled) {
                        std::lock_guard<std::mutex> lock(cache_mutex_);
                        // Ein vor dem Leeren gestarteter Abruf darf den Cache nicht wieder befüllen.
                        if (cache_generation == cache_generation_) {
                            cache_.erase(std::remove_if(cache_.begin(), cache_.end(), [&](const auto& entry) { return entry.key == job.key; }), cache_.end());
                            cache_.push_front({job.key, result.pixels, result.track_count, result.duration_ms});
                            if (cache_.size() > 64) cache_.pop_back();
                        }
                    }
                }
            } catch (const HttpError& error) { result.error = error.code; }
              catch (...) { result.error = ERROR_INVALID_DATA; }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closing_) break;
                if (job.generation != generation_) continue;
                completed_.push_back(std::move(result));
            }
            PostMessageW(owner_, artwork_ready, 0, 0);
        }
    } catch (...) { PostMessageW(owner_, artwork_ready, ERROR_NOT_ENOUGH_MEMORY, 0); }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
}
