#pragma once
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>

namespace plex::plugin {
class ViewReaper;

// Die UI trennt ihre Fensterressourcen vor der Übergabe ab. Der Hintergrund
// besitzt anschließend ausschließlich den abzubrechenden Worker-Zustand.
class RetiredView {
public:
    virtual ~RetiredView() = default;
private:
    friend class ViewReaper;
    RetiredView* next_ = nullptr;
};

class ViewReaper {
public:
    ViewReaper();
    ~ViewReaper();
    ViewReaper(const ViewReaper&) = delete;
    ViewReaper& operator=(const ViewReaper&) = delete;
    void retire(RetiredView* view) noexcept;
    // Nur der UI-Thread erzeugt Ansichten. Der laufende Abbau zählt mit,
    // damit rasche Wechsel bei langsamer Freigabe keine Workerflut erzeugen.
    bool available() const noexcept { return pending_.load() < 8; }
private:
    void run() noexcept;
    std::mutex mutex_;
    std::condition_variable wake_;
    RetiredView* first_ = nullptr;
    RetiredView* last_ = nullptr;
    bool closing_ = false;
    std::atomic<unsigned> pending_{0};
    std::thread worker_;
};
}
