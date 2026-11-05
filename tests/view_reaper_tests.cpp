#include "ui/view_reaper.h"
#include <objbase.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>

namespace {
void check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
struct State {
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> timed_out{false};
    std::atomic<bool> apartment_valid{true};
    std::vector<unsigned> deleted;
    ~State() { CloseHandle(entered); CloseHandle(release); CloseHandle(closed); }
};
struct Task final : plex::plugin::RetiredView {
    State& state;
    unsigned identifier;
    Task(State& value, unsigned number) : state(value), identifier(number) {}
    ~Task() override {
        APTTYPE apartment; APTTYPEQUALIFIER qualifier;
        if (FAILED(CoGetApartmentType(&apartment, &qualifier)) || apartment != APTTYPE_MTA)
            state.apartment_valid.store(false);
        if (identifier == 0) {
            SetEvent(state.entered);
            if (WaitForSingleObject(state.release, 1500) != WAIT_OBJECT_0) state.timed_out.store(true);
        }
        state.deleted.push_back(identifier);
    }
};
}

int main() {
    try {
        State state;
        check(state.entered && state.release && state.closed, "test_events");
        auto reaper = std::make_unique<plex::plugin::ViewReaper>();
        const auto started = GetTickCount64();
        reaper->retire(new Task(state, 0));
        const bool immediate = GetTickCount64() - started < 250;
        const bool active = WaitForSingleObject(state.entered, 2000) == WAIT_OBJECT_0;
        for (unsigned identifier = 1; identifier < 8; ++identifier) reaper->retire(new Task(state, identifier));
        const bool bounded = !reaper->available();
        std::thread shutdown([&] { reaper.reset(); SetEvent(state.closed); });
        const bool waits = WaitForSingleObject(state.closed, 100) == WAIT_TIMEOUT;
        SetEvent(state.release);
        shutdown.join();
        check(immediate && active && !state.timed_out.load(), "retire_does_not_block_ui");
        check(bounded, "retired_views_are_bounded");
        check(waits && WaitForSingleObject(state.closed, 0) == WAIT_OBJECT_0, "shutdown_joins_pending_cleanup");
        check(state.apartment_valid.load(), "cleanup_uses_owned_mta");
        check(state.deleted == std::vector<unsigned>({0, 1, 2, 3, 4, 5, 6, 7}), "cleanup_drains_fifo");
        std::cout << "5 view cleanup checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
