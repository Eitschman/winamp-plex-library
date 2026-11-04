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
}
