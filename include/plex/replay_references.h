#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace plex {
struct ReplayReference {
    std::string alias, server, track, part;
};
// Dauerhafte Zuordnungen sind keine HTTP-Berechtigung. Der aktuelle Zugang und
// die Serveridentität müssen vor jeder erneuten Medienauflösung geprüft werden.
class ReplayReferences {
public:
    explicit ReplayReferences(std::filesystem::path path);
    std::vector<std::optional<ReplayReference>> find(const std::vector<std::string>& aliases) const;
    void merge(const std::vector<ReplayReference>& references, HANDLE stop);
private:
    std::filesystem::path path_;
};
void validate_replay_reference(const ReplayReference& reference);
}
