#pragma once
#include "plex/feedback.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace plex {
struct PlaybackSnapshot {
    std::string playback_key;
    TimelineReport timeline;
};
// Reine Zustandslogik: Winamp-Abfragen und Netzwerkzugriffe gehören den Aufrufern.
class TimelineTracker {
public:
    std::vector<TimelineReport> observe(std::optional<PlaybackSnapshot> snapshot, unsigned long long now_ms);
    void reset() noexcept;
private:
    std::optional<PlaybackSnapshot> previous_;
    unsigned long long observed_at_ = 0, reported_at_ = 0;
};
struct ReporterStatus { unsigned long long sent = 0; DWORD error = 0; };
class PlaybackReporter {
public:
    using Sender = std::function<void(const TimelineReport&, HANDLE)>;
    explicit PlaybackReporter(PlexConnectionSettings settings);
    explicit PlaybackReporter(Sender sender);
    ~PlaybackReporter();
    PlaybackReporter(const PlaybackReporter&) = delete;
    PlaybackReporter& operator=(const PlaybackReporter&) = delete;
    bool submit(TimelineReport report);
    ReporterStatus status() const noexcept;
private:
    void run() noexcept;
    Sender sender_;
    HANDLE stop_ = nullptr, idle_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<TimelineReport> pending_;
    bool closing_ = false;
    std::atomic<bool> authentication_failed_{false};
    std::atomic<unsigned long long> sent_{0};
    std::atomic<DWORD> error_{0};
    std::thread worker_;
};
}
