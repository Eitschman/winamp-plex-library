#include "plex/playback_reporter.h"
#include "plex/http_stream.h"
#include <stdexcept>

namespace plex {
std::vector<TimelineReport> TimelineTracker::observe(std::optional<PlaybackSnapshot> snapshot, unsigned long long now_ms) {
    if (previous_ && now_ms < observed_at_) throw std::invalid_argument("timeline_clock");
    if (snapshot) {
        timeline_path(snapshot->timeline);
        if (snapshot->playback_key.empty() || snapshot->timeline.state == PlaybackState::stopped || snapshot->timeline.continuing)
            throw std::invalid_argument("timeline_snapshot");
    }
    std::vector<TimelineReport> reports;
    const bool changed = previous_ && (!snapshot || snapshot->playback_key != previous_->playback_key);
    if (changed) {
        auto stopped = previous_->timeline;
        stopped.state = PlaybackState::stopped; stopped.continuing = snapshot.has_value();
        reports.push_back(std::move(stopped));
    }
    if (snapshot) {
        bool send = !previous_ || changed || snapshot->timeline.state != previous_->timeline.state;
        if (!send) {
            const auto elapsed = now_ms - observed_at_;
            const auto expected = previous_->timeline.state == PlaybackState::playing ? static_cast<long long>(elapsed) : 0LL;
            const auto movement = snapshot->timeline.time_ms - previous_->timeline.time_ms;
            // Seeking wird zeitnah gemeldet; normale Decoder-/Timerabweichungen
            // unter zwei Sekunden erzeugen keine zusätzlichen Requests.
            const bool seek = movement > expected + 2000 || movement < expected - 2000;
            send = seek || now_ms - reported_at_ >= 10000;
        }
        if (send) { reports.push_back(snapshot->timeline); reported_at_ = now_ms; }
    }
    previous_ = std::move(snapshot); observed_at_ = now_ms;
    return reports;
}
void TimelineTracker::reset() noexcept { previous_.reset(); observed_at_ = 0; reported_at_ = 0; }
PlaybackReporter::PlaybackReporter(PlexConnectionSettings settings)
    : PlaybackReporter([settings = std::move(settings)](const auto& report, HANDLE stop) { report_timeline(settings, report, stop); }) {}
PlaybackReporter::PlaybackReporter(Sender sender) : sender_(std::move(sender)) {
    if (!sender_) throw std::invalid_argument("timeline_sender");
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    idle_ = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!stop_ || !idle_) {
        if (stop_) CloseHandle(stop_);
        if (idle_) CloseHandle(idle_);
        throw HttpError(ERROR_NOT_ENOUGH_MEMORY);
    }
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { CloseHandle(stop_); CloseHandle(idle_); throw; }
}
PlaybackReporter::~PlaybackReporter() {
    { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; }
    wake_.notify_one();
    // Nur beim Abbau kurz auf die letzte Stopmeldung warten. Danach laufende
    // Requests abbrechen und den Thread vor dem Entladen der DLL verbinden.
    WaitForSingleObject(idle_, 500);
    SetEvent(stop_); wake_.notify_one();
    if (worker_.joinable()) worker_.join();
    CloseHandle(idle_); CloseHandle(stop_);
}
bool PlaybackReporter::submit(TimelineReport report) {
    timeline_path(report);
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_ || authentication_failed_.load()) return false;
    if (pending_.size() >= 64) { error_.store(ERROR_NOT_ENOUGH_QUOTA); return false; }
    pending_.push_back(std::move(report)); ResetEvent(idle_); wake_.notify_one(); return true;
}
ReporterStatus PlaybackReporter::status() const noexcept { return {sent_.load(), error_.load()}; }
void PlaybackReporter::run() noexcept {
    try {
    for (;;) {
        TimelineReport report;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return closing_ || !pending_.empty(); });
            if ((closing_ && pending_.empty()) || WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) {
                pending_.clear(); SetEvent(idle_); return;
            }
            report = std::move(pending_.front()); pending_.pop_front();
        }
        try {
            sender_(report, stop_); ++sent_; error_.store(0);
        } catch (const HttpError& failure) {
            if (failure.code == 0x20000000UL + 401 || failure.code == 0x20000000UL + 403) authentication_failed_.store(true);
            error_.store(failure.code);
        } catch (...) { error_.store(ERROR_INVALID_DATA); }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (authentication_failed_.load()) pending_.clear();
            if (pending_.empty()) SetEvent(idle_);
        }
    }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true; pending_.clear();
        error_.store(ERROR_INVALID_DATA); SetEvent(idle_);
    }
}
}
