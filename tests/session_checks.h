#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace plex::test {
// Erst eine zugeordnete aktive Sitzung erlaubt eine Aussage über Transcoding.
// PlexAPI ordnet TranscodeSession unmittelbar dem jeweiligen Medienobjekt zu.
inline void require_no_transcode_session(const nlohmann::json& session) {
    if (!session.is_object() || !session.contains("Player") || !session.at("Player").is_object() ||
        session.at("Player").value("machineIdentifier", std::string{}).empty())
        throw std::invalid_argument("active_player_session_required");
    if (session.contains("TranscodeSession"))
        throw std::invalid_argument("unexpected_transcode_session");
}
}
