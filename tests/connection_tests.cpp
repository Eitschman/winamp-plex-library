#include "plex/connection.h"
#include "plex/http_stream.h"
#include <winhttp.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <future>

namespace {
int checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
template<class Action> void fehler(Action action, unsigned long code, const char* name) {
    try { action(); } catch (const plex::HttpError& error) { check(error.code == code, name); return; }
    throw std::runtime_error(name);
}
struct Ereignis {
    HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Ereignis() { if (value) CloseHandle(value); }
};
plex::PlexConnectionSettings profil() {
    plex::PlexConnectionSettings result{"synthetic-client", "synthetic-server", "https://old.example:32400", "Test", "synthetic-server-token", "7", "Music"};
    result.account_token = "synthetic-account-token";
    result.server_connections = {{result.server_origin, false, false}};
    return result;
}
plex::PlexServer neuer_server() {
    return {"synthetic-server", "Test", "synthetic-renewed-server-token", {{"https://new.example:32400", false, false}}};
}
// Die alte WAN-Adresse ist unerreichbar. Nur der zuvor gewählte Server darf
// nach dem Wechsel Zugangsdaten erhalten; die Fixtures enthalten keine echten Konten.
nlohmann::json anfrage(const std::string& origin, const std::string& token, const std::string& path, HANDLE, HANDLE) {
    if (origin == "https://old.example:32400") throw plex::HttpError(ERROR_WINHTTP_CANNOT_CONNECT);
    if (origin != "https://new.example:32400") throw std::runtime_error("unexpected_origin");
    if (path == "/identity") {
        if (!token.empty()) throw std::runtime_error("identity_has_token");
        return {{"MediaContainer", {{"machineIdentifier", "synthetic-server"}}}};
    }
    if (path != "/library/sections" || token != "synthetic-renewed-server-token") throw std::runtime_error("wrong_server_credential");
    return {{"MediaContainer", {{"Directory", nlohmann::json::array({{{"key", "7"}, {"type", "artist"}, {"title", "Music"}}})}}}};
}
}
int main() {
    try {
        Ereignis stop, cancelled;
        check(stop.value && cancelled.value, "test_events");
        auto settings = profil();
        unsigned discoveries = 0;
        auto resources = [&](const auto& token, HANDLE, HANDLE) {
            ++discoveries;
            if (token != settings.account_token) throw std::runtime_error("account_token_required");
            return std::vector<plex::PlexServer>{neuer_server()};
        };
        plex::Verbindungsverwaltung manager(settings, resources, anfrage);
        fehler([&] { plex::verify_server_connection({settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections}, {},
            [&](const auto& origin, const auto& token, const auto& path) { return anfrage(origin, token, path, stop.value, nullptr); }); },
            ERROR_NOT_CONNECTED, "old_address_without_discovery_reproduces_failure");
        const auto original = plex::parse_server_address(settings.server_origin);
        check(manager.ziel(original, settings.server_token)->server.host == "old.example", "old_target_before_recovery");
        const auto connected = manager.verbinden(stop.value);
        check(connected.origin == "https://new.example:32400" && connected.libraries.size() == 1, "ip_change_on_startup");
        const auto current = manager.ziel(original, settings.server_token);
        check(current && current->server.host == "new.example" && current->token == neuer_server().access_token, "existing_grant_routes_to_new_address_and_resource_token");
        check(manager.ziel(plex::parse_server_address(connected.origin), settings.server_token)->token == neuer_server().access_token,
              "updated_origin_with_old_snapshot_token_uses_current_resource_credential");
        manager.erneuern(0, stop.value);
        check(discoveries == 1, "stale_workers_share_completed_discovery");
        check(manager.passt(neuer_server(), {}), "updated_profile_still_uses_recovery_manager");
        auto synchronized = settings;
        synchronized.known_servers = {{settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections}};
        manager.abgleichen(synchronized);
        check(synchronized.server_origin == connected.origin && synchronized.server_token == neuer_server().access_token &&
              synchronized.known_servers[0].connections[0].uri == connected.origin, "settings_receive_current_address_and_resource_token");
        check(!manager.ziel(plex::parse_server_address("https://radio.example"), settings.server_token), "radio_is_not_owned");
        check(!manager.ziel(original, "synthetic-foreign-token"), "foreign_credential_is_not_owned");
        check(!manager.passt({"different-server", "Other", settings.server_token, {}}, {}), "foreign_server_not_matched");
        plex::Verbindungsverwaltung updated(settings, resources, anfrage);
        updated.uebernehme_aliasse(manager);
        for (unsigned index = 0; index < 150; ++index) updated.uebernehme_aliasse(manager);
        check(updated.ziel(plex::parse_server_address(connected.origin), neuer_server().access_token).has_value(), "settings_keep_previous_playback_aliases");
        auto legacy = settings; legacy.account_token.clear();
        plex::Verbindungsverwaltung old_profile(legacy, resources, anfrage);
        fehler([&] { old_profile.verbinden(stop.value); }, plex::account_signin_required, "legacy_profile_requires_one_signin_without_playlist_deletion");
        auto fixed = settings; fixed.preferred_origin = fixed.server_origin;
        plex::Verbindungsverwaltung pinned(fixed, resources, anfrage);
        fehler([&] { pinned.verbinden(stop.value); }, ERROR_WINHTTP_CANNOT_CONNECT, "manual_connection_remains_fixed");
        check(discoveries == 1, "legacy_and_manual_do_not_query_cloud");
        plex::Verbindungsverwaltung missing(settings, [](const auto&, HANDLE, HANDLE) { return std::vector<plex::PlexServer>{}; }, anfrage);
        fehler([&] { missing.erneuern(0, stop.value); }, ERROR_NOT_FOUND, "missing_selected_server_is_error");
        plex::Verbindungsverwaltung hostile(settings, resources, [](const auto&, const auto& token, const auto&, HANDLE, HANDLE) -> nlohmann::json {
            if (!token.empty()) throw std::runtime_error("credentials_reached_foreign_server");
            return {{"MediaContainer", {{"machineIdentifier", "foreign-server"}}}};
        });
        fehler([&] { hostile.erneuern(0, stop.value); }, ERROR_NOT_CONNECTED, "foreign_identity_never_gets_token");
        unsigned invalid_calls = 0;
        plex::Verbindungsverwaltung revoked(settings, [&](const auto&, HANDLE, HANDLE) -> std::vector<plex::PlexServer> {
            ++invalid_calls; throw plex::HttpError(0x20000000UL + 401);
        }, anfrage);
        fehler([&] { revoked.erneuern(0, stop.value); }, 0x20000000UL + 401, "revoked_account_stays_authentication_error");
        fehler([&] { revoked.erneuern(0, stop.value); }, 0x20000000UL + 401, "failed_discovery_backoff");
        check(invalid_calls == 1, "cloud_error_requests_bounded");
        plex::Verbindungsverwaltung denied(settings, resources, [](const auto&, const auto& token, const auto&, HANDLE, HANDLE) -> nlohmann::json {
            if (token.empty()) return {{"MediaContainer", {{"machineIdentifier", "synthetic-server"}}}};
            throw plex::HttpError(0x20000000UL + 403);
        });
        const auto before_denied = discoveries;
        fehler([&] { denied.verbinden(stop.value); }, 0x20000000UL + 403, "server_denial_does_not_trigger_discovery");
        check(discoveries == before_denied, "server_denial_is_not_an_address_error");
        SetEvent(cancelled.value);
        fehler([&] { manager.erneuern(current->generation, stop.value, cancelled.value); }, ERROR_CANCELLED, "request_cancelled_before_discovery");
        std::atomic<unsigned> concurrent_calls{0}, failures{0};
        plex::Verbindungsverwaltung concurrent(settings, [&](const auto&, HANDLE, HANDLE) {
            ++concurrent_calls; Sleep(60); return std::vector<plex::PlexServer>{neuer_server()};
        }, anfrage);
        std::vector<std::thread> workers;
        for (unsigned index = 0; index < 8; ++index) workers.emplace_back([&] {
            try { if (concurrent.erneuern(0, stop.value).server.host != "new.example") ++failures; } catch (...) { ++failures; }
        });
        for (auto& worker : workers) worker.join();
        check(concurrent_calls == 1 && failures == 0, "parallel_downloads_share_single_cloud_refresh");
        plex::Verbindungsverwaltung repeated(settings, [](const auto&, HANDLE, HANDLE) { return std::vector<plex::PlexServer>{neuer_server()}; }, anfrage);
        for (unsigned generation = 0; generation < 150; ++generation) repeated.erneuern(generation, stop.value);
        check(repeated.ziel(original, settings.server_token)->generation == 150, "same_endpoint_reconnects_do_not_exhaust_alias_budget");
        Ereignis entered, released;
        plex::Verbindungsverwaltung blocked(settings, [&](const auto&, HANDLE, HANDLE) {
            SetEvent(entered.value);
            if (WaitForSingleObject(released.value, 2000) != WAIT_OBJECT_0) throw plex::HttpError(ERROR_TIMEOUT);
            return std::vector<plex::PlexServer>{neuer_server()};
        }, anfrage);
        auto owner = std::async(std::launch::async, [&] { return blocked.erneuern(0, stop.value); });
        if (WaitForSingleObject(entered.value, 2000) != WAIT_OBJECT_0) throw std::runtime_error("refresh_owner_not_started");
        fehler([&] { blocked.erneuern(0, stop.value, cancelled.value); }, ERROR_CANCELLED, "waiting_refresh_worker_can_be_cancelled");
        SetEvent(released.value); owner.get();
        check(!plex::Verbindungsverwaltung::verbindungsfehler(0x20000000UL + 401), "http_401_not_network_failure");
        check(!plex::Verbindungsverwaltung::verbindungsfehler(ERROR_CANCELLED), "cancellation_not_network_failure");
        // Der echte HttpStream muss den Manager erreichen. Die Cloud-Antwort
        // wird injiziert; es werden weder Plex noch fremde Hosts kontaktiert.
        auto transport = settings;
        transport.server_origin = "https://127.0.0.1:1";
        transport.server_connections = {{transport.server_origin, false, false}};
        unsigned transport_refreshes = 0;
        plex::konfiguriere_verbindung(transport, [&](const auto& token, HANDLE, HANDLE) -> std::vector<plex::PlexServer> {
            ++transport_refreshes;
            if (token != transport.account_token) throw std::runtime_error("transport_used_wrong_account_token");
            throw plex::HttpError(ERROR_ACCESS_DENIED);
        }, anfrage);
        const auto endpoint = plex::parse_server_address(transport.server_origin);
        fehler([&] { plex::HttpStream stream(endpoint, transport.server_token, "/library/parts/1/file.mp3", false, {}, stop.value); },
               ERROR_ACCESS_DENIED, "media_transport_enters_address_recovery");
        fehler([&] { plex::PlexAuth(transport.client_identifier, stop.value).get_server(transport.server_origin, transport.server_token, "/library/sections"); },
               ERROR_ACCESS_DENIED, "catalog_transport_enters_address_recovery");
        check(transport_refreshes == 1, "transport_failures_observe_shared_error_cooldown");
        bool write_failed = false;
        try { plex::HttpStream stream(endpoint, transport.server_token, "/:/rate", plex::HttpMethod::put, transport.client_identifier, stop.value); }
        catch (const plex::HttpError& error) { write_failed = plex::Verbindungsverwaltung::verbindungsfehler(error.code); }
        check(write_failed && transport_refreshes == 1, "writes_are_not_replayed_after_transport_failure");
        plex::entferne_verbindung();
        std::cout << checks << " connection checks passed\n";
        return 0;
    } catch (const std::exception& error) { plex::entferne_verbindung(); std::cerr << error.what() << '\n'; return 1; }
}
