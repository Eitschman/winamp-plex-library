#include "plex/playback_reporter.h"
#include "plex/http_stream.h"
#include <winhttp.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Predicate> void wait(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("reporter_test_timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
int main() {
    try {
        plex::TimelineTracker tracker;
        plex::PlaybackSnapshot sample{"own-playback-a", {"42", plex::PlaybackState::playing, 1000, 60000}};
        check(tracker.observe(sample, 1000).size() == 1, "first_playing_report");
        sample.timeline.time_ms = 1500;
        check(tracker.observe(sample, 1500).empty(), "normal_progress_not_reported_every_tick");
        sample.timeline.state = plex::PlaybackState::paused;
        auto reports = tracker.observe(sample, 1600);
        check(reports.size() == 1 && reports.front().state == plex::PlaybackState::paused, "pause_reported_without_waiting_ten_seconds");
        check(tracker.observe(sample, 11599).empty(), "periodic_interval_not_early");
        check(tracker.observe(sample, 11600).size() == 1, "paused_session_keepalive_after_ten_seconds");
        sample.timeline.state = plex::PlaybackState::playing;
        check(tracker.observe(sample, 11700).size() == 1, "resume_reported");
        sample.timeline.time_ms = 20000;
        check(tracker.observe(sample, 12200).size() == 1, "seeking_reported_promptly");
        sample.playback_key = "own-playback-b"; sample.timeline.identifier = "43"; sample.timeline.time_ms = 0;
        reports = tracker.observe(sample, 12300);
        check(reports.size() == 2 && reports[0].identifier == "42" && reports[0].state == plex::PlaybackState::stopped &&
              reports[0].continuing && reports[0].time_ms == 20000 && reports[1].identifier == "43", "track_change_stops_old_before_starting_new");
        reports = tracker.observe(std::nullopt, 13000);
        check(reports.size() == 1 && reports.front().state == plex::PlaybackState::stopped && !reports.front().continuing, "stop_or_foreign_file_ends_owned_session");
        check(tracker.observe(std::nullopt, 14000).empty(), "stopped_state_not_repeated");
        tracker.reset();
        sample.timeline.state = plex::PlaybackState::playing;
        check(tracker.observe(sample, 0).size() == 1, "tracker_reset_for_new_connection");
        bool rejected = false;
        sample.timeline.time_ms = -1;
        try { tracker.observe(sample, 500); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "invalid_player_position_rejected");
        sample.timeline.time_ms = 0;

        std::vector<plex::TimelineReport> sent;
        std::mutex sent_mutex;
        {
            plex::PlaybackReporter worker([&](const auto& report, HANDLE) {
                std::lock_guard<std::mutex> lock(sent_mutex); sent.push_back(report);
            });
            check(worker.submit(sample.timeline), "worker_accepts_playing");
            auto stopped = sample.timeline; stopped.state = plex::PlaybackState::stopped;
            check(worker.submit(stopped), "worker_accepts_final_stop");
        }
        check(sent.size() == 2 && sent[0].state == plex::PlaybackState::playing && sent[1].state == plex::PlaybackState::stopped,
              "shutdown_drains_final_stop_in_order");
        {
            std::atomic<unsigned> calls{0};
            plex::PlaybackReporter worker([&](const auto&, HANDLE) { ++calls; throw plex::HttpError(0x20000000UL + 401); });
            worker.submit(sample.timeline);
            wait([&] { return worker.status().error == 0x20000000UL + 401; });
            check(!worker.submit(sample.timeline) && calls.load() == 1, "revoked_token_stops_automatic_reporting_without_alternate_credentials");
        }
        {
            std::atomic<unsigned> calls{0};
            plex::PlaybackReporter worker([&](const auto&, HANDLE) {
                if (++calls == 1) throw plex::HttpError(ERROR_WINHTTP_TIMEOUT);
            });
            worker.submit(sample.timeline);
            wait([&] { return worker.status().error == ERROR_WINHTTP_TIMEOUT; });
            check(worker.submit(sample.timeline), "new_player_state_accepted_after_transient_failure");
            wait([&] { return worker.status().sent == 1 && worker.status().error == 0; });
            check(calls.load() == 2, "successful_report_clears_transient_error_without_changing_credentials");
        }
        const auto start = std::chrono::steady_clock::now();
        {
            std::promise<void> entered;
            plex::PlaybackReporter worker([&](const auto&, HANDLE stop) {
                entered.set_value(); WaitForSingleObject(stop, INFINITE); throw plex::HttpError(ERROR_CANCELLED);
            });
            worker.submit(sample.timeline);
            auto ready = entered.get_future();
            check(ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "stalled_sender_started");
            ready.get();
            for (unsigned index = 0; index < 64; ++index) check(worker.submit(sample.timeline), "bounded_queue_accepts_capacity");
            check(!worker.submit(sample.timeline) && worker.status().error == ERROR_NOT_ENOUGH_QUOTA, "bounded_queue_rejects_overflow");
        }
        check(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "stalled_request_cancelled_during_shutdown");
        std::cout << checks << " playback reporter checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
