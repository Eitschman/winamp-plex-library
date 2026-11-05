#pragma once
#include "plex/proxy.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace plex {
struct LocalMediaState;
struct LocalMediaLimits {
    unsigned long long file_bytes = 512ULL * 1024 * 1024;
    unsigned long long total_bytes = 2ULL * 1024 * 1024 * 1024;
};

// Vorbereitete Dateien bleiben erhalten, solange eigene Wiedergabezuordnungen sie halten.
class LocalMediaFile {
public:
    ~LocalMediaFile();
    const std::filesystem::path& path() const noexcept { return path_; }
    unsigned long long size() const noexcept { return size_; }
private:
    friend class LocalMediaStore;
    LocalMediaFile(std::shared_ptr<LocalMediaState> state, std::filesystem::path path,
                   unsigned long long size, HANDLE guard);
    std::shared_ptr<LocalMediaState> state_;
    std::filesystem::path path_;
    unsigned long long size_;
    HANDLE guard_;
};

using PreparedMedia = std::unordered_map<std::string, std::shared_ptr<const LocalMediaFile>>;
bool requires_local_media(const std::string& media_path);

// FLAC und OGG benötigen in der Zielversion lokalen Decoderzugriff. Die Auswahl
// erfolgt vor dem Download; Netzwerkfehler erzeugen keinen anderen Wiedergabepfad.
class LocalMediaStore {
public:
    explicit LocalMediaStore(std::filesystem::path root, LocalMediaLimits limits = {});
    std::shared_ptr<const LocalMediaFile> stage(const PlaybackSource& source, HANDLE stop);
    std::filesystem::path directory() const;
    unsigned long long bytes_used() const;
private:
    std::shared_ptr<LocalMediaState> state_;
};
}
