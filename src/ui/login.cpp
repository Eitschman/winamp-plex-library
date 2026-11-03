#include "plugin/plugin.h"
#include "plex/auth.h"
#include "plex/http_stream.h"
#include "resource.h"
#include <shellapi.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace plex::plugin {
namespace {
constexpr UINT state_changed = WM_APP + 31;
enum class Step { starting, pin, authorized, libraries, failed };
const wchar_t* localized(const wchar_t* german_text, const wchar_t* english_text) { return german() ? german_text : english_text; }
std::wstring unicode(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("utf8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
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
    DWORD error = 0;
    bool browser_opened = false;

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
                    publish(Step::authorized); return;
                } catch (...) { SecureZeroMemory(token.data(), token.size()); throw; }
            }
            error = ERROR_TIMEOUT; publish(Step::failed);
        } catch (const HttpError& failure) { error = failure.code; publish(Step::failed); }
          catch (...) { error = ERROR_INVALID_DATA; publish(Step::failed); }
    }
    void connect(std::size_t server_index, std::size_t connection_index) noexcept {
        try {
            PlexServer server;
            PlexConnection connection;
            {
                std::lock_guard<std::mutex> lock(mutex);
                server = servers.at(server_index);
                connection = server.connections.at(connection_index);
            }
            PlexAuth auth(settings.client_identifier, stop);
            const auto identity = auth.get_server(connection.uri, server.access_token, "/identity");
            if (identity.at("MediaContainer").at("machineIdentifier") != server.identifier)
                throw std::invalid_argument("server_identity_mismatch");
            auto found = parse_libraries(auth.get_server(connection.uri, server.access_token, "/library/sections"));
            {
                std::lock_guard<std::mutex> lock(mutex);
                libraries = std::move(found);
                settings.server_identifier = server.identifier;
                settings.server_name = server.name;
                settings.server_origin = connection.uri;
                settings.server_token = server.access_token;
            }
            publish(Step::libraries);
        } catch (const HttpError& failure) { error = failure.code; publish(Step::failed); }
          catch (...) { error = ERROR_INVALID_DATA; publish(Step::failed); }
    }
};

void fill_connections(HWND dialog, Login& login) {
    const auto index = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_RESETCONTENT, 0, 0);
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), FALSE);
    if (index < 0 || static_cast<std::size_t>(index) >= login.servers.size()) return;
    const auto& connections = login.servers[static_cast<std::size_t>(index)].connections;
    int preferred = -1;
    for (std::size_t position = 0; position < connections.size(); ++position) {
        const auto& connection = connections[position];
        const auto prefix = connection.relay ? L"Relay (HTTPS)" : connection.local ? localized(L"Lokal (HTTPS)", L"Local (HTTPS)") : localized(L"Fernzugriff (HTTPS)", L"Remote (HTTPS)");
        const auto label = std::wstring(prefix) + (connections.size() > 2 ? L" " + std::to_wstring(position + 1) : L"");
        SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (preferred < 0 || (!connection.local && !connection.relay)) preferred = static_cast<int>(position);
    }
    SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_SETCURSEL, preferred, 0);
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), !connections.empty());
    EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), !connections.empty());
}

