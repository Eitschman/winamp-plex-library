#pragma once
#include "plex/catalog.h"
#include <chrono>
#include <list>
#include <mutex>

namespace plex {
// Begrenzter Speicher-Cache für Metadaten; keine Token und keine Audiodateien.
class PageCache {
public:
    using Clock = std::chrono::steady_clock;
    explicit PageCache(std::size_t capacity = 32, std::chrono::seconds lifetime = std::chrono::minutes(5));
    std::optional<CatalogPage> get(const std::string& key, Clock::time_point now = Clock::now());
    void put(std::string key, CatalogPage page, Clock::time_point now = Clock::now());
    void clear();
private:
    struct Entry { std::string key; CatalogPage page; Clock::time_point expires; };
    std::size_t capacity_;
    std::chrono::seconds lifetime_;
    std::mutex mutex_;
    std::list<Entry> entries_;
};
}
