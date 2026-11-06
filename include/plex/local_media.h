#pragma once
#include "plex/proxy.h"
#include <windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace plex {
struct LocalMediaState;
struct LocalMediaLimits {
    unsigned long long file_bytes = 512ULL * 1024 * 1024;
    unsigned long long total_bytes = 2ULL * 1024 * 1024 * 1024;
};

// Privater, fortlaufend beschreibbarer Medienpuffer. Ein Leser darf nur bereits
// geschriebene Bytes abrufen; Reservierungen teilen das Budget vorbereiteter Dateien.
class LocalMediaBuffer {
public:
    ~LocalMediaBuffer();
    void append(const char* data, unsigned long size);
    unsigned long read(unsigned long long offset, char* data, unsigned long capacity);
private:
    friend class LocalMediaStore;
    LocalMediaBuffer(std::shared_ptr<LocalMediaState> state, std::filesystem::path path,
                     HANDLE file, unsigned long long reserved, unsigned long long limit);
    std::shared_ptr<LocalMediaState> state_;
    std::filesystem::path path_;
    HANDLE file_;
    std::mutex mutex_;
    unsigned long long written_ = 0, reserved_ = 0, limit_ = 0;
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

// FLAC, OGG und ADTS-AAC benötigen in der Zielversion lokalen Decoderzugriff. Die Auswahl
// erfolgt vor dem Download; Netzwerkfehler erzeugen keinen anderen Wiedergabepfad.
class LocalMediaStore {
public:
    explicit LocalMediaStore(std::filesystem::path root, LocalMediaLimits limits = {});
    std::shared_ptr<const LocalMediaFile> stage(const PlaybackSource& source, HANDLE stop);
    std::shared_ptr<LocalMediaBuffer> buffer(unsigned long long expected_bytes = 0);
    std::filesystem::path directory() const;
    unsigned long long bytes_used() const;
private:
    std::shared_ptr<const LocalMediaFile> download(const PlaybackSource& source, HANDLE stop);
    std::shared_ptr<LocalMediaState> state_;
};
}
