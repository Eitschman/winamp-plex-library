#pragma once

#include "plex/security.h"
#include <memory>
#include <string>
#include <chrono>

namespace plex {
class LocalMediaStore;
struct PlaybackSource {
    ServerAddress server;
    std::string token;
    std::string media_path;
    ~PlaybackSource();
};

struct ProxyFailure {
    unsigned long long count = 0;
    unsigned long upstream_status = 0;
    unsigned long transport_error = 0;
    unsigned long long expected_bytes = 0;
    unsigned long long transferred_bytes = 0;
};

struct ProxyStatus {
    unsigned long upstream_status = 0;
    unsigned long transport_error = 0;
    unsigned long long bytes_sent = 0;
    const char* local_rejection = nullptr;
    ProxyFailure last_failure;
    unsigned long long local_disconnects = 0;
    unsigned long long bytes_received = 0;
    unsigned long long active_downloads = 0;
    unsigned long long reserved_media_bytes = 0;
};

enum class ProxyClientPolicy { capability_only, host_session };

// Ein Listener bedient begrenzt viele unabhängige, befristete Wiedergabe-Grants.
class PlaybackProxy {
public:
    explicit PlaybackProxy(PlaybackSource source, std::chrono::milliseconds lifetime = std::chrono::hours(2),
                           ProxyClientPolicy policy = ProxyClientPolicy::capability_only,
                           std::shared_ptr<LocalMediaStore> media_store = {});
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
