#include "artwork_loader.h"
#include "plex/http_stream.h"
#include <objbase.h>
#include <algorithm>
#include <stdexcept>

namespace plex::plugin {
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
                const auto cached = std::find_if(cache_.begin(), cache_.end(), [&](const auto& entry) {
                    return entry.key == job.key && std::chrono::steady_clock::now() < entry.expires;
                });
                if (job.settings.cache_enabled && cached != cache_.end()) {
                    auto value = std::move(*cached); cache_.erase(cached);
                    result.pixels = value.pixels; result.track_count = value.track_count; cache_.push_front(std::move(value));
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
                    if (job.settings.cache_enabled) {
                        cache_.push_front({job.key, result.pixels, result.track_count});
                        if (cache_.size() > 64) cache_.pop_back();
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
