#include "plex/auth.h"
#include "plex/http_stream.h"
#include <array>
#include <stdexcept>
#include <algorithm>

namespace plex {
namespace {
std::string scalar_identifier(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    throw std::invalid_argument("identifier_type");
}
}

PlexAuth::PlexAuth(std::string client_identifier, HANDLE stop) : client_identifier_(std::move(client_identifier)), stop_(stop) {
    validate_token(client_identifier_);
}

nlohmann::json PlexAuth::request(const std::string& origin, const std::string& token, const std::string& path, bool post) const {
    const auto server = parse_server_address(origin);
    HttpStream stream(server, token, path, post ? HttpMethod::post : HttpMethod::get, client_identifier_, stop_);
    const auto status = stream.status();
    if (status != 200 && status != 201) throw HttpError(0x20000000UL + status);
    std::array<char, 32768> buffer{};
    std::string body;
    try {
        for (;;) {
            const auto size = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!size) break;
            if (body.size() + size > 16 * 1024 * 1024) throw std::invalid_argument("response_limit");
            body.append(buffer.data(), size);
        }
        auto response = nlohmann::json::parse(body, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth > 64) throw std::invalid_argument("api_depth");
            return true;
        });
        // Einige Server liefern Pagination nur als Antwortheader.
        if (response.is_object() && response.contains("MediaContainer")) {
            auto& container = response.at("MediaContainer");
            for (const auto& header : {std::pair{L"X-Plex-Container-Start", "offset"}, std::pair{L"X-Plex-Container-Total-Size", "totalSize"}}) {
                const auto value = stream.header(header.first);
                if (!container.contains(header.second) && !value.empty()) {
                    if (value.size() > 10 || value.find_first_not_of("0123456789") != value.npos) throw std::invalid_argument("pagination_header");
                    container[header.second] = std::stoull(value);
                }
            }
        }
        SecureZeroMemory(body.data(), body.size());
        return response;
    } catch (...) {
        SecureZeroMemory(body.data(), body.size());
        // Parserfehler können den Antworttext enthalten. Niemals über die API-Grenze geben.
        throw std::runtime_error("api_response_error");
    }
}

PinRequest PlexAuth::create_pin() const {
    const auto response = request("https://plex.tv", {}, "/api/v2/pins?strong=true", true);
    PinRequest result;
    result.identifier = scalar_identifier(response.at("id"));
    result.code = response.at("code").get<std::string>();
    result.expires_in = response.at("expiresIn").get<int>();
    result.auth_url = "https://app.plex.tv/auth#?clientID=" + encode_query_component(client_identifier_) +
                      "&code=" + encode_query_component(result.code) +
                      "&context%5Bdevice%5D%5Bproduct%5D=Winamp%20Plex%20Library";
    if (result.identifier.empty() || result.code.empty() || result.expires_in <= 0 || result.expires_in > 3600)
        throw std::invalid_argument("pin_response");
    return result;
}

std::string PlexAuth::check_pin(const PinRequest& pin) const {
    const auto response = request("https://plex.tv", {}, "/api/v2/pins/" + encode_query_component(pin.identifier), false);
    if (!response.contains("authToken") || response.at("authToken").is_null()) return {};
    auto token = response.at("authToken").get<std::string>();
    validate_token(token);
    return token;
}

std::vector<PlexServer> PlexAuth::servers(const std::string& account_token) const {
    return parse_servers(request("https://clients.plex.tv", account_token, "/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=0", false));
}

nlohmann::json PlexAuth::get_server(const std::string& origin, const std::string& token, const std::string& path) const {
    return request(origin, token, path, false);
}

std::vector<PlexServer> parse_servers(const nlohmann::json& response) {
    if (!response.is_array() || response.size() > 64) throw std::invalid_argument("resources_shape");
    std::vector<PlexServer> result;
    for (const auto& resource : response) {
        const auto provides = resource.value("provides", std::string{});
        if (provides != "server" && provides.find("server,") != 0 && provides.find(",server") == provides.npos) continue;
        PlexServer server;
        server.identifier = resource.at("clientIdentifier").get<std::string>();
        server.name = resource.at("name").get<std::string>();
        server.access_token = resource.at("accessToken").get<std::string>();
        validate_token(server.access_token);
        validate_token(server.identifier);
        if (!resource.at("connections").is_array() || resource.at("connections").size() > 32)
            throw std::invalid_argument("connections_shape");
        for (const auto& connection : resource.at("connections")) {
            if (server.connections.size() >= 32) throw std::invalid_argument("connections_limit");
            const auto uri = connection.at("uri").get<std::string>();
            // Unsichere Verbindungen werden dem Benutzer nicht als sichere Wahl angeboten.
            if (uri.substr(0, 8) != "https://") continue;
            parse_server_address(uri);
            server.connections.push_back({uri, connection.value("local", false), connection.value("relay", false)});
        }
        result.push_back(std::move(server));
    }
    return result;
}

// Die Erreichbarkeit wird im Hintergrund geprüft. Lokal hat Vorrang vor Fernzugriff
// und Relay; eine fehlerhafte Authentifizierung löst keine weitere Auswahl aus.
PlexConnection select_server_connection(const PlexServer& server, const std::function<bool(const PlexConnection&)>& reachable) {
    if (server.connections.size() > 32) throw std::invalid_argument("connections_limit");
    auto connections = server.connections;
    const auto rank = [](const PlexConnection& connection) { return connection.relay ? 2 : connection.local ? 0 : 1; };
    std::stable_sort(connections.begin(), connections.end(), [&](const auto& left, const auto& right) { return rank(left) < rank(right); });
    for (const auto& connection : connections) {
        if (!parse_server_address(connection.uri).secure) throw std::invalid_argument("https_required");
        if (reachable(connection)) return connection;
    }
    throw HttpError(ERROR_NOT_CONNECTED);
}

std::vector<PlexLibrary> parse_libraries(const nlohmann::json& response) {
    const auto& container = response.at("MediaContainer");
    std::vector<PlexLibrary> result;
    if (!container.contains("Directory")) return result;
    for (const auto& library : container.at("Directory")) {
        if (library.at("type") != "artist") continue;
        result.push_back({scalar_identifier(library.at("key")), library.at("title").get<std::string>()});
    }
    return result;
}
}
