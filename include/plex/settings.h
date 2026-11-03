#pragma once

#include <filesystem>
#include <string>

namespace plex {
struct PlexConnectionSettings {
    std::string client_identifier;
    std::string server_identifier;
    std::string server_origin;
    std::string server_name;
    std::string server_token;
    std::string library_identifier;
    std::string library_title;
    std::string language;
};
std::filesystem::path settings_path();
PlexConnectionSettings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path, const PlexConnectionSettings& settings);
std::string create_client_identifier();
}
