#include "plex/auth.h"
#include "plex/settings.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Action> void rejects(Action action, const char* name) {
    bool rejected = false;
    try { action(); } catch (...) { rejected = true; }
    check(rejected, name);
}
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() / (L"winamp-plex-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = directory / L"connection.dpapi";
    try {
        check(!std::filesystem::exists(directory), "isolated_test_directory");
        check(SetEnvironmentVariableW(L"WINAMP_PLEX_CONFIG_DIRECTORY", directory.c_str()) != FALSE, "set_isolated_configuration");
        check(plex::settings_path() == path, "explicit_configuration_directory");
        SetEnvironmentVariableW(L"WINAMP_PLEX_CONFIG_DIRECTORY", L"relative-directory");
        rejects([] { plex::settings_path(); }, "relative_configuration_directory_rejected");
        SetEnvironmentVariableW(L"WINAMP_PLEX_CONFIG_DIRECTORY", nullptr);
        const auto empty = plex::load_settings(path);
        check(empty.client_identifier.size() == 36 && empty.server_token.empty(), "new_configuration");
        plex::PlexConnectionSettings settings{empty.client_identifier, "synthetic-server-id", "https://music.example:32400", "Synthetic server",
            "synthetic-private-token", "17", "Björk & Klassik", "de"};
        plex::save_settings(path, settings);
        auto actual = plex::load_settings(path);
        check(actual.server_token == settings.server_token && actual.library_title == settings.library_title &&
              actual.client_identifier == settings.client_identifier, "dpapi_roundtrip_unicode");
        std::ifstream stored(path, std::ios::binary);
        std::string encrypted{std::istreambuf_iterator<char>(stored), {}};
        check(encrypted.find(settings.server_token) == encrypted.npos && encrypted.find(settings.server_origin) == encrypted.npos, "no_plaintext_credentials");
        stored.close();
        settings.library_title = "Replacement library";
        plex::save_settings(path, settings);
        check(plex::load_settings(path).library_title == settings.library_title, "atomic_config_replacement");
        { std::ofstream corrupted(path, std::ios::binary | std::ios::trunc); corrupted << "corrupt"; }
        rejects([&] { plex::load_settings(path); }, "corrupt_config_is_error");
        settings.server_origin = "https://music.example:32400/?X-Plex-Token=fake";
        rejects([&] { plex::save_settings(path, settings); }, "unsafe_config_origin_rejected");
        const auto resources = nlohmann::json::parse(R"([
          {"provides":"player","name":"Ignore player"},
          {"provides":"server","clientIdentifier":"server-1","name":"Musikserver","accessToken":"synthetic-server-token",
           "connections":[{"uri":"http://music.example:32400","local":true,"relay":false},
                          {"uri":"https://music.example:32400","local":false,"relay":false}]}
        ])");
        const auto servers = plex::parse_servers(resources);
        check(servers.size() == 1 && servers[0].identifier == "server-1", "servers_only");
        check(servers[0].connections.size() == 1 && servers[0].connections[0].uri == "https://music.example:32400", "secure_connections_only");
        check(servers[0].access_token == "synthetic-server-token", "resource_specific_token");
        auto hostile = resources;
        hostile[1]["connections"][1]["uri"] = "https://user:pass@other.example:32400";
        rejects([&] { plex::parse_servers(hostile); }, "embedded_credentials_rejected");
        rejects([&] { plex::parse_servers(nlohmann::json::object()); }, "malformed_resources");
        const auto libraries = plex::parse_libraries(nlohmann::json::parse(R"({"MediaContainer":{"Directory":[
          {"key":"1","type":"movie","title":"Movies"},
          {"key":"7","type":"artist","title":"Música & 日本語"}
        ]}})"));
        check(libraries.size() == 1 && libraries[0].identifier == "7" && libraries[0].title == "Música & 日本語", "music_library_unicode");
        check(plex::parse_libraries(nlohmann::json::parse(R"({"MediaContainer":{"size":0}})")).empty(), "empty_libraries");
        rejects([&] { plex::parse_libraries(nlohmann::json::parse(R"({"MediaContainer":{"Directory":"invalid"}})")); }, "malformed_libraries");
        // Nur die ausdrücklich für diesen Test erzeugten Dateien zurückbauen.
        check(std::filesystem::remove(path), "test_file_cleanup");
        check(std::filesystem::remove(directory), "test_directory_cleanup");
        std::cout << checks << " config/auth checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
