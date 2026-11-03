#pragma once

#include "plex/security.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace plex {
struct PlexConnection {
    std::string uri;
    bool local = false;
    bool relay = false;
};
struct PlexServer {
    std::string identifier;
    std::string name;
    std::string access_token;
    std::vector<PlexConnection> connections;
};
struct PlexLibrary { std::string identifier; std::string title; };
struct PinRequest { std::string identifier; std::string code; std::string auth_url; int expires_in = 0; };

// Der Account-Token wird ausschließlich an Plex selbst gesendet; PMS erhält den Ressourcen-Token.
class PlexAuth {
public:
    PlexAuth(std::string client_identifier, HANDLE stop);
    PinRequest create_pin() const;
    std::string check_pin(const PinRequest& pin) const;
    std::vector<PlexServer> servers(const std::string& account_token) const;
    nlohmann::json get_server(const std::string& origin, const std::string& token, const std::string& path) const;
private:
    nlohmann::json request(const std::string& origin, const std::string& token, const std::string& path, bool post) const;
    std::string client_identifier_;
    HANDLE stop_;
};
std::vector<PlexServer> parse_servers(const nlohmann::json& response);
std::vector<PlexLibrary> parse_libraries(const nlohmann::json& response);
}
