#pragma once

#include "plex/security.h"
#include <memory>
#include <string>

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
};

// Eine Instanz besitzt genau einen unveränderlichen, befristeten Wiedergabe-Grant.
class PlaybackProxy {
public:
    explicit PlaybackProxy(PlaybackSource source);
    ~PlaybackProxy();
    PlaybackProxy(const PlaybackProxy&) = delete;
    PlaybackProxy& operator=(const PlaybackProxy&) = delete;
    const std::string& url() const;
    ProxyStatus status() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
