#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace plex {
struct ServerAddress {
    std::string host;
    std::uint16_t port;
    bool secure;
};

// Nur eine Server-Origin; Zugangsdaten und fremde API-Ziele werden abgewiesen.
ServerAddress parse_server_address(std::string_view address);
void validate_token(std::string_view token);
void validate_media_path(std::string_view path);
std::string media_extension(std::string_view path);
std::string validate_range(std::string_view range);
std::string encode_query_component(std::string_view value);

struct LocalRequest {
    bool head = false;
    std::string range;
};

// Der Proxy akzeptiert ausschließlich seine konkrete Host-/Capability-Kombination.
LocalRequest parse_local_request(std::string_view headers, std::string_view authority,
                                 std::string_view capability_path);
}
