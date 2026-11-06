#include "plex/stream_buffer.h"
#include "plex/http_stream.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace plex {
StreamBuffer::StreamBuffer(HANDLE stop) : stop_(stop), cancelled_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!stop_ || !cancelled_) {
        if (cancelled_) CloseHandle(cancelled_);
        throw HttpError(ERROR_INVALID_HANDLE);
    }
}
StreamBuffer::~StreamBuffer() {
    SetEvent(cancelled_);
    changed_.notify_all();
    if (producer_.joinable()) producer_.join();
    CloseHandle(cancelled_);
}
void StreamBuffer::start(std::shared_ptr<LocalMediaBuffer> file, std::function<void(StreamBuffer&)> producer) {
    if (file_ || !file || !producer) throw std::invalid_argument("stream_buffer_start");
    file_ = std::move(file);
    producer_ = std::thread([this, producer = std::move(producer)] {
        std::exception_ptr failure;
        try { producer(*this); } catch (...) { failure = std::current_exception(); }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_ = failure; finished_ = true;
        }
        changed_.notify_all();
    });
}
void StreamBuffer::append(const char* data, unsigned long size) {
    if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 || WaitForSingleObject(cancelled_, 0) == WAIT_OBJECT_0)
        throw HttpError(ERROR_CANCELLED);
    file_->append(data, size);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        published_ += size;
    }
    changed_.notify_one();
}
std::optional<unsigned long> StreamBuffer::read(char* data, unsigned long capacity) {
    if (!file_ || !capacity) throw std::invalid_argument("stream_buffer_read");
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [&] {
        return finished_ || published_ - consumed_ >= (started_reading_ ? 1ULL : 128ULL * 1024);
    };
    changed_.wait_for(lock, std::chrono::milliseconds(100), ready);
    if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 || WaitForSingleObject(cancelled_, 0) == WAIT_OBJECT_0)
        throw HttpError(ERROR_CANCELLED);
    if (!ready()) return std::nullopt;
    if (published_ == consumed_) {
        if (failure_) std::rethrow_exception(failure_);
        return 0;
    }
    const auto count = static_cast<unsigned long>((std::min)(published_ - consumed_, static_cast<unsigned long long>(capacity)));
    // Der Produzent benötigt diesen Mutex erst nach dem Dateischreiben. Den
    // Dateizugriff deshalb ohne gehaltenen Veröffentlichungs-Mutex ausführen.
    const auto offset = consumed_;
    lock.unlock();
    const auto actual = file_->read(offset, data, count);
    lock.lock();
    consumed_ += actual; started_reading_ = true;
    return actual;
}
}
