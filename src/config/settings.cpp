#include "plex/settings.h"
#include "plex/auth.h"
#include "plex/security.h"
#include "plex/catalog.h"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>

namespace plex {
namespace {
using Json = nlohmann::json;
Json query_json(const BrowseQuery& query) {
    catalog_path(query);
    return {{"kind", static_cast<int>(query.kind)}, {"library", query.library_identifier}, {"parent", query.parent_identifier},
        {"search", query.search}, {"sort", query.sort}, {"playlist", query.playlist_items}, {"descending", query.descending},
        {"offset", query.offset}, {"limit", query.limit}, {"genre", query.genre}, {"year", query.year}, {"codec", query.audio_codec}};
}
BrowseQuery read_query(const Json& value) {
    const auto kind = value.at("kind").get<int>();
    if (kind < 0 || kind > 3) throw std::invalid_argument("view_kind");
    BrowseQuery query; query.kind = static_cast<BrowseKind>(kind);
    query.library_identifier = value.at("library"); query.parent_identifier = value.at("parent");
    query.search = value.at("search"); query.sort = value.at("sort");
    query.playlist_items = value.at("playlist"); query.descending = value.at("descending");
    query.offset = value.at("offset"); query.limit = value.at("limit");
    query.genre = value.at("genre"); query.year = value.at("year"); query.audio_codec = value.at("codec");
    catalog_path(query); return query;
}
void validate_navigation(const LibraryNavigation& navigation) {
    if (navigation.caption.size() > 16384 || navigation.offsets.empty() || navigation.offsets.size() > 40001 || navigation.selected.size() > 500 ||
        navigation.offsets.front() != 0 || navigation.offsets.back() != navigation.query.offset ||
        !std::is_sorted(navigation.offsets.begin(), navigation.offsets.end())) throw std::invalid_argument("view_navigation");
    for (const auto offset : navigation.offsets) if (offset > 10000000) throw std::invalid_argument("view_offset");
    const auto valid_id = [](const std::string& id) {
        const auto separator = id.find(':');
        const auto key = id.substr(0, separator);
        if (key.size() > 32 || key.find_first_not_of("0123456789") != key.npos) return false;
        if (separator == id.npos) return true;
        const auto occurrence = id.substr(separator + 1);
        return !key.empty() && !occurrence.empty() && occurrence.size() <= 3 && occurrence.find_first_not_of("0123456789") == occurrence.npos;
    };
    for (const auto& id : navigation.selected) if (!valid_id(id)) throw std::invalid_argument("view_selection");
    if (!valid_id(navigation.top) || !valid_id(navigation.focused)) throw std::invalid_argument("view_position");
}
Json navigation_json(const LibraryNavigation& navigation) {
    validate_navigation(navigation);
    return {{"query", query_json(navigation.query)}, {"caption", navigation.caption}, {"offsets", navigation.offsets},
        {"selected", navigation.selected}, {"focused", navigation.focused}, {"top", navigation.top}};
}
LibraryNavigation read_navigation(const Json& value) {
    LibraryNavigation result; result.query = read_query(value.at("query")); result.caption = value.at("caption");
    result.offsets = value.at("offsets").get<std::vector<std::size_t>>(); result.selected = value.at("selected").get<std::vector<std::string>>();
    result.focused = value.at("focused"); result.top = value.at("top"); validate_navigation(result); return result;
}
Json preferences_json(const LibraryPreferences& preferences) {
    if (preferences.bookmarks.size() > 32 || preferences.searches.size() > 64) throw std::invalid_argument("views_limit");
    Json bookmarks = Json::array(), searches = Json::array();
    for (const auto& bookmark : preferences.bookmarks) {
        if (bookmark.history.size() > 16 || bookmark.server.size() > 256 || bookmark.library.size() > 32) throw std::invalid_argument("view_scope");
        Json history = Json::array();
        for (const auto& navigation : bookmark.history) history.push_back(navigation_json(navigation));
        bookmarks.push_back({{"server", bookmark.server}, {"library", bookmark.library}, {"root", static_cast<int>(bookmark.root)},
            {"current", navigation_json(bookmark.current)}, {"history", history}});
    }
    for (const auto& search : preferences.searches) {
        if (search.name.empty() || search.name.size() > 256 || search.server.size() > 256 || search.library.size() > 32) throw std::invalid_argument("view_name");
        searches.push_back({{"server", search.server}, {"library", search.library}, {"name", search.name}, {"query", query_json(search.query)}});
    }
    return {{"bookmarks", bookmarks}, {"searches", searches}};
}
LibraryPreferences read_preferences(const Json& value) {
    LibraryPreferences result;
    if (!value.at("bookmarks").is_array() || value.at("bookmarks").size() > 32 || !value.at("searches").is_array() || value.at("searches").size() > 64) throw std::invalid_argument("views_limit");
    for (const auto& item : value.at("bookmarks")) {
        LibraryBookmark bookmark; bookmark.server = item.at("server"); bookmark.library = item.at("library");
        const auto root = item.at("root").get<int>(); if (root < 0 || root > 3) throw std::invalid_argument("view_kind");
        bookmark.root = static_cast<BrowseKind>(root); bookmark.current = read_navigation(item.at("current"));
        if (!item.at("history").is_array() || item.at("history").size() > 16) throw std::invalid_argument("view_history");
        for (const auto& navigation : item.at("history")) bookmark.history.push_back(read_navigation(navigation));
        result.bookmarks.push_back(std::move(bookmark));
    }
    for (const auto& item : value.at("searches")) result.searches.push_back({item.at("server"), item.at("library"), item.at("name"), read_query(item.at("query"))});
    preferences_json(result); return result;
}
}
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
        if (document.contains("library_preferences")) result.library_preferences = read_preferences(document.at("library_preferences"));
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
        {"cache_enabled", settings.cache_enabled}, {"preferred_origin", settings.preferred_origin},
        {"library_preferences", preferences_json(settings.library_preferences)}};
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
