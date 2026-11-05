#pragma once

#include "plex/security.h"
#include <memory>
#include <string>
#include <chrono>

namespace plex {
struct PlaybackSource {
    ServerAddress server;
    std::string token;
    std::string media_path;
    ~PlaybackSource();
};

struct ProxyStatus {
    unsigned long upstream_status = 0;
    unsigned long transport_error = 0;
    unsigned long long bytes_sent = 0;
    const char* local_rejection = nullptr;
};

// Ein Listener bedient begrenzt viele unabhängige, befristete Wiedergabe-Grants.
class PlaybackProxy {
public:
    explicit PlaybackProxy(PlaybackSource source, std::chrono::milliseconds lifetime = std::chrono::hours(2));
    ~PlaybackProxy();
    PlaybackProxy(const PlaybackProxy&) = delete;
    PlaybackProxy& operator=(const PlaybackProxy&) = delete;
    const std::string& url() const;
    std::string add(PlaybackSource source);
    void revoke(const std::string& url);
    ProxyStatus status() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
