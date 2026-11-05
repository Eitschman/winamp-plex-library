#include "view_reaper.h"
#include <objbase.h>
#include <future>
#include <stdexcept>

namespace plex::plugin {
ViewReaper::ViewReaper() {
    std::promise<HRESULT> initialized;
    auto ready = initialized.get_future();
    worker_ = std::thread([this, result = std::move(initialized)]() mutable {
        // Die COM-Lebensdauer umfasst auch Lücken zwischen Ansichtsworkern.
        // Initialisierung und Freigabe bleiben auf demselben eigenen Thread.
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        result.set_value(apartment);
        if (FAILED(apartment)) return;
        run();
        CoUninitialize();
    });
    if (FAILED(ready.get())) {
        worker_.join();
        throw std::runtime_error("view_cleanup_com");
    }
}

ViewReaper::~ViewReaper() {
    { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void ViewReaper::retire(RetiredView* view) noexcept {
    if (!view) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++pending_;
        if (last_) last_->next_ = view;
        else first_ = view;
        last_ = view;
    }
    wake_.notify_one();
}

void ViewReaper::run() noexcept {
    for (;;) {
        RetiredView* view = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return closing_ || first_; });
            if (!first_) return;
            view = first_;
            first_ = view->next_;
            if (!first_) last_ = nullptr;
        }
        delete view;
        --pending_;
    }
}
}
