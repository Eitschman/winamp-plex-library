#include "plugin/plugin.h"
#include <gen_ml/ml_ipc_0313.h>
#include "plex/auth.h"
#include "plex/http_stream.h"
#include "resource.h"
#include <shellapi.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <algorithm>
#include <winhttp.h>
#include <atomic>

namespace plex::plugin {
struct SettingsPanel {
    int resource;
    DLGPROC procedure;
    const wchar_t* caption;
    LPARAM context = 0;
    bool settings = false;
};
INT_PTR CALLBACK settings_frame(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) noexcept;
namespace {
constexpr UINT state_changed = WM_APP + 31;
HWND active_settings = nullptr;
enum class Step { starting, pin, authorized, libraries, reachable, failed };
const wchar_t* localized(const wchar_t* german_text, const wchar_t* english_text) { return german() ? german_text : english_text; }
const wchar_t* connection_error(DWORD error) {
    if (error == 0x20000000UL + 401 || error == 0x20000000UL + 403)
        return localized(L"Plex-Zugriff verweigert. Erneut mit Plex anmelden.", L"Plex access denied. Sign in with Plex again.");
    if (error == ERROR_WINHTTP_NAME_NOT_RESOLVED)
        return localized(L"Servername nicht gefunden. Adresse und Netzwerk prüfen.", L"Server name not found. Check the address and network.");
    if (error == ERROR_WINHTTP_SECURE_FAILURE || error == ERROR_WINHTTP_SECURE_CERT_DATE_INVALID || error == ERROR_WINHTTP_SECURE_INVALID_CA || error == ERROR_WINHTTP_SECURE_CERT_CN_INVALID)
        return localized(L"HTTPS-Zertifikat ungültig. Eine gültige HTTPS-Adresse verwenden.", L"HTTPS certificate invalid. Use a valid HTTPS address.");
    if (error == ERROR_WINHTTP_TIMEOUT || error == ERROR_TIMEOUT)
        return localized(L"Zeitüberschreitung. Server und Netzwerk prüfen.", L"Request timed out. Check the server and network.");
    if (error == server_identity_mismatch)
        return localized(L"Die Adresse gehört nicht zum ausgewählten Plex-Server.", L"The address does not match the selected Plex server.");
    if (error == ERROR_NOT_CONNECTED || error == ERROR_WINHTTP_CANNOT_CONNECT)
        return localized(L"Keine erreichbare HTTPS-Verbindung. Netzwerk und erweiterte Einstellungen prüfen.", L"No reachable HTTPS connection. Check the network and advanced settings.");
    return localized(L"Plex-Anmeldung oder Serverantwort fehlerhaft. Erneut versuchen.", L"Plex sign-in or server response failed. Try again.");
}
std::wstring diagnostics_information(DWORD connection_code);
// Die Diagnose wächst mit den tatsächlich umgebrochenen Zeilen des Skin-Fonts.
// Ein fester Ressourcenrahmen darf neu ergänzte Zähler nicht abschneiden.
void update_diagnostics(HWND dialog, DWORD connection_code) {
    const auto information = diagnostics_information(connection_code);
    const auto label = GetDlgItem(dialog, IDC_DIAGNOSTICS_TEXT);
    SetWindowTextW(label, information.c_str());
    RECT text{}, button{}, client{};
    GetWindowRect(label, &text); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&text), 2);
    GetWindowRect(GetDlgItem(dialog, IDCANCEL), &button); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&button), 2);
    GetClientRect(dialog, &client);
    const auto dc = GetDC(label);
    if (!dc) throw std::runtime_error("diagnostics_measure");
    const auto font = reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0));
    const auto previous = font ? SelectObject(dc, font) : nullptr;
    RECT measured{0, 0, text.right - text.left, 0};
    DrawTextW(dc, information.c_str(), static_cast<int>(information.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(label, dc);
    // Die gemessene Mindesthöhe steht der nativen Layoutprüfung zur Verfügung.
    // GDI-Fonthandles dürfen nicht in einen anderen Prozess übernommen werden.
    SetPropW(label, L"WinampPlex.RequiredTextHeight", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(measured.bottom)));
    const int padding = (std::max)(8L, text.top);
    const int required = measured.bottom + padding;
    const int growth = (std::max)(0L, required - (text.bottom - text.top));
    if (!growth) return;
    SetWindowPos(label, nullptr, 0, 0, text.right - text.left, required, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(dialog, IDCANCEL), nullptr, button.left, button.top + growth, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(dialog, nullptr, 0, 0, client.right, client.bottom + growth, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const auto frame = GetParent(dialog);
    if (GetDlgItem(frame, IDC_SETTINGS_CONTENT) == dialog) {
        RECT bounds{}; GetWindowRect(frame, &bounds);
        SetWindowPos(frame, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top + growth, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
INT_PTR CALLBACK diagnostics_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            SetWindowLongPtrW(dialog, DWLP_USER, detail);
            SetDlgItemTextW(dialog, IDC_DIAGNOSTICS_TEXT, diagnostics_information(static_cast<DWORD>(detail)).c_str());
            SetDlgItemTextW(dialog, IDCANCEL, localized(L"Schließen", L"Close"));
            skin_library_dialog(dialog);
            update_diagnostics(dialog, static_cast<DWORD>(detail));
            if (!SetTimer(dialog, 1, 1000, nullptr)) throw std::runtime_error("diagnostics_timer");
            return TRUE;
        }
        if (message == WM_TIMER && parameter == 1) {
            update_diagnostics(dialog, static_cast<DWORD>(GetWindowLongPtrW(dialog, DWLP_USER)));
            return TRUE;
        }
        if (message == WM_DESTROY) { KillTimer(dialog, 1); RemovePropW(GetDlgItem(dialog, IDC_DIAGNOSTICS_TEXT), L"WinampPlex.RequiredTextHeight"); }
        if (message == WM_COMMAND && (LOWORD(parameter) == IDOK || LOWORD(parameter) == IDCANCEL)) {
            EndDialog(GetParent(dialog), IDOK); return TRUE;
        }
    } catch (...) { EndDialog(GetParent(dialog), IDCANCEL); return TRUE; }
    return FALSE;
}
std::wstring unicode(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("utf8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

// Die Zähler gehören zur Proxy-Verbindung, nicht zum aktuell abgespielten Titel.
std::wstring diagnostics_information(DWORD connection_code) {
    const auto status = playback_status();
    const auto mib = [](unsigned long long bytes) {
        return std::to_wstring(bytes / 1048576ULL) + (german() ? L"," : L".") +
            std::to_wstring((bytes % 1048576ULL) * 10 / 1048576ULL) + L" MiB";
    };
    return std::wstring(L"Winamp Plex Library\r\nx86\r\n") +
        localized(L"Zugangsspeicherung: Windows DPAPI\r\n", L"Credential storage: Windows DPAPI\r\n") +
        L"HTTP: " + std::to_wstring(status.upstream_status) + L" / Proxy: " + unicode(status.local_rejection ? status.local_rejection : "none") +
        L"\r\nWindows: " + std::to_wstring(status.transport_error) +
        localized(L"\r\nVerbindungscode: ", L"\r\nConnection code: ") + std::to_wstring(connection_code) +
        localized(L"\r\nCover-Zuordnungen: ", L"\r\nArtwork mappings: ") + std::to_wstring(artwork_provider_diagnostic(3)) +
        localized(L"\r\nCover-Abfragen: ", L"\r\nArtwork requests: ") + std::to_wstring(artwork_provider_diagnostic(2)) +
        localized(L"\r\n\r\nEmpfangen seit Verbindungsstart: ", L"\r\n\r\nReceived since connection start: ") + mib(status.bytes_received) +
        localized(L"\r\nAn Winamp gesendet: ", L"\r\nSent to Winamp: ") + mib(status.bytes_sent) +
        localized(L"\r\nLaufende Streaming-Downloads: ", L"\r\nActive streaming downloads: ") + std::to_wstring(status.active_downloads) +
        localized(L"\r\nReservierter Medienspeicher: ", L"\r\nReserved media storage: ") + mib(status.reserved_media_bytes) + L" / 2048 MiB" +
        localized(L"\r\nVorrat für nächste Titel: ", L"\r\nNext-track cache: ") + mib(status.prefetched_bytes) + L" / 64 MiB" +
        localized(L"\r\nVorladen aktiv / Treffer / Fehlercode: ", L"\r\nPrefetch active / hits / error code: ") +
        std::to_wstring(status.prefetch_active) + L" / " + std::to_wstring(status.prefetch_hits) + L" / " + std::to_wstring(status.prefetch_error) +
        localized(L"\r\n\r\nÜbertragungsfehler: ", L"\r\n\r\nTransfer failures: ") + std::to_wstring(status.last_failure.count) +
        localized(L"\r\nLetzter Fehler (HTTP / Windows): ", L"\r\nLast failure (HTTP / Windows): ") +
        std::to_wstring(status.last_failure.upstream_status) + L" / " + std::to_wstring(status.last_failure.transport_error) +
        localized(L"\r\nLetzter Fehler, empfangen / erwartet (Bytes): ", L"\r\nLast failure, received / expected (bytes): ") +
        std::to_wstring(status.last_failure.transferred_bytes) + L" / " + std::to_wstring(status.last_failure.expected_bytes) +
        localized(L"\r\nLokale Verbindungsenden: ", L"\r\nLocal disconnects: ") + std::to_wstring(status.local_disconnects);
}

struct Login {
    HWND dialog;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker;
    std::mutex mutex;
    Step step = Step::starting;
    std::string auth_url;
    std::vector<PlexServer> servers;
    std::vector<PlexLibrary> libraries;
    PlexConnectionSettings settings = connection_settings();
    std::atomic<DWORD> error{0};
    bool browser_opened = false;
    bool freshly_verified = false;
    bool advanced = true;
    bool manual_connection = !settings.preferred_origin.empty();
    bool changing_address = false;

    explicit Login(HWND window) : dialog(window) { if (!stop) throw std::runtime_error("login_event"); }
    ~Login() { SetEvent(stop); if (worker.joinable()) worker.join(); CloseHandle(stop); }
    void publish(Step value) {
        { std::lock_guard<std::mutex> lock(mutex); step = value; }
        PostMessageW(dialog, state_changed, 0, 0);
    }
    void authenticate() noexcept {
        try {
            PlexAuth auth(settings.client_identifier, stop);
            const auto pin = auth.create_pin();
            { std::lock_guard<std::mutex> lock(mutex); auth_url = pin.auth_url; }
            publish(Step::pin);
            const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(pin.expires_in);
            while (std::chrono::steady_clock::now() < expires) {
                if (WaitForSingleObject(stop, 1000) == WAIT_OBJECT_0) return;
                auto token = auth.check_pin(pin);
                if (token.empty()) continue;
                try {
                    auto available = auth.servers(token);
                    SecureZeroMemory(token.data(), token.size());
                    { std::lock_guard<std::mutex> lock(mutex); servers = std::move(available); }
                    error = 0; publish(Step::authorized); return;
                } catch (...) { SecureZeroMemory(token.data(), token.size()); throw; }
            }
            error = ERROR_TIMEOUT; publish(Step::failed);
        } catch (const HttpError& failure) { error = failure.code; publish(Step::failed); }
          catch (...) { error = ERROR_INVALID_DATA; publish(Step::failed); }
    }
    void connect(std::size_t server_index, std::string origin, bool automatic) noexcept {
        try {
            PlexServer server;
            {
                std::lock_guard<std::mutex> lock(mutex);
                server = servers.at(server_index);
            }
            PlexAuth auth(settings.client_identifier, stop);
            auto connection = auth.connect_server(server, automatic ? std::string{} : origin);
            origin = std::move(connection.origin);
            auto found = std::move(connection.libraries);
            {
                std::lock_guard<std::mutex> lock(mutex);
                libraries = std::move(found);
                if (settings.server_identifier != server.identifier) {
                    settings.library_identifier.clear(); settings.library_title.clear();
                }
                settings.server_identifier = server.identifier;
                settings.server_name = server.name;
                settings.server_origin = origin;
                settings.preferred_origin = automatic ? std::string{} : origin;
                settings.server_connections = server.connections;
                settings.known_servers = servers;
                settings.server_token = server.access_token;
                freshly_verified = true;
            }
            error = 0; publish(Step::libraries);
        } catch (const HttpError& failure) { error = failure.code; publish(Step::failed); }
          catch (...) { error = ERROR_INVALID_DATA; publish(Step::failed); }
    }
    void probe(const std::string& origin) noexcept {
        try {
            const auto response = PlexAuth(settings.client_identifier, stop).get_server(origin, {}, "/identity");
            if (response.at("MediaContainer").at("machineIdentifier").get<std::string>().empty())
                throw std::invalid_argument("server_identity_missing");
            error = 0; publish(Step::reachable);
        } catch (const HttpError& failure) { error = failure.code; publish(Step::failed); }
          catch (...) { error = ERROR_INVALID_DATA; publish(Step::failed); }
    }
};

std::string address(HWND dialog) {
    wchar_t value[2049]{};
    if (GetWindowTextLengthW(GetDlgItem(dialog, IDC_LOGIN_ADDRESS)) >= 2048) throw std::invalid_argument("address_limit");
    GetDlgItemTextW(dialog, IDC_LOGIN_ADDRESS, value, 2049);
    std::string result;
    for (const auto character : std::wstring(value)) {
        if (character < 33 || character > 126) throw std::invalid_argument("address_encoding");
        result.push_back(static_cast<char>(character));
    }
    if (!result.empty() && !parse_server_address(result).secure) throw std::invalid_argument("https_required");
    return result;
}

void set_address(HWND dialog, Login& login, const std::string& value) {
    login.changing_address = true;
    SetDlgItemTextW(dialog, IDC_LOGIN_ADDRESS, unicode(value).c_str());
    login.changing_address = false;
}

const wchar_t* verified_status(const Login& login) {
    for (const auto& connection : login.settings.server_connections) if (connection.uri == login.settings.server_origin) {
        if (connection.relay) return localized(L"Verbunden: Plex Relay (HTTPS).", L"Connected: Plex Relay (HTTPS).");
        if (connection.local) return localized(L"Verbunden: lokal (HTTPS).", L"Connected: local (HTTPS).");
        return localized(L"Verbunden: Fernzugriff (HTTPS).", L"Connected: remote (HTTPS).");
    }
    return localized(L"Verbindung geprüft (HTTPS).", L"Connection verified (HTTPS).");
}

void fill_connections(HWND dialog, Login& login) {
    const auto index = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_RESETCONTENT, 0, 0);
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), FALSE);
    if (index < 0 || static_cast<std::size_t>(index) >= login.servers.size()) return;
    const auto& connections = login.servers[static_cast<std::size_t>(index)].connections;
    wchar_t draft[2049]{}; GetDlgItemTextW(dialog, IDC_LOGIN_ADDRESS, draft, 2049);
    int preferred = login.manual_connection ? static_cast<int>(connections.size()) + 1 : 0;
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localized(L"Automatisch", L"Automatic")));
    for (std::size_t position = 0; position < connections.size(); ++position) {
        const auto& connection = connections[position];
        const auto prefix = connection.relay ? L"Relay (HTTPS)" : connection.local ? localized(L"Lokal (HTTPS)", L"Local (HTTPS)") : localized(L"Fernzugriff (HTTPS)", L"Remote (HTTPS)");
        const auto label = std::wstring(prefix) + (connections.size() > 2 ? L" " + std::to_wstring(position + 1) : L"");
        SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (login.manual_connection && unicode(connection.uri) == draft) preferred = static_cast<int>(position) + 1;
    }
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localized(L"Benutzerdefiniert", L"Custom")));
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_SETCURSEL, preferred, 0);
    if (GetWindowTextLengthW(GetDlgItem(dialog, IDC_LOGIN_ADDRESS)) == 0 && !connections.empty())
        set_address(dialog, login, preferred > 0 && preferred <= static_cast<int>(connections.size()) ? connections[static_cast<std::size_t>(preferred - 1)].uri : connections.front().uri);
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), !connections.empty());
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), !connections.empty());
}

