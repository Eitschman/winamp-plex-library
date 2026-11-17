#pragma once

#include "plex/security.h"
#include "plex/settings.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>

namespace plex {
struct PlexLibrary { std::string identifier; std::string title; };
struct PinRequest { std::string identifier; std::string code; std::string auth_url; int expires_in = 0; };
inline constexpr unsigned long server_identity_mismatch = 0x30000001;
inline constexpr unsigned long account_signin_required = 0x30000002;
struct PlexConnectionResult {
    std::string origin;
    std::vector<PlexLibrary> libraries;
    std::string server_token;
    std::vector<PlexConnection> connections;
};
using PlexServerRequest = std::function<nlohmann::json(const std::string&, const std::string&, const std::string&)>;
PlexConnectionResult verify_server_connection(const PlexServer& server, const std::string& preferred_origin, const PlexServerRequest& request);

// Der Account-Token wird ausschließlich an Plex selbst gesendet; PMS erhält den Ressourcen-Token.
class PlexAuth {
public:
    PlexAuth(std::string client_identifier, HANDLE stop, HANDLE request_stop = nullptr);
    PinRequest create_pin() const;
    std::string check_pin(const PinRequest& pin) const;
    std::vector<PlexServer> servers(const std::string& account_token) const;
    nlohmann::json get_server(const std::string& origin, const std::string& token, const std::string& path, bool recover = true) const;
    PlexConnectionResult connect_server(const PlexServer& server, const std::string& preferred_origin, bool recover = true) const;
private:
    nlohmann::json request(const std::string& origin, const std::string& token, const std::string& path, bool post, bool recover = true) const;
    std::string client_identifier_;
    HANDLE stop_;
    HANDLE request_stop_;
};
std::vector<PlexServer> parse_servers(const nlohmann::json& response);
std::vector<PlexLibrary> parse_libraries(const nlohmann::json& response);
PlexConnection select_server_connection(const PlexServer& server, const std::function<bool(const PlexConnection&)>& reachable);
}
