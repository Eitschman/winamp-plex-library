#include "plex/feedback.h"
#include "plex/http_stream.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace plex {
namespace {
void check_identifier(const std::string& identifier) {
    if (identifier.empty() || identifier.size() > 64 || identifier.find_first_not_of("0123456789") != identifier.npos)
        throw std::invalid_argument("feedback_identifier");
}
void send(const PlexConnectionSettings& settings, const std::string& path, HttpMethod method, HANDLE stop) {
    const auto server = parse_server_address(settings.server_origin);
    if (!server.secure) throw std::invalid_argument("https_required");
    HttpStream stream(server, settings.server_token, path, method, settings.client_identifier, stop);
    if (stream.status() != 200) throw HttpError(0x20000000UL + stream.status());
    std::array<char, 4096> buffer{};
    std::size_t received = 0;
    for (;;) {
        const auto size = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size) break;
        received += size;
        if (received > 65536) throw HttpError(ERROR_INVALID_DATA);
    }
}
}
std::string timeline_path(const TimelineReport& report) {
    check_identifier(report.identifier);
    if (report.duration_ms <= 0 || report.time_ms < 0 || report.time_ms > report.duration_ms)
        throw std::invalid_argument("feedback_time");
    const char* state = nullptr;
    switch (report.state) {
    case PlaybackState::playing: state = "playing"; break;
    case PlaybackState::paused: state = "paused"; break;
    case PlaybackState::stopped: state = "stopped"; break;
    case PlaybackState::buffering: state = "buffering"; break;
    default: throw std::invalid_argument("feedback_state");
    }
    if (report.continuing && report.state != PlaybackState::stopped) throw std::invalid_argument("feedback_continuing");
    return "/:/timeline?key=%2Flibrary%2Fmetadata%2F" + report.identifier + "&ratingKey=" + report.identifier +
        "&state=" + state + "&time=" + std::to_string(report.time_ms) + "&duration=" + std::to_string(report.duration_ms) +
        (report.state == PlaybackState::stopped ? report.continuing ? "&continuing=1" : "&continuing=0" : "");
}
std::string rating_path(const std::string& identifier, double rating) {
    check_identifier(identifier);
    if (!std::isfinite(rating) || rating < 0 || rating > 10) throw std::invalid_argument("feedback_rating");
    std::ostringstream value;
    value.imbue(std::locale::classic());
    value << std::setprecision(std::numeric_limits<double>::max_digits10) << rating;
    return "/:/rate?identifier=com.plexapp.plugins.library&key=" + identifier + "&rating=" + value.str();
}
void report_timeline(const PlexConnectionSettings& settings, const TimelineReport& report, HANDLE stop) {
    send(settings, timeline_path(report), HttpMethod::post, stop);
}
void rate_item(const PlexConnectionSettings& settings, const std::string& identifier, double rating, HANDLE stop) {
    send(settings, rating_path(identifier, rating), HttpMethod::put, stop);
}
}
