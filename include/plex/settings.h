#pragma once

#include <filesystem>
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
struct PlexConnectionSettings {
    std::string client_identifier;
    std::string server_identifier;
    std::string server_origin;
    std::string server_name;
    std::string server_token;
    std::string library_identifier;
    std::string library_title;
    bool cache_enabled = true;
    std::string preferred_origin;
    std::vector<PlexConnection> server_connections;
    std::vector<PlexServer> known_servers;
};
std::filesystem::path settings_path();
PlexConnectionSettings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const PlexConnectionSettings& settings);
std::string create_client_identifier();
}
