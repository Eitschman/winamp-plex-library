#include "plex/auth.h"
#include "plex/http_stream.h"
#include "plex/settings.h"
#include <windows.h>
#include <wincrypt.h>
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
// Ausschließlich synthetische Konfigurationsdaten für Schema- und Migrationstests.
void protected_fixture(const std::filesystem::path& path, const nlohmann::json& document) {
    auto serialized = document.dump();
    DATA_BLOB plain{static_cast<DWORD>(serialized.size()), reinterpret_cast<BYTE*>(serialized.data())}, encrypted{};
    const auto success = CryptProtectData(&plain, L"Plex test fixture", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    SecureZeroMemory(serialized.data(), serialized.size());
    check(success != FALSE, "protect_synthetic_fixture");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData);
    LocalFree(encrypted.pbData);
    check(static_cast<bool>(output), "write_synthetic_fixture");
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
            "synthetic-private-token", "17", "Björk & Klassik"};
        plex::save_settings(path, settings);
        auto actual = plex::load_settings(path);
        check(actual.server_token == settings.server_token && actual.library_title == settings.library_title &&
              actual.client_identifier == settings.client_identifier, "dpapi_roundtrip_unicode");
        check(actual.cache_enabled, "cache_default_enabled");
        settings.cache_enabled = false;
        settings.preferred_origin = "https://preferred.example:32400";
        settings.server_connections = {{"https://local.example:32400", true, false}, {"https://music.example:32400", false, false}};
        settings.known_servers = {{settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections},
                                 {"synthetic-other-id", "Second server", "synthetic-other-token", {{"https://second.example", false, false}}}};
        plex::save_settings(path, settings);
        check(!plex::load_settings(path).cache_enabled, "cache_preference_roundtrip");
        check(plex::load_settings(path).preferred_origin == settings.preferred_origin, "preferred_origin_roundtrip");
        const auto routes = plex::load_settings(path).server_connections;
        check(routes.size() == 2 && routes[0].local && !routes[1].local && routes[0].uri == settings.server_connections[0].uri, "connection_routes_roundtrip");
        const auto profiles = plex::load_settings(path).known_servers;
        check(profiles.size() == 2 && profiles[1].access_token == "synthetic-other-token" && profiles[0].connections[0].local,
              "server_profiles_roundtrip");
        std::ifstream stored(path, std::ios::binary);
        std::string encrypted{std::istreambuf_iterator<char>(stored), {}};
        check(encrypted.find(settings.server_token) == encrypted.npos && encrypted.find(settings.server_origin) == encrypted.npos, "no_plaintext_credentials");
        check(encrypted.find("synthetic-other-token") == encrypted.npos && encrypted.find(settings.preferred_origin) == encrypted.npos, "no_plaintext_profile_credentials");
        stored.close();
        settings.library_title = "Replacement library";
        plex::save_settings(path, settings);
        check(plex::load_settings(path).library_title == settings.library_title, "atomic_config_replacement");
        auto oversized = settings; oversized.library_title.assign(1024 * 1024, 'x');
        rejects([&] { plex::save_settings(path, oversized); }, "oversized_config_rejected_before_replacement");
        check(plex::load_settings(path).library_title == settings.library_title, "failed_save_preserves_existing_config");
        nlohmann::json legacy{{"client_identifier", settings.client_identifier}, {"server_identifier", settings.server_identifier},
            {"server_origin", settings.server_origin}, {"server_name", settings.server_name}, {"server_token", settings.server_token},
            {"library_identifier", settings.library_identifier}, {"library_title", settings.library_title}, {"language", "de"}};
        protected_fixture(path, legacy);
        const auto migrated = plex::load_settings(path);
        check(migrated.cache_enabled && migrated.preferred_origin.empty() && migrated.known_servers.empty(), "optional_preferences_and_legacy_language_ignored");
        legacy["cache_enabled"] = "disabled";
        protected_fixture(path, legacy);
        rejects([&] { plex::load_settings(path); }, "invalid_cache_preference_is_error");
        legacy.erase("cache_enabled");
        legacy["server_connections"] = nlohmann::json::array({{{"uri", "https://test.example/?X-Plex-Token=synthetic"}, {"local", false}, {"relay", false}}});
        protected_fixture(path, legacy);
        rejects([&] { plex::load_settings(path); }, "unsafe_saved_connection_is_error");
        legacy["server_connections"] = nlohmann::json::array(); legacy["known_servers"] = "invalid";
        protected_fixture(path, legacy);
        rejects([&] { plex::load_settings(path); }, "invalid_saved_profiles_is_error");
        { std::ofstream corrupted(path, std::ios::binary | std::ios::trunc); corrupted << "corrupt"; }
        rejects([&] { plex::load_settings(path); }, "corrupt_config_is_error");
        settings.preferred_origin = "https://preferred.example:32400/?X-Plex-Token=fake";
        rejects([&] { plex::save_settings(path, settings); }, "unsafe_preferred_origin_rejected");
        settings.preferred_origin.clear();
        settings.server_connections[0].uri = "http://local.example:32400";
        rejects([&] { plex::save_settings(path, settings); }, "insecure_saved_route_rejected");
        settings.server_connections.clear();
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
        plex::PlexServer routed{"synthetic-id", "Server", "synthetic-token", {
            {"https://relay.example", false, true}, {"https://remote.example", false, false}, {"https://local.example", true, false}}};
        std::vector<std::string> probes;
        auto selected = plex::select_server_connection(routed, [&](const auto& candidate) { probes.push_back(candidate.uri); return true; });
        check(selected.local && probes.size() == 1, "home_prefers_reachable_local_connection");
        probes.clear();
        selected = plex::select_server_connection(routed, [&](const auto& candidate) { probes.push_back(candidate.uri); return !candidate.local; });
        check(!selected.local && !selected.relay && probes.size() == 2, "office_prefers_remote_when_local_unreachable");
        selected = plex::select_server_connection(routed, [](const auto& candidate) { return candidate.relay; });
        check(selected.relay, "relay_requires_confirmed_reachability");
        rejects([&] { plex::select_server_connection(routed, [](const auto&) { return false; }); }, "no_reachable_connection_is_error");
        rejects([&] { plex::select_server_connection(routed, [](const auto&) -> bool { throw std::runtime_error("malformed_identity"); }); }, "invalid_identity_is_not_hidden");
        routed.connections[2].uri = "http://local.example";
        rejects([&] { plex::select_server_connection(routed, [](const auto&) { return true; }); }, "discovery_rejects_insecure_route");
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
        {
            const plex::PlexServer server{"synthetic-id", "Server", "synthetic-token", {
                {"https://remote.example", false, false}, {"https://local.example", true, false}}};
            const auto identity = nlohmann::json::parse(R"({"MediaContainer":{"machineIdentifier":"synthetic-id"}})");
            const auto sections = nlohmann::json::parse(R"({"MediaContainer":{"Directory":[{"key":"7","type":"artist","title":"Music"}]}})");
            unsigned calls = 0;
            auto local = plex::verify_server_connection(server, {}, [&](const auto& origin, const auto& token, const auto& path) {
                ++calls;
                check(origin == "https://local.example", "startup_prefers_local_route");
                if (path == "/identity") { check(token.empty(), "identity_never_receives_token"); return identity; }
                check(path == "/library/sections" && token == "synthetic-token" && calls == 2, "authentication_only_after_identity");
                return sections;
            });
            check(local.origin == "https://local.example" && local.libraries.size() == 1 && calls == 2, "verified_connection_and_libraries");
            calls = 0;
            auto fixed = plex::verify_server_connection(server, "https://remote.example", [&](const auto& origin, const auto& token, const auto& path) {
                ++calls;
                check(origin == "https://remote.example", "fixed_route_does_not_probe_local");
                check((path == "/identity") == token.empty(), "fixed_route_identity_before_authentication");
                return path == "/identity" ? identity : sections;
            });
            check(fixed.origin == "https://remote.example" && calls == 2, "fixed_route_remains_fixed_at_startup");
            calls = 0;
            auto matched = plex::verify_server_connection(server, {}, [&](const auto& origin, const auto& token, const auto& path) {
                ++calls;
                if (origin == "https://local.example") {
                    check(token.empty() && path == "/identity", "foreign_local_server_never_receives_credentials");
                    return nlohmann::json::parse(R"({"MediaContainer":{"machineIdentifier":"foreign-id"}})");
                }
                return path == "/identity" ? identity : sections;
            });
            check(matched.origin == "https://remote.example" && calls == 3, "automatic_route_requires_matching_server_identity");
            calls = 0;
            auto remote = plex::verify_server_connection(server, {}, [&](const auto& origin, const auto& token, const auto& path) {
                ++calls;
                if (origin == "https://local.example") { check(token.empty(), "unreachable_route_has_no_token"); throw plex::HttpError(ERROR_TIMEOUT); }
                return path == "/identity" ? identity : sections;
            });
            check(remote.origin == "https://remote.example" && calls == 3, "startup_remote_when_local_unreachable");
            calls = 0;
            bool refused = false;
            try { plex::verify_server_connection(server, {}, [&](const auto&, const auto&, const auto& path) {
                ++calls;
                if (path == "/identity") return identity;
                throw plex::HttpError(0x20000000UL + 401);
            }); } catch (const plex::HttpError& error) { refused = error.code == 0x20000000UL + 401; }
            check(refused && calls == 2, "auth_failure_never_tries_another_route");
            calls = 0;
            refused = false;
            try { plex::verify_server_connection(server, "https://remote.example", [&](const auto& origin, const auto& token, const auto&) {
                ++calls;
                check(origin == "https://remote.example" && token.empty(), "fixed_route_checked_without_token");
                return nlohmann::json::parse(R"({"MediaContainer":{"machineIdentifier":"foreign-id"}})");
            }); } catch (const plex::HttpError& error) { refused = error.code == plex::server_identity_mismatch; }
            check(refused && calls == 1, "fixed_route_mismatch_blocks_authentication");
            calls = 0;
            refused = false;
            try { plex::verify_server_connection(server, {}, [&](const auto&, const auto&, const auto&) -> nlohmann::json {
                ++calls; throw plex::HttpError(ERROR_CANCELLED);
            }); } catch (const plex::HttpError& error) { refused = error.code == ERROR_CANCELLED; }
            check(refused && calls == 1, "connection_cancellation_stops_discovery");
        }
        rejects([&] { plex::parse_libraries(nlohmann::json::parse(R"({"MediaContainer":{"Directory":"invalid"}})")); }, "malformed_libraries");
        // Nur die ausdrücklich für diesen Test erzeugten Dateien zurückbauen.
        check(std::filesystem::remove(path), "test_file_cleanup");
        check(std::filesystem::remove(directory), "test_directory_cleanup");
        std::cout << checks << " config/auth checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