INT_PTR CALLBACK login_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM) noexcept {
    auto* login = reinterpret_cast<Login*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            auto state = std::make_unique<Login>(dialog);
            login = state.get();
            SetWindowTextW(dialog, localized(L"Mit Plex anmelden", L"Sign in with Plex"));
            SetDlgItemTextW(dialog, IDC_LOGIN_CONNECTION_LABEL, localized(L"Verbindung", L"Connection"));
            SetDlgItemTextW(dialog, IDC_LOGIN_LIBRARY_LABEL, localized(L"Musikbibliothek", L"Music library"));
            SetDlgItemTextW(dialog, IDC_LOGIN_CONNECT, localized(L"Verbinden", L"Connect"));
            SetDlgItemTextW(dialog, IDOK, localized(L"Speichern", L"Save"));
            SetDlgItemTextW(dialog, IDCANCEL, localized(L"Abbrechen", L"Cancel"));
            SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Plex-Anmeldung wird angefordert...", L"Requesting Plex sign-in..."));
            for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY, IDC_LOGIN_CONNECT, IDOK})
                EnableWindow(GetDlgItem(dialog, identifier), FALSE);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state.release()));
            login->worker = std::thread([login] { login->authenticate(); });
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
                SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_RESETCONTENT, 0, 0);
                for (const auto& server : login->servers) {
                    const auto label = unicode(server.name);
                    SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                }
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), !login->servers.empty());
                SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_SETCURSEL, 0, 0);
                fill_connections(dialog, *login);
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Bei Plex angemeldet.", L"Signed in to Plex."));
            } else if (login->step == Step::libraries) {
                SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_RESETCONTENT, 0, 0);
                for (const auto& library : login->libraries) {
                    const auto label = unicode(library.title);
                    SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                }
                SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_SETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), !login->libraries.empty());
                EnableWindow(GetDlgItem(dialog, IDOK), !login->libraries.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), TRUE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), TRUE);
                SetDlgItemTextW(dialog, IDC_STATUS, login->libraries.empty() ? localized(L"Keine Musikbibliothek gefunden.", L"No music library found.") : localized(L"Verbindung geprüft.", L"Connection verified."));
            } else if (login->step == Step::failed) {
                wchar_t status[160]{};
                swprintf_s(status, localized(L"Anmeldung oder Verbindung fehlgeschlagen (Code %lu).", L"Sign-in or connection failed (code %lu)."), login->error);
                SetDlgItemTextW(dialog, IDC_STATUS, status);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_SERVER), !login->servers.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECTION), !login->servers.empty());
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_CONNECT), !login->servers.empty());
            }
            return TRUE;
        }
        if (message == WM_COMMAND) {
            if (LOWORD(parameter) == IDC_LOGIN_SERVER && HIWORD(parameter) == CBN_SELCHANGE) {
                std::lock_guard<std::mutex> lock(login->mutex); fill_connections(dialog, *login); return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_CONNECTION && HIWORD(parameter) == CBN_SELCHANGE) {
                EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
                EnableWindow(GetDlgItem(dialog, IDC_LOGIN_LIBRARY), FALSE);
                return TRUE;
            }
            if (LOWORD(parameter) == IDC_LOGIN_CONNECT) {
                if (login->worker.joinable()) login->worker.join();
                const auto server = SendDlgItemMessageW(dialog, IDC_LOGIN_SERVER, CB_GETCURSEL, 0, 0);
                const auto connection = SendDlgItemMessageW(dialog, IDC_LOGIN_CONNECTION, CB_GETCURSEL, 0, 0);
                if (server < 0 || connection < 0) return TRUE;
                for (const auto identifier : {IDC_LOGIN_SERVER, IDC_LOGIN_CONNECTION, IDC_LOGIN_LIBRARY, IDC_LOGIN_CONNECT, IDOK})
                    EnableWindow(GetDlgItem(dialog, identifier), FALSE);
                SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Verbindung wird geprüft...", L"Checking connection..."));
                login->worker = std::thread([login, server, connection] { login->connect(static_cast<std::size_t>(server), static_cast<std::size_t>(connection)); });
                return TRUE;
            }
            if (LOWORD(parameter) == IDOK) {
                const auto library = SendDlgItemMessageW(dialog, IDC_LOGIN_LIBRARY, CB_GETCURSEL, 0, 0);
                if (library < 0) return TRUE;
                { std::lock_guard<std::mutex> lock(login->mutex);
                  login->settings.library_identifier = login->libraries.at(static_cast<std::size_t>(library)).identifier;
                  login->settings.library_title = login->libraries.at(static_cast<std::size_t>(library)).title;
                  update_connection_settings(login->settings); }
                EndDialog(dialog, IDOK); return TRUE;
            }
        }
        if ((message == WM_COMMAND && LOWORD(parameter) == IDCANCEL) || message == WM_CLOSE) {
            EndDialog(dialog, IDCANCEL); return TRUE;
        }
        if (message == WM_DESTROY) {
            SetWindowLongPtrW(dialog, DWLP_USER, 0); delete login; return TRUE;
        }
    } catch (...) {
        SetDlgItemTextW(dialog, IDC_STATUS, localized(L"Plex-Anmeldung konnte nicht verarbeitet werden.", L"Could not process Plex sign-in."));
        return TRUE;
    }
    return FALSE;
}
}

void show_login(HWND parent) noexcept {
    DialogBoxParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_LOGIN), parent, login_dialog, 0);
}
}
