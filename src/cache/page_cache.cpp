#include "plex/page_cache.h"
#include <algorithm>
#include <stdexcept>

namespace plex {
PageCache::PageCache(std::size_t capacity, std::chrono::seconds lifetime) : capacity_(capacity), lifetime_(lifetime) {
    if (!capacity || capacity > 128 || lifetime.count() < 0) throw std::invalid_argument("cache_settings");
}
std::optional<CatalogPage> PageCache::get(const std::string& key, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.key == key; });
    if (found == entries_.end()) return std::nullopt;
    if (now >= found->expires) { entries_.erase(found); return std::nullopt; }
    entries_.splice(entries_.begin(), entries_, found);
    return entries_.front().page;
}
void PageCache::put(std::string key, CatalogPage page, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.remove_if([&](const auto& entry) { return entry.key == key; });
    entries_.push_front({std::move(key), std::move(page), now + lifetime_});
    while (entries_.size() > capacity_) entries_.pop_back();
}
void PageCache::clear() { std::lock_guard<std::mutex> lock(mutex_); entries_.clear(); }

LibraryCache::LibraryCache(std::chrono::seconds lifetime) : lifetime_(lifetime) {
    if (lifetime.count() < 0) throw std::invalid_argument("cache_settings");
}
std::vector<PlexLibrary> LibraryCache::load(const std::string& key, bool enabled,
    const std::function<std::vector<PlexLibrary>()>& fetch, Clock::time_point now) {
    if (!enabled) return fetch();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entry_ && entry_->key == key && now < entry_->expires) return entry_->libraries;
    }
    // Netzwerkzugriffe bleiben außerhalb der Sperre. Fehler werden weitergegeben;
    // eine abgelaufene Liste ersetzt keine fehlgeschlagene Serverantwort.
    auto libraries = fetch();
    put(key, libraries, now);
    return libraries;
}
void LibraryCache::put(std::string key, std::vector<PlexLibrary> libraries, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    entry_ = Entry{std::move(key), std::move(libraries), now + lifetime_};
}
void LibraryCache::clear() { std::lock_guard<std::mutex> lock(mutex_); entry_.reset(); }
}
