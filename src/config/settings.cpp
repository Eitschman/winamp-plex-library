#include "plex/settings.h"
#include "plex/auth.h"
#include "plex/security.h"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <array>

namespace plex {
std::string create_client_identifier() {
    GUID identifier{};
    if (FAILED(CoCreateGuid(&identifier))) throw std::runtime_error("client_identifier");
    wchar_t buffer[40]{};
    if (!StringFromGUID2(identifier, buffer, 40)) throw std::runtime_error("client_identifier");
    std::string result;
    for (std::size_t index = 1; index < 37; ++index) result += static_cast<char>(buffer[index]);
    return result;
}

std::filesystem::path settings_path() {
    // Prozesslokale Umleitung erlaubt isolierte Testhosts ohne Zugriff auf echte Zugänge.
    std::array<wchar_t, 32768> override_path{};
    const auto count = GetEnvironmentVariableW(L"WINAMP_PLEX_CONFIG_DIRECTORY", override_path.data(), static_cast<DWORD>(override_path.size()));
    if (count) {
        if (count >= override_path.size()) throw std::runtime_error("settings_override");
        const std::filesystem::path directory(override_path.data());
        if (!directory.is_absolute()) throw std::runtime_error("settings_override");
        return directory / L"connection.dpapi";
    }
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)))
        throw std::runtime_error("settings_directory");
    const std::filesystem::path directory(local);
    CoTaskMemFree(local);
    return directory / L"WinampPlexLibrary" / L"connection.dpapi";
}

PlexConnectionSettings load_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {create_client_identifier(), {}, {}, {}, {}, {}, {}};
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > 1024 * 1024) throw std::runtime_error("settings_size");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("settings_read");
    std::vector<unsigned char> encrypted(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(encrypted.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("settings_read");
    DATA_BLOB source{static_cast<DWORD>(encrypted.size()), encrypted.data()}, plain{};
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain))
        throw std::runtime_error("settings_decrypt");
    try {
        const auto document = nlohmann::json::parse(plain.pbData, plain.pbData + plain.cbData);
        PlexConnectionSettings result{document.at("client_identifier"), document.at("server_identifier"), document.at("server_origin"),
            document.at("server_name"), document.at("server_token"), document.at("library_identifier"), document.at("library_title")};
        validate_token(result.client_identifier);
        result.cache_enabled = document.value("cache_enabled", true);
        result.preferred_origin = document.value("preferred_origin", std::string{});
        if (!result.preferred_origin.empty()) parse_server_address(result.preferred_origin);
        if (document.contains("server_connections")) {
            const auto& connections = document.at("server_connections");
            if (!connections.is_array() || connections.size() > 32) throw std::runtime_error("connections_limit");
            for (const auto& connection : connections) {
                const auto uri = connection.at("uri").get<std::string>();
                if (!parse_server_address(uri).secure) throw std::runtime_error("https_required");
                result.server_connections.push_back({uri, connection.at("local"), connection.at("relay")});
            }
        }
        if (document.contains("known_servers")) {
            result.known_servers = parse_servers(document.at("known_servers"));
            for (const auto& server : document.at("known_servers")) for (const auto& connection : server.at("connections"))
                if (!parse_server_address(connection.at("uri").get<std::string>()).secure) throw std::runtime_error("https_required");
        }
        if (!result.server_origin.empty()) { parse_server_address(result.server_origin); validate_token(result.server_token); }
        SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
        return result;
    } catch (...) {
        SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
        throw std::runtime_error("settings_invalid");
    }
}

void save_settings(const std::filesystem::path& path, const PlexConnectionSettings& settings) {
    validate_token(settings.client_identifier);
    if (!settings.preferred_origin.empty()) parse_server_address(settings.preferred_origin);
    if (!settings.server_origin.empty()) { parse_server_address(settings.server_origin); validate_token(settings.server_token); }
    nlohmann::json document{{"client_identifier", settings.client_identifier}, {"server_identifier", settings.server_identifier},
        {"server_origin", settings.server_origin}, {"server_name", settings.server_name}, {"server_token", settings.server_token},
        {"library_identifier", settings.library_identifier}, {"library_title", settings.library_title},
        {"cache_enabled", settings.cache_enabled}, {"preferred_origin", settings.preferred_origin}};
    if (settings.server_connections.size() > 32) throw std::invalid_argument("connections_limit");
    document["server_connections"] = nlohmann::json::array();
    for (const auto& connection : settings.server_connections) {
        if (!parse_server_address(connection.uri).secure) throw std::invalid_argument("https_required");
        document["server_connections"].push_back({{"uri", connection.uri}, {"local", connection.local}, {"relay", connection.relay}});
    }
    if (settings.known_servers.size() > 64) throw std::invalid_argument("servers_limit");
    document["known_servers"] = nlohmann::json::array();
    for (const auto& server : settings.known_servers) {
        validate_token(server.access_token);
        if (server.connections.size() > 32) throw std::invalid_argument("connections_limit");
        nlohmann::json connections = nlohmann::json::array();
        for (const auto& connection : server.connections) {
            if (!parse_server_address(connection.uri).secure) throw std::invalid_argument("https_required");
            connections.push_back({{"uri", connection.uri}, {"local", connection.local}, {"relay", connection.relay}});
        }
        document["known_servers"].push_back({{"provides", "server"}, {"clientIdentifier", server.identifier}, {"name", server.name},
                                             {"accessToken", server.access_token}, {"connections", std::move(connections)}});
    }
    auto serialized = document.dump();
    if (serialized.size() > 1024 * 1024 - 1024) {
        SecureZeroMemory(serialized.data(), serialized.size());
        throw std::runtime_error("settings_size");
    }
    DATA_BLOB plain{static_cast<DWORD>(serialized.size()), reinterpret_cast<BYTE*>(serialized.data())}, encrypted{};
    const auto success = CryptProtectData(&plain, L"Winamp Plex Library", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    SecureZeroMemory(serialized.data(), serialized.size());
    if (!success) throw std::runtime_error("settings_encrypt");
    try {
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path; temporary += L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData) || !output.flush())
            throw std::runtime_error("settings_write");
        output.close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("settings_replace");
        LocalFree(encrypted.pbData);
    } catch (...) { LocalFree(encrypted.pbData); throw; }
}
}
