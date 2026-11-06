#include "plex/replay_references.h"
#include "plex/security.h"
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <stdexcept>

namespace plex {
namespace {
constexpr std::size_t maximum_bytes = 64 * 1024 * 1024, maximum_references = 100000;
using Records = std::map<std::string, ReplayReference>;
Records read(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    const auto size = std::filesystem::file_size(path);
    if (!size || size > maximum_bytes) throw std::runtime_error("replay_store_size");
    std::vector<BYTE> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) throw std::runtime_error("replay_store_read");
    DATA_BLOB encrypted{static_cast<DWORD>(bytes.size()), bytes.data()}, plain{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain)) throw std::runtime_error("replay_store_decrypt");
    try {
        const auto document = nlohmann::json::parse(plain.pbData, plain.pbData + plain.cbData);
        if (document.at("schema") != 1 || !document.at("references").is_array() || document.at("references").size() > maximum_references)
            throw std::runtime_error("replay_store_schema");
        Records records;
        for (const auto& value : document.at("references")) {
            ReplayReference reference{value.at("alias"), value.at("server"), value.at("track"), value.at("part")};
            validate_replay_reference(reference);
            if (!records.emplace(reference.alias, std::move(reference)).second) throw std::runtime_error("replay_store_duplicate");
        }
        SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
        return records;
    } catch (...) { SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData); throw; }
}
void write(const std::filesystem::path& path, const Records& records) {
    nlohmann::json document{{"schema", 1}, {"references", nlohmann::json::array()}};
    for (const auto& entry : records) {
        const auto& record = entry.second;
        document["references"].push_back({{"alias", record.alias}, {"server", record.server}, {"track", record.track}, {"part", record.part}});
    }
    auto serialized = document.dump();
    if (serialized.size() > maximum_bytes - 4096) throw std::runtime_error("replay_store_limit");
    DATA_BLOB plain{static_cast<DWORD>(serialized.size()), reinterpret_cast<BYTE*>(serialized.data())}, encrypted{};
    const auto protected_data = CryptProtectData(&plain, L"Winamp Plex playlist references", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    SecureZeroMemory(serialized.data(), serialized.size());
    if (!protected_data) throw std::runtime_error("replay_store_encrypt");
    try {
        auto pending = path; pending += L".pending";
        std::ofstream output(pending, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData) || !output.flush()) throw std::runtime_error("replay_store_write");
        output.close();
        if (!output || !MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("replay_store_replace");
        LocalFree(encrypted.pbData);
    } catch (...) { LocalFree(encrypted.pbData); throw; }
}
}
void validate_replay_reference(const ReplayReference& reference) {
    if (reference.alias.empty() || reference.alias.size() > 32768 || reference.alias.find_first_of("\r\n") != std::string::npos ||
        reference.alias.find('\0') != std::string::npos || reference.server.empty() || reference.server.size() > 200 ||
        reference.server.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ||
        reference.track.empty() || reference.track.size() > 32 || reference.track.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("replay_identity");
    validate_media_path(reference.part);
    // Aliase dürfen niemals eine entfernte authentifizierte Adresse einschleusen.
    if (reference.alias.rfind("http://127.0.0.1:", 0) == 0) {
        const auto slash = reference.alias.find('/', 17);
        if (slash == std::string::npos || reference.alias.substr(slash, 8) != "/stream/" ||
            reference.alias.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.:/", 0) != std::string::npos)
            throw std::invalid_argument("replay_alias");
        parse_server_address(reference.alias.substr(0, slash));
        const auto path = reference.alias.substr(slash);
        if (path.size() != 66 || path.substr(8, 48).find_first_not_of("0123456789abcdef") != std::string::npos ||
            path.substr(56, 6) != "/audio" || (path.substr(62) != ".mp3" && path.substr(62) != ".m4a"))
            throw std::invalid_argument("replay_alias");
    } else {
        const std::filesystem::path file = std::filesystem::u8path(reference.alias);
        if (!file.is_absolute() || reference.alias.find("://") != std::string::npos ||
            (file.extension() != L".flac" && file.extension() != L".ogg" && file.extension() != L".m4a")) throw std::invalid_argument("replay_alias");
        const auto stem = file.stem().u8string();
        if (stem.size() != 48 || stem.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::invalid_argument("replay_alias");
    }
}
ReplayReferences::ReplayReferences(std::filesystem::path path) : path_(std::move(path)) {}
std::vector<std::optional<ReplayReference>> ReplayReferences::find(const std::vector<std::string>& aliases) const {
    const auto records = read(path_);
    std::vector<std::optional<ReplayReference>> result;
    for (const auto& alias : aliases) {
        const auto found = records.find(alias);
        result.push_back(found == records.end() ? std::nullopt : std::optional<ReplayReference>(found->second));
    }
    return result;
}
void ReplayReferences::merge(const std::vector<ReplayReference>& references, HANDLE stop) {
    if (references.empty()) return;
    for (const auto& reference : references) validate_replay_reference(reference);
    std::filesystem::create_directories(path_.parent_path());
    auto lock_path = path_; lock_path += L".lock";
    HANDLE lock = INVALID_HANDLE_VALUE;
    const auto deadline = GetTickCount64() + 5000;
    // Mehrere Winamp-Prozesse dürfen einander keine gespeicherten Zuordnungen verlieren.
    while (lock == INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) throw std::runtime_error("replay_cancelled");
        lock = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE && (GetLastError() != ERROR_SHARING_VIOLATION || GetTickCount64() >= deadline)) throw std::runtime_error("replay_store_lock");
        if (lock == INVALID_HANDLE_VALUE && WaitForSingleObject(stop, 25) == WAIT_OBJECT_0) throw std::runtime_error("replay_cancelled");
    }
    try {
        auto records = read(path_);
        for (const auto& reference : references) {
            const auto found = records.find(reference.alias);
            if (found != records.end() && (found->second.server != reference.server || found->second.track != reference.track || found->second.part != reference.part))
                throw std::runtime_error("replay_alias_collision");
            records[reference.alias] = reference;
        }
        if (records.size() > maximum_references) throw std::runtime_error("replay_store_limit");
        write(path_, records); CloseHandle(lock);
    } catch (...) { CloseHandle(lock); throw; }
}
}
