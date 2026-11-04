#include "plex/settings.h"
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
    if (!std::filesystem::exists(path)) return {create_client_identifier(), {}, {}, {}, {}, {}, {}, {}};
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
            document.at("server_name"), document.at("server_token"), document.at("library_identifier"), document.at("library_title"), document.at("language")};
        validate_token(result.client_identifier);
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
    if (!settings.server_origin.empty()) { parse_server_address(settings.server_origin); validate_token(settings.server_token); }
    nlohmann::json document{{"client_identifier", settings.client_identifier}, {"server_identifier", settings.server_identifier},
        {"server_origin", settings.server_origin}, {"server_name", settings.server_name}, {"server_token", settings.server_token},
        {"library_identifier", settings.library_identifier}, {"library_title", settings.library_title}, {"language", settings.language}};
    auto serialized = document.dump();
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
