#pragma once

#include "plex/auth.h"
#include <memory>
#include <mutex>
#include <optional>

namespace plex {
struct Verbindungsziel {
    ServerAddress server;
    std::string token;
    unsigned long long generation = 0;
};

// Ein Profil erneuert seine Adresse, ohne bestehende lokale Playlist-Links zu ändern.
// Die injizierten Aufrufe erlauben IP-Wechseltests ohne echte Konten oder Routereingriffe.
class Verbindungsverwaltung {
public:
    using Ressourcen = std::function<std::vector<PlexServer>(const std::string&, HANDLE, HANDLE)>;
    using Anfrage = std::function<nlohmann::json(const std::string&, const std::string&, const std::string&, HANDLE, HANDLE)>;
    explicit Verbindungsverwaltung(PlexConnectionSettings settings, Ressourcen resources = {}, Anfrage request = {});
    ~Verbindungsverwaltung();
    std::optional<Verbindungsziel> ziel(const ServerAddress& server, const std::string& token) const;
    Verbindungsziel erneuern(unsigned long long generation, HANDLE stop, HANDLE request_stop = nullptr);
    PlexConnectionResult verbinden(HANDLE stop, HANDLE request_stop = nullptr);
    bool passt(const PlexServer& server, const std::string& preferred) const;
    void uebernehme_aliasse(const Verbindungsverwaltung& previous);
    void abgleichen(PlexConnectionSettings& settings) const;
    static bool verbindungsfehler(unsigned long code) noexcept;
private:
    struct Alias { ServerAddress server; std::string token; };
    PlexConnectionResult pruefen(const PlexServer& server, const std::string& preferred, HANDLE stop, HANDLE request_stop) const;
    PlexConnectionSettings settings_;
    Ressourcen resources_;
    Anfrage request_;
    mutable std::mutex mutex_;
    std::timed_mutex erneuerung_;
    std::vector<Alias> aliases_;
    Verbindungsziel current_;
    PlexConnectionResult connection_;
    bool verified_ = false;
    unsigned long last_error_ = 0;
    ULONGLONG retry_after_ = 0;
};

void konfiguriere_verbindung(const PlexConnectionSettings& settings,
    Verbindungsverwaltung::Ressourcen resources = {}, Verbindungsverwaltung::Anfrage request = {});
void entferne_verbindung() noexcept;
std::shared_ptr<Verbindungsverwaltung> aktuelle_verbindung();
}