// Das optionale Adressfeld nimmt im üblichen Plex-Anmeldeablauf keinen Platz ein.
void expand_advanced(HWND dialog, Login& login, bool expanded) {
    if (login.advanced == expanded) return;
    RECT spacing{0, 0, 0, 82}; MapDialogRect(dialog, &spacing);
    const auto offset = expanded ? spacing.bottom : -spacing.bottom;
    // Verborgene Eingaben besitzen keine aktive Skin-Unterklasse. Dadurch kann
    // deren Fokuszeichnung keine überlappenden sichtbaren Controls übermalen.
    if (!expanded) {
        for (const auto identifier : {IDC_LOGIN_CONNECTION, IDC_LOGIN_ADDRESS})
            if (!MLUnskinWindow(descriptor().hwndLibraryParent, GetDlgItem(dialog, identifier))) throw std::runtime_error("unskin_advanced_control");
    }
    for (const auto identifier : {IDC_LOGIN_CACHE_LABEL, IDC_LOGIN_CACHE,
                                 IDC_LOGIN_CLEAR_CACHE, IDC_LOGIN_DIAGNOSTICS, IDOK, IDCANCEL}) {
        const auto child = GetDlgItem(dialog, identifier);
        RECT rectangle{}; GetWindowRect(child, &rectangle);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rectangle), 2);
        SetWindowPos(child, nullptr, rectangle.left, rectangle.top + offset, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ShowWindow(GetDlgItem(dialog, IDC_LOGIN_ADDRESS_LABEL), expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_LOGIN_ADDRESS), expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION_LABEL), expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), expanded ? SW_SHOW : SW_HIDE);
    if (expanded) {
        for (const auto identifier : {IDC_LOGIN_CONNECTION, IDC_LOGIN_ADDRESS}) {
            MLSKINWINDOW window{GetDlgItem(dialog, identifier), SKINNEDWND_TYPE_AUTO,
                                SWS_USESKINCOLORS | SWS_USESKINFONT | SWS_USESKINCURSORS};
            if (!MLSkinWindow(descriptor().hwndLibraryParent, &window)) throw std::runtime_error("skin_advanced_control");
        }
    }
    // Auch die gen_ml-Unterklasse darf verborgene Eingaben nicht in den Fokus nehmen.
    for (const auto identifier : {IDC_LOGIN_CONNECTION, IDC_LOGIN_ADDRESS}) {
        const auto control = GetDlgItem(dialog, identifier);
        const auto style = GetWindowLongPtrW(control, GWL_STYLE);
        SetWindowLongPtrW(control, GWL_STYLE, expanded ? style | WS_TABSTOP : style & ~WS_TABSTOP);
    }
    RECT rectangle{}; GetWindowRect(dialog, &rectangle);
    SetWindowPos(dialog, nullptr, 0, 0, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top + offset,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const auto frame = GetParent(dialog);
    RECT frame_rectangle{}, client{};
    GetWindowRect(frame, &frame_rectangle); GetClientRect(frame, &client);
    const auto content_height = rectangle.bottom - rectangle.top + offset;
    SetWindowPos(frame, nullptr, 0, 0, frame_rectangle.right - frame_rectangle.left,
                 frame_rectangle.bottom - frame_rectangle.top + content_height - client.bottom,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    login.advanced = expanded;
    SetDlgItemTextW(dialog, IDC_LOGIN_ADVANCED, expanded ? localized(L"Erweitert schließen", L"Close advanced") :
                                                        localized(L"Erweitert...", L"Advanced..."));
    InvalidateRect(dialog, nullptr, TRUE);
}

INT_PTR CALLBACK login_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM) noexcept {
    auto* login = reinterpret_cast<Login*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            auto state = std::make_unique<Login>(dialog);
            login = state.get();
            SetWindowTextW(dialog, localized(L"Plex-Einstellungen", L"Plex settings"));
            SetDlgItemTextW(dialog, IDC_LOGIN_CONNECTION_LABEL, localized(L"Verbindung", L"Connection"));
            SetDlgItemTextW(dialog, IDC_LOGIN_LIBRARY_LABEL, localized(L"Musikbibliothek", L"Music library"));
            SetDlgItemTextW(dialog, IDC_LOGIN_CONNECT, localized(L"Verbindung testen", L"Test connection"));
            SetDlgItemTextW(dialog, IDC_LOGIN_ADDRESS_LABEL, localized(L"Plex-Serveradresse (HTTPS)", L"Plex server address (HTTPS)"));
            SetDlgItemTextW(dialog, IDC_LOGIN_CACHE_LABEL, localized(L"Metadaten- und Cover-Cache", L"Metadata and artwork cache"));
            SetDlgItemTextW(dialog, IDC_LOGIN_REAUTH, localized(L"Mit Plex anmelden...", L"Sign in with Plex..."));
            SetDlgItemTextW(dialog, IDC_LOGIN_CLEAR_CACHE, localized(L"Cache leeren", L"Clear cache"));
            SetDlgItemTextW(dialog, IDC_LOGIN_DIAGNOSTICS, localized(L"Diagnose...", L"Diagnostics..."));
            SetDlgItemTextW(dialog, IDOK, localized(L"Speichern", L"Save"));
            SetDlgItemTextW(dialog, IDCANCEL, localized(L"Abbrechen", L"Cancel"));
            SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Nicht bei Plex angemeldet.", L"Not signed in to Plex."));
            SendDlgItemMessageW(dialog, IDC_LOGIN_ADDRESS, EM_SETLIMITTEXT, 2048, 0);
            SetDlgItemTextW(dialog, IDC_LOGIN_ADDRESS, unicode(login->settings.preferred_origin.empty() ? login->settings.server_origin : login->settings.preferred_origin).c_str());
            SendDlgItemMessageW(dialog, IDC_LOGIN_CACHE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localized(L"Aktiviert", L"Enabled")));
            SendDlgItemMessageW(dialog, IDC_LOGIN_CACHE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(localized(L"Deaktiviert", L"Disabled")));
            SendDlgItemMessageW(dialog, IDC_LOGIN_CACHE, CB_SETCURSEL, login->settings.cache_enabled ? 0 : 1, 0);
            for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY, IDC_LOGIN_CONNECT, IDOK})
                EnableWindow(GetDlgItem(dialog, identifier), FALSE);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state.release()));
            if (!login->settings.server_origin.empty()) {
                login->servers = login->settings.known_servers;
                if (login->servers.empty()) {
                    auto connections = login->settings.server_connections;
                    if (connections.empty()) connections.push_back({login->settings.server_origin, false, false});
                    login->servers.push_back({login->settings.server_identifier, login->settings.server_name, login->settings.server_token, std::move(connections)});
                }
                login->libraries.push_back({login->settings.library_identifier, login->settings.library_title});
                for (std::size_t index = 0; index < login->servers.size(); ++index) {
                    SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(unicode(login->servers[index].name).c_str()));
                    if (login->servers[index].identifier == login->settings.server_identifier)
                        SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_SETCURSEL, index, 0);
                }
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), TRUE);
                fill_connections(dialog, *login);
                login->publish(Step::libraries);
                PostMessageW(dialog, WM_COMMAND, IDC_LOGIN_CONNECT, 0);
            } else {
                EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), !login->settings.preferred_origin.empty());
            }
            skin_library_dialog(dialog);
            expand_advanced(dialog, *login, false);
            return TRUE;
        }
        if (!login) return FALSE;
        if (message == state_changed) {
            std::lock_guard<std::mutex> lock(login->mutex);
            if (login->step == Step::pin && !login->browser_opened) {
                login->browser_opened = true;
                const auto url = unicode(login->auth_url);
                if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                    throw std::runtime_error("browser_launch");
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Warte auf Anmeldung im Browser.", L"Waiting for sign-in in your browser."));
            } else if (login->step == Step::authorized) {
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_REAUTH), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_ADDRESS), TRUE);
                SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_RESETCONTENT, 0, 0);
                for (const auto& server : login->servers) {
                    const auto label = unicode(server.name);
                    SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                }
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), !login->servers.empty());
                SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_SETCURSEL, 0, 0);
                for (std::size_t index = 0; index < login->servers.size(); ++index)
                    if (login->servers[index].identifier == login->settings.server_identifier)
                        SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_SETCURSEL, index, 0);
                fill_connections(dialog, *login);
                SetDlgItemTextW(dialog, IDC_STATUS, login->servers.empty() ?
                    localized(L"Für dieses Konto sind keine Plex-Server verfügbar.", L"No Plex servers are available for this account.") :
                    localized(L"Bei Plex angemeldet.", L"Signed in to Plex."));
                if (!login->servers.empty()) PostMessageW(dialog, WM_COMMAND, IDC_LOGIN_CONNECT, 0);
            } else if (login->step == Step::libraries) {
                set_address(dialog, *login, login->settings.server_origin);
                SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_RESETCONTENT, 0, 0);
                for (const auto& library : login->libraries) {
                    const auto label = unicode(library.title);
                    SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                }
                SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_SETCURSEL, 0, 0);
                for (std::size_t index = 0; index < login->libraries.size(); ++index)
                    if (login->libraries[index].identifier == login->settings.library_identifier)
                        SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_SETCURSEL, index, 0);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), !login->libraries.empty());
                EnableWindow(GetDlgItem(dialog, IDOK), !login->libraries.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_REAUTH), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_ADDRESS), TRUE);
                SetDlgItemTextW(dialog, IDC_STATUS, login->libraries.empty() ? localized(L"Keine Musikbibliothek gefunden.", L"No music library found.") :
                    login->freshly_verified ? verified_status(*login) : localized(L"Gespeicherte Verbindung.", L"Saved connection."));
            } else if (login->step == Step::reachable) {
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Server erreichbar. Für die Bibliotheken bei Plex anmelden.", L"Server reachable. Sign in to Plex to access libraries."));
                for (const auto identifier : {IDC_LOGIN_REAUTH, IDC_LOGIN_ADDRESS, IDC_LOGIN_CONNECT, IDOK}) EnableWindow(GetDlgItem(dialog, identifier), TRUE);
            } else if (login->step == Step::failed) {
                SetDlgItemTextW(dialog, IDC_STATUS, connection_error(login->error));
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), !login->servers.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), !login->servers.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_REAUTH), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_ADDRESS), TRUE);
                EnableWindow(GetDlgItem(dialog, IDOK), login->settings.server_origin.empty() || address(dialog) == login->settings.server_origin);
            }
            return TRUE;
        }
        if (message == WM_COMMAND) {
            if (LOWORD(parameter) == IDC_LOGIN_ADVANCED) {
                expand_advanced(dialog, *login, !login->advanced); return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_REAUTH) {
                if (login->worker.joinable()) login->worker.join();
                login->browser_opened = false; login->freshly_verified = false;
                login->servers.clear(); login->libraries.clear(); login->auth_url.clear();
                for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY})
                    SendDlgItemMessageW(dialog, identifier, CB_RESETCONTENT, 0, 0);
                for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY, IDC_LOGIN_CONNECT, IDC_LOGIN_REAUTH, IDC_LOGIN_ADDRESS, IDOK})
                    EnableWindow(GetDlgItem(dialog, identifier), FALSE);
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Plex-Anmeldung wird angefordert...", L"Requesting Plex sign-in..."));
                login->worker = std::thread([login] { login->authenticate(); }); return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_ADDRESS && HIWORD(parameter) == EN_CHANGE) {
                if (login->changing_address) return TRUE;
                login->manual_connection = true;
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), GetWindowTextLengthW(GetDlgItem(dialog, IDC_LOGIN_ADDRESS)) != 0);
                const auto server = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
                if (server >= 0)
                    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_SETCURSEL, login->servers.at(static_cast<std::size_t>(server)).connections.size() + 1, 0);
                login->freshly_verified = false;
                EnableWindow(GetDlgItem(dialog, IDOK), login->settings.server_origin.empty());
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Adresse geändert. Verbindung testen.", L"Address changed. Test the connection."));
                return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_CLEAR_CACHE) {
                clear_library_cache();
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Cache geleert.", L"Cache cleared.")); return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_DIAGNOSTICS) {
                SettingsPanel panel{IDD_PLEX_DIAGNOSTICS, diagnostics_dialog, localized(L"Plex-Diagnose", L"Plex diagnostics"), static_cast<LPARAM>(login->error.load())};
                DialogBoxParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_SETTINGS_FRAME), GetParent(dialog), settings_frame, reinterpret_cast<LPARAM>(&panel));
                return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_SERVER && HIWORD(parameter) == CBN_SELCHANGE) {
                login->manual_connection = false;
                set_address(dialog, *login, {});
                std::lock_guard<std::mutex> lock(login->mutex); fill_connections(dialog, *login);
                PostMessageW(dialog, WM_COMMAND, IDC_LOGIN_CONNECT, 0); return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_CONNECTION && HIWORD(parameter) == CBN_SELCHANGE) {
                const auto server = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
                const auto connection = SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_GETCURSEL, 0, 0);
                login->manual_connection = connection > 0;
                if (server >= 0 && connection > 0 && static_cast<std::size_t>(connection) <= login->servers.at(static_cast<std::size_t>(server)).connections.size())
                    set_address(dialog, *login, login->servers.at(static_cast<std::size_t>(server)).connections.at(static_cast<std::size_t>(connection - 1)).uri);
                EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), FALSE);
                return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_CONNECT) {
                const auto server = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
                const auto automatic = !login->manual_connection && server >= 0;
                const auto origin = automatic ? std::string{} : address(dialog);
                if (!automatic && origin.empty()) throw std::invalid_argument("server_address_missing");
                if (login->worker.joinable()) login->worker.join();
                for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY, IDC_LOGIN_CONNECT, IDC_LOGIN_REAUTH, IDC_LOGIN_ADDRESS, IDOK})
                    EnableWindow(GetDlgItem(dialog, identifier), FALSE);
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Verbindung wird geprüft...", L"Checking connection..."));
                login->worker = std::thread([login, server, origin, automatic] {
                    if (server < 0) login->probe(origin);
                    else login->connect(static_cast<std::size_t>(server), origin, automatic);
                });
                return TRUE;
            }
            if (LOWORD(parameter) == IDOK) {
                const auto origin = address(dialog);
                if (!login->settings.server_origin.empty() && origin != login->settings.server_origin)
                    throw std::invalid_argument("connection_not_verified");
                const auto library = SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_GETCURSEL, 0, 0);
                if (library < 0 && !login->settings.server_origin.empty()) return TRUE;
                { std::lock_guard<std::mutex> lock(login->mutex);
                  if (library >= 0) {
                      login->settings.library_identifier = login->libraries.at(static_cast<std::size_t>(library)).identifier;
                      login->settings.library_title = login->libraries.at(static_cast<std::size_t>(library)).title;
                  }
                  login->settings.preferred_origin = login->manual_connection ? origin : std::string{};
                  login->settings.cache_enabled = SendDlgItemMessageW(dialog, IDC_LOGIN_CACHE, CB_GETCURSEL, 0, 0) == 0;
                  update_connection_settings(login->settings);
                }
                EndDialog(GetParent(dialog), IDOK); return TRUE;
            }
        }
        if ((message == WM_COMMAND && LOWORD(parameter) == IDCANCEL) || message == WM_CLOSE) {
            EndDialog(GetParent(dialog), IDCANCEL); return TRUE;
        }
        if (message == WM_DESTROY) {
            SetWindowLongPtrW(dialog, DWLP_USER, 0); delete login; return TRUE;
        }
    } catch (const std::invalid_argument&) {
        SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Gültige HTTPS-Basisadresse eingeben und die geänderte Verbindung testen.", L"Enter a valid HTTPS base address and test the changed connection."));
        return TRUE;
    } catch (...) {
        if (message == WM_INITDIALOG) { EndDialog(GetParent(dialog), IDCANCEL); return TRUE; }
        SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Plex-Einstellungen konnten nicht verarbeitet werden.", L"Could not process Plex settings."));
        return TRUE;
    }
    return FALSE;
}
}

