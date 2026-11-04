#include "plex/security.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace plex {
namespace {
bool digits(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= '0' && character <= '9';
    });
}

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}
}

ServerAddress parse_server_address(std::string_view address) {
    ServerAddress result{};
    if (address.substr(0, 8) == "https://") {
        result.secure = true;
        result.port = 443;
        address.remove_prefix(8);
    } else if (address.substr(0, 7) == "http://") {
        result.port = 80;
        address.remove_prefix(7);
    } else throw std::invalid_argument("server_scheme");
    if (!address.empty() && address.back() == '/') address.remove_suffix(1);
    if (address.empty() || address.size() > 260 || address.find_first_of("/@?#%\\\r\n\t ") != address.npos)
        throw std::invalid_argument("server_origin");
    const auto colon = address.find(':');
    const auto host = address.substr(0, colon);
    if (host.empty() || host.front() == '.' || host.back() == '.' || host.find("..") != host.npos ||
        !std::all_of(host.begin(), host.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '-' || character == '.';
        })) throw std::invalid_argument("server_host");
    if (colon != address.npos) {
        const auto port = address.substr(colon + 1);
        if (!digits(port) || port.size() > 5) throw std::invalid_argument("server_port");
        const auto number = std::stoul(std::string(port));
        if (number == 0 || number > 65535) throw std::invalid_argument("server_port");
        result.port = static_cast<std::uint16_t>(number);
    }
    result.host = lower(host);
    // Öffentliche HTTP-URLs würden das Header-Token unverschlüsselt übertragen.
    if (!result.secure && result.host != "127.0.0.1" && result.host != "localhost")
        throw std::invalid_argument("server_requires_https");
    return result;
}

void validate_token(std::string_view token) {
    if (token.empty() || token.size() > 4096 || !std::all_of(token.begin(), token.end(), [](unsigned char character) {
        return character >= 33 && character <= 126;
    })) throw std::invalid_argument("token_invalid");
}

void validate_media_path(std::string_view path) {
    constexpr std::string_view prefix = "/library/parts/";
    if (path.substr(0, prefix.size()) != prefix || path.size() > 512)
        throw std::invalid_argument("media_path");
    const auto identifier_end = path.find('/', prefix.size());
    if (identifier_end == path.npos || !digits(path.substr(prefix.size(), identifier_end - prefix.size())))
        throw std::invalid_argument("media_id");
    auto filename = path.substr(identifier_end + 1);
    const auto version_end = filename.find('/');
    if (version_end != filename.npos) {
        if (!digits(filename.substr(0, version_end))) throw std::invalid_argument("media_version");
        filename.remove_prefix(version_end + 1);
    }
    if (filename.empty() || filename.find("..") != filename.npos ||
        !std::all_of(filename.begin(), filename.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
        })) throw std::invalid_argument("media_filename");
}

std::string media_extension(std::string_view path) {
    validate_media_path(path);
    const auto dot = path.rfind('.');
    if (dot == path.npos) throw std::invalid_argument("media_format");
    const auto extension = lower(path.substr(dot));
    if (extension != ".mp3" && extension != ".flac" && extension != ".aac" &&
        extension != ".m4a" && extension != ".ogg") throw std::invalid_argument("media_format");
    return extension;
}

std::string validate_range(std::string_view range) {
    if (range.substr(0, 6) != "bytes=" || range.size() > 64) throw std::invalid_argument("range_unit");
    const auto interval = range.substr(6);
    const auto dash = interval.find('-');
    if (dash == interval.npos || interval.find('-', dash + 1) != interval.npos)
        throw std::invalid_argument("range_interval");
    const auto first = interval.substr(0, dash);
    const auto last = interval.substr(dash + 1);
    if ((first.empty() && last.empty()) || (!first.empty() && !digits(first)) || (!last.empty() && !digits(last)) ||
        first.size() > 19 || last.size() > 19) throw std::invalid_argument("range_value");
    if (!first.empty() && !last.empty() && std::stoull(std::string(first)) > std::stoull(std::string(last)))
        throw std::invalid_argument("range_order");
    if (first.empty() && std::stoull(std::string(last)) == 0) throw std::invalid_argument("range_suffix");
    return std::string(range);
}

std::string encode_query_component(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.' || character == '~')
            result += static_cast<char>(character);
        else {
            result += '%'; result += hex[character >> 4]; result += hex[character & 15];
        }
    }
    return result;
}

LocalRequest parse_local_request(std::string_view headers, std::string_view authority, std::string_view capability_path) {
    if (headers.size() > 16384 || headers.size() < 4 || headers.substr(headers.size() - 4) != "\r\n\r\n")
        throw std::invalid_argument("request_headers");
    const auto first_end = headers.find("\r\n");
    const auto first_line = headers.substr(0, first_end);
    const auto space = first_line.find(' ');
    if (space == first_line.npos) throw std::invalid_argument("request_line");
    const auto method = first_line.substr(0, space);
    const auto target_end = first_line.find(' ', space + 1);
    const auto target = first_line.substr(space + 1, target_end - space - 1);
    const auto version = target_end == first_line.npos ? std::string_view{} : first_line.substr(target_end + 1);
    if ((method != "GET" && method != "HEAD") || target != capability_path ||
        (version != "HTTP/1.1" && version != "HTTP/1.0")) throw std::invalid_argument("request_target");
    LocalRequest result{method == "HEAD", {}};
    bool host_seen = false;
    bool range_seen = false;
    for (auto position = first_end + 2; position < headers.size() - 2;) {
        const auto end = headers.find("\r\n", position);
        if (end == headers.npos) throw std::invalid_argument("header_line");
        const auto line = headers.substr(position, end - position);
        const auto colon = line.find(':');
        if (colon == line.npos || colon == 0) throw std::invalid_argument("header_name");
        const auto name = lower(line.substr(0, colon));
        const auto value = trim(line.substr(colon + 1));
        if (value.find_first_of("\r\n") != value.npos) throw std::invalid_argument("header_value");
        if (name == "host") {
            // Winamps HTTP-Client lässt den Port weg. Die Verbindung ist bereits
            // an den konkreten Loopback-Port gebunden; fremde Hosts bleiben verboten.
            const bool winamp_host = value == "127.0.0.1" && authority.substr(0, 10) == "127.0.0.1:";
            if (host_seen || (value != authority && !winamp_host)) throw std::invalid_argument("request_host");
            host_seen = true;
        } else if (name == "range") {
            if (range_seen) throw std::invalid_argument("duplicate_range");
            result.range = validate_range(value); range_seen = true;
        } else if (name == "origin" || name == "transfer-encoding" || name == "content-length") {
            throw std::invalid_argument("request_body_or_origin");
        } else if (name == "sec-fetch-site" && value != "none" && value != "same-origin") {
            throw std::invalid_argument("request_cross_site");
        }
        position = end + 2;
    }
    if (!host_seen) throw std::invalid_argument("missing_host");
    return result;
}
}
