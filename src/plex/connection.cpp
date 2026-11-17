#include "plex/connection.h"
#include "plex/http_stream.h"
#include <winhttp.h>
#include <algorithm>
#include <chrono>

namespace plex {
namespace {
std::mutex registry_mutex;
std::shared_ptr<Verbindungsverwaltung> registry;
bool gleich(const ServerAddress& first, const ServerAddress& second) {
    return first.host == second.host && first.port == second.port && first.secure == second.secure;
}
void abbruch(HANDLE stop, HANDLE request_stop) {
    if (!stop || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 ||
        (request_stop && WaitForSingleObject(request_stop, 0) == WAIT_OBJECT_0)) throw HttpError(ERROR_CANCELLED);
}
}

Verbindungsverwaltung::Verbindungsverwaltung(PlexConnectionSettings settings, Ressourcen resources, Anfrage request)
    : settings_(std::move(settings)), resources_(std::move(resources)), request_(std::move(request)) {
    current_ = {parse_server_address(settings_.server_origin), settings_.server_token, 0};
    aliases_.push_back({current_.server, current_.token});
    if (!resources_) resources_ = [client = settings_.client_identifier](const auto& token, HANDLE stop, HANDLE request_stop) {
        return PlexAuth(client, stop, request_stop).servers(token);
    };
    if (!request_) request_ = [client = settings_.client_identifier](const auto& origin, const auto& token, const auto& path, HANDLE stop, HANDLE request_stop) {
        return PlexAuth(client, stop, request_stop).get_server(origin, token, path, false);
    };
}
Verbindungsverwaltung::~Verbindungsverwaltung() {
    SecureZeroMemory(settings_.account_token.data(), settings_.account_token.size());
    SecureZeroMemory(settings_.server_token.data(), settings_.server_token.size());
    SecureZeroMemory(current_.token.data(), current_.token.size());
    for (auto& alias : aliases_) SecureZeroMemory(alias.token.data(), alias.token.size());
}
std::optional<Verbindungsziel> Verbindungsverwaltung::ziel(const ServerAddress& server, const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto origin_known = std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) { return gleich(alias.server, server); });
    const auto token_known = std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) { return alias.token == token; });
    if (origin_known && token_known) return current_;
    return std::nullopt;
}
bool Verbindungsverwaltung::passt(const PlexServer& server, const std::string& preferred) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server.identifier != settings_.server_identifier || preferred != settings_.preferred_origin) return false;
    const auto gleiche_verbindungen = [&](const auto& connections) {
        if (server.connections.size() != connections.size()) return false;
        for (std::size_t index = 0; index < server.connections.size(); ++index)
            if (server.connections[index].uri != connections[index].uri) return false;
        return true;
    };
    if (!gleiche_verbindungen(settings_.server_connections) && !gleiche_verbindungen(connection_.connections)) return false;
    return std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) { return alias.token == server.access_token; });
}
void Verbindungsverwaltung::uebernehme_aliasse(const Verbindungsverwaltung& previous) {
    std::lock_guard<std::mutex> lock(previous.mutex_);
    if (previous.settings_.server_identifier != settings_.server_identifier) return;
    for (const auto& alias : previous.aliases_) {
        if (std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& known) { return gleich(known.server, alias.server) && known.token == alias.token; })) continue;
        if (aliases_.size() >= 128) throw HttpError(ERROR_BUFFER_OVERFLOW);
        aliases_.push_back(alias);
    }
}
bool Verbindungsverwaltung::verbindungsfehler(unsigned long code) noexcept {
    return code == ERROR_WINHTTP_NAME_NOT_RESOLVED || code == ERROR_WINHTTP_CANNOT_CONNECT ||
        code == ERROR_WINHTTP_CONNECTION_ERROR || code == ERROR_WINHTTP_TIMEOUT || code == ERROR_TIMEOUT ||
        code == ERROR_WINHTTP_SECURE_FAILURE || code == ERROR_NOT_CONNECTED || code == ERROR_HANDLE_EOF;
}
void Verbindungsverwaltung::abgleichen(PlexConnectionSettings& settings) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!verified_ || settings.server_identifier != settings_.server_identifier) return;
    settings.server_origin = connection_.origin;
    settings.server_token = connection_.server_token;
    settings.server_connections = connection_.connections;
    for (auto& server : settings.known_servers) if (server.identifier == settings.server_identifier) {
        server.access_token = settings.server_token; server.connections = settings.server_connections;
    }
}
PlexConnectionResult Verbindungsverwaltung::pruefen(const PlexServer& server, const std::string& preferred, HANDLE stop, HANDLE request_stop) const {
    abbruch(stop, request_stop);
    return verify_server_connection(server, preferred, [&](const auto& origin, const auto& token, const auto& path) {
        abbruch(stop, request_stop);
        return request_(origin, token, path, stop, request_stop);
    });
}

