#pragma once
#include "plex/local_media.h"
#include <condition_variable>
#include <exception>
#include <functional>
#include <optional>
#include <thread>

namespace plex {
// Ein aktiver HTTP-Body besitzt genau einen Produzenten und einen Leser.
// Nur vollständig geschriebene Bytes werden veröffentlicht. Netzwerkfehler
// bleiben nach dem letzten verfügbaren Byte als Fehler erhalten.
class StreamBuffer {
public:
    explicit StreamBuffer(HANDLE stop);
    ~StreamBuffer();
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;
    HANDLE cancellation() const noexcept { return cancelled_; }
    void start(std::shared_ptr<LocalMediaBuffer> file, std::function<void(StreamBuffer&)> producer);
    void append(const char* data, unsigned long size);
    // nullopt bedeutet: noch nicht genug Daten. Der Aufrufer kann in diesem
    // kurzen Intervall eine geschlossene Decoderverbindung erkennen.
    std::optional<unsigned long> read(char* data, unsigned long capacity);
private:
    HANDLE stop_, cancelled_;
    std::shared_ptr<LocalMediaBuffer> file_;
    std::thread producer_;
    std::mutex mutex_;
    std::condition_variable changed_;
    unsigned long long published_ = 0, consumed_ = 0;
    bool finished_ = false, started_reading_ = false;
    std::exception_ptr failure_;
};
}