INT_PTR CALLBACK settings_frame(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) noexcept {
    if (message == WM_INITDIALOG) {
        const auto& panel = *reinterpret_cast<const SettingsPanel*>(detail);
        if (panel.settings) active_settings = dialog;
        SetWindowTextW(dialog, panel.caption);
        const auto content = CreateDialogParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(panel.resource), dialog, panel.procedure, panel.context);
        if (!content) { EndDialog(dialog, IDCANCEL); return TRUE; }
        SetWindowLongPtrW(content, GWLP_ID, IDC_SETTINGS_CONTENT);
        RECT frame_rectangle{}, client{}, content_rectangle{};
        GetWindowRect(dialog, &frame_rectangle); GetClientRect(dialog, &client); GetWindowRect(content, &content_rectangle);
        SetWindowPos(dialog, nullptr, 0, 0,
                     frame_rectangle.right - frame_rectangle.left + content_rectangle.right - content_rectangle.left - client.right,
                     frame_rectangle.bottom - frame_rectangle.top + content_rectangle.bottom - content_rectangle.top - client.bottom,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(content, SW_SHOW);
        RECT owner{}, rectangle{}; GetWindowRect(GetParent(dialog), &owner); GetWindowRect(dialog, &rectangle);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const auto width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
            const auto left = (owner.left + owner.right - width) / 2;
            const auto top = (owner.top + owner.bottom - height) / 2;
            SetWindowPos(dialog, nullptr, (std::max)(monitor.rcWork.left, (std::min)(left, monitor.rcWork.right - width)),
                         (std::max)(monitor.rcWork.top, (std::min)(top, monitor.rcWork.bottom - height)), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(parameter) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        return SendMessageW(GetDlgItem(dialog, IDC_SETTINGS_CONTENT), message, parameter, detail);
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message == WM_DESTROY) { if (active_settings == dialog) active_settings = nullptr; return TRUE; }
    return FALSE;
}

bool show_settings(HWND parent) noexcept {
    if (IsWindow(active_settings)) { SetForegroundWindow(active_settings); return false; }
    SettingsPanel panel{IDD_PLEX_LOGIN, login_dialog, localized(L"Plex-Einstellungen", L"Plex settings"), 0, true};
    return DialogBoxParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_SETTINGS_FRAME), parent, settings_frame, reinterpret_cast<LPARAM>(&panel)) == IDOK;
}
bool show_library_panel(HWND parent, int resource, DLGPROC procedure, LPARAM context, const wchar_t* caption) {
    SettingsPanel panel{resource, procedure, caption, context};
    return DialogBoxParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_SETTINGS_FRAME), parent, settings_frame, reinterpret_cast<LPARAM>(&panel)) == IDOK;
}
}