// Nur ein Worker fragt Plex ab. Wartende Worker übernehmen dessen Ergebnis;
// ein kurzer Fehlerabstand begrenzt Cloud-Anfragen bei längerem Netzausfall.
Verbindungsziel Verbindungsverwaltung::erneuern(unsigned long long generation, HANDLE stop, HANDLE request_stop) {
    std::unique_lock<std::timed_mutex> refresh(erneuerung_, std::defer_lock);
    while (!refresh.try_lock_for(std::chrono::milliseconds(20))) abbruch(stop, request_stop);
    abbruch(stop, request_stop);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != current_.generation) return current_;
        if (!settings_.preferred_origin.empty()) throw HttpError(ERROR_NOT_CONNECTED);
        if (settings_.account_token.empty()) throw HttpError(account_signin_required);
        if (last_error_ && GetTickCount64() < retry_after_) throw HttpError(last_error_);
    }
    try {
        const auto available = resources_(settings_.account_token, stop, request_stop);
        abbruch(stop, request_stop);
        const auto found = std::find_if(available.begin(), available.end(), [&](const auto& server) { return server.identifier == settings_.server_identifier; });
        if (found == available.end()) throw HttpError(ERROR_NOT_FOUND);
        const auto connection = pruefen(*found, {}, stop, request_stop);
        const Verbindungsziel next{parse_server_address(connection.origin), found->access_token, generation + 1};
        std::lock_guard<std::mutex> lock(mutex_);
        const auto known = std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) { return gleich(alias.server, next.server) && alias.token == next.token; });
        if (!known) {
            if (aliases_.size() >= 128) throw HttpError(ERROR_BUFFER_OVERFLOW);
            aliases_.push_back({next.server, next.token});
        }
        current_ = next; connection_ = connection; verified_ = true; last_error_ = 0;
        return current_;
    } catch (const HttpError& error) {
        if (error.code != ERROR_CANCELLED && error.code != ERROR_OPERATION_ABORTED) {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = error.code; retry_after_ = GetTickCount64() + 5000;
        }
        throw;
    }
}
PlexConnectionResult Verbindungsverwaltung::verbinden(HANDLE stop, HANDLE request_stop) {
    abbruch(stop, request_stop);
    unsigned long long generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (verified_) return connection_;
        generation = current_.generation;
    }
    try {
        const auto result = pruefen({settings_.server_identifier, settings_.server_name, settings_.server_token, settings_.server_connections},
                                   settings_.preferred_origin, stop, request_stop);
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == current_.generation) {
            const auto server = parse_server_address(result.origin);
            const auto known = std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) { return gleich(alias.server, server) && alias.token == current_.token; });
            if (!known) {
                if (aliases_.size() >= 128) throw HttpError(ERROR_BUFFER_OVERFLOW);
                aliases_.push_back({server, current_.token});
            }
            current_.server = server;
            ++current_.generation;
            connection_ = result; verified_ = true;
        }
        return connection_;
    } catch (const HttpError& error) {
        if (!verbindungsfehler(error.code) || !settings_.preferred_origin.empty()) throw;
        erneuern(generation, stop, request_stop);
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_;
    }
}
void konfiguriere_verbindung(const PlexConnectionSettings& settings, Verbindungsverwaltung::Ressourcen resources, Verbindungsverwaltung::Anfrage request) {
    std::shared_ptr<Verbindungsverwaltung> next;
    if (!settings.server_origin.empty()) next = std::make_shared<Verbindungsverwaltung>(settings, std::move(resources), std::move(request));
    std::lock_guard<std::mutex> lock(registry_mutex);
    if (next && registry) next->uebernehme_aliasse(*registry);
    registry = std::move(next);
}
void entferne_verbindung() noexcept {
    std::lock_guard<std::mutex> lock(registry_mutex); registry.reset();
}
std::shared_ptr<Verbindungsverwaltung> aktuelle_verbindung() {
    std::lock_guard<std::mutex> lock(registry_mutex); return registry;
}
}
