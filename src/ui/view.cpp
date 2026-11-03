#include "plugin/plugin.h"
#include "resource.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace plex::plugin {
namespace {
std::vector<HWND> views;

const wchar_t* text(const wchar_t* german_text, const wchar_t* english_text) noexcept {
    return german() ? german_text : english_text;
}

std::wstring input(HWND dialog, int identifier) {
    const auto control = GetDlgItem(dialog, identifier);
    const auto length = GetWindowTextLengthW(control);
    if (length > 4096) throw std::invalid_argument("input_limit");
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

std::string ascii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character < 32 || character > 126) throw std::invalid_argument("ascii_input");
        result += static_cast<char>(character);
    }
    return result;
}

void localize_view(HWND dialog) noexcept {
    SetDlgItemTextW(dialog, IDC_LANGUAGE_LABEL, text(L"Sprache", L"Language"));
    SetDlgItemTextW(dialog, IDC_LOGIN, text(L"Mit Plex anmelden...", L"Sign in with Plex..."));
    SetDlgItemTextW(dialog, IDC_TEST_PLAYBACK, text(L"Wiedergabe testen...", L"Test playback..."));
    SetDlgItemTextW(dialog, IDC_STOP_CONNECTION, text(L"Verbindung beenden", L"End connection"));
}

void update_status(HWND dialog) noexcept {
    if (!playback_active()) {
        SetDlgItemTextW(dialog, IDC_STATUS, text(L"Kein Wiedergabetest gestartet.", L"No playback test started."));
        EnableWindow(GetDlgItem(dialog, IDC_STOP_CONNECTION), FALSE);
        return;
    }
    EnableWindow(GetDlgItem(dialog, IDC_STOP_CONNECTION), TRUE);
    const auto status = playback_status();
    std::array<wchar_t, 256> message{};
    if (status.upstream_status == 401 || status.upstream_status == 403)
        wcscpy_s(message.data(), message.size(), text(L"Zugriff verweigert. Plex-Token prüfen.", L"Access denied. Check the Plex token."));
    else if (status.upstream_status == 404)
        wcscpy_s(message.data(), message.size(), text(L"Mediendatei nicht gefunden.", L"Media file not found."));
    else if (status.transport_error)
        swprintf_s(message.data(), message.size(), text(L"Verbindungsfehler (Windows-Code %lu).", L"Connection error (Windows code %lu)."), status.transport_error);
    else if (status.upstream_status >= 300)
        swprintf_s(message.data(), message.size(), text(L"Plex-Anfrage abgelehnt (HTTP %lu).", L"Plex request rejected (HTTP %lu)."), status.upstream_status);
    else if (status.upstream_status)
        swprintf_s(message.data(), message.size(), text(L"HTTP %lu | %llu Bytes übertragen", L"HTTP %lu | %llu bytes transferred"), status.upstream_status, status.bytes_sent);
    else wcscpy_s(message.data(), message.size(), text(L"Warte auf Winamps Streaming-Anfrage.", L"Waiting for Winamp's streaming request."));
    SetDlgItemTextW(dialog, IDC_STATUS, message.data());
}

INT_PTR CALLBACK test_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            SetWindowTextW(dialog, text(L"Wiedergabetest", L"Playback test"));
            const std::array<std::pair<int, const wchar_t*>, 7> labels{{
                {IDC_SERVER_LABEL, text(L"Serveradresse (HTTPS)", L"Server address (HTTPS)")},
                {IDC_TOKEN_LABEL, L"Plex-Token"},
                {IDC_MEDIA_LABEL, text(L"Medienpfad (/library/parts/...)", L"Media path (/library/parts/...)")},
                {IDC_TITLE_LABEL, text(L"Titel", L"Title")},
                {IDC_DURATION_LABEL, text(L"Laufzeit (Sekunden)", L"Duration (seconds)")},
                {IDOK, text(L"Jetzt abspielen", L"Play now")},
                {IDCANCEL, text(L"Abbrechen", L"Cancel")}}};
            for (const auto& label : labels) SetDlgItemTextW(dialog, label.first, label.second);
            for (const auto identifier : {IDC_SERVER, IDC_TOKEN, IDC_MEDIA_PATH, IDC_TRACK_TITLE})
                SendDlgItemMessageW(dialog, identifier, EM_SETLIMITTEXT, identifier == IDC_TOKEN ? 4096 : 512, 0);
            SendDlgItemMessageW(dialog, IDC_DURATION, EM_SETLIMITTEXT, 7, 0);
            SetDlgItemTextW(dialog, IDC_DURATION, L"0");
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(parameter) == IDOK) {
            try {
                PlaybackSource source;
                source.server = parse_server_address(ascii(input(dialog, IDC_SERVER)));
                source.media_path = ascii(input(dialog, IDC_MEDIA_PATH));
                media_extension(source.media_path);
                auto secret = input(dialog, IDC_TOKEN);
                try { source.token = ascii(secret); }
                catch (...) { SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t)); throw; }
                SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));
                validate_token(source.token);
                const auto title = input(dialog, IDC_TRACK_TITLE);
                BOOL translated = FALSE;
                auto seconds = GetDlgItemInt(dialog, IDC_DURATION, &translated, FALSE);
                if (!translated || seconds > 9999999 || title.empty()) throw std::invalid_argument("track_metadata");
                start_playback(std::move(source), title, seconds == 0 ? -1 : static_cast<int>(seconds));
                SetDlgItemTextW(dialog, IDC_TOKEN, L"");
                EndDialog(dialog, IDOK);
            } catch (const std::invalid_argument&) {
                SetDlgItemTextW(dialog, IDC_RESULT, text(
                    L"Eingaben prüfen: gültige HTTPS-Adresse, Token, unterstützter Medienpfad und Titel sind erforderlich. HTTP ist nur auf Loopback erlaubt.",
                    L"Check inputs: a valid HTTPS address, token, supported media path and title are required. HTTP is allowed on loopback only."));
            } catch (...) {
                SetDlgItemTextW(dialog, IDC_RESULT, text(L"Wiedergabetest konnte nicht gestartet werden.", L"Could not start the playback test."));
            }
            return TRUE;
        }
        if ((message == WM_COMMAND && LOWORD(parameter) == IDCANCEL) || message == WM_CLOSE) {
            SetDlgItemTextW(dialog, IDC_TOKEN, L"");
            EndDialog(dialog, IDCANCEL); return TRUE;
        }
    } catch (...) { return FALSE; }
    return FALSE;
}

INT_PTR CALLBACK view_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            views.push_back(dialog);
            SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Deutsch"));
            SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
            SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_SETCURSEL, german() ? 0 : 1, 0);
            localize_view(dialog); update_status(dialog); SetTimer(dialog, 1, 500, nullptr);
            return TRUE;
        }
        if (message == WM_SIZE) {
            const int width = LOWORD(detail);
            RECT units{0, 0, 10, 10}; MapDialogRect(dialog, &units);
            const int padding = units.right;
            const auto language = GetDlgItem(dialog, IDC_LANGUAGE);
            RECT bounds{}; GetWindowRect(language, &bounds);
            POINT point{bounds.left, bounds.top}; ScreenToClient(dialog, &point);
            const int combo_width = bounds.right - bounds.left;
            MoveWindow(language, (std::max)(padding, width - combo_width - padding), point.y, combo_width, bounds.bottom - bounds.top, TRUE);
            const auto label = GetDlgItem(dialog, IDC_LANGUAGE_LABEL);
            GetWindowRect(label, &bounds); point = {bounds.left, bounds.top}; ScreenToClient(dialog, &point);
            MoveWindow(label, (std::max)(padding, width - combo_width - padding - 65), point.y, 60, bounds.bottom - bounds.top, TRUE);
            const auto status = GetDlgItem(dialog, IDC_STATUS);
            GetWindowRect(status, &bounds); point = {bounds.left, bounds.top}; ScreenToClient(dialog, &point);
            MoveWindow(status, point.x, point.y, (std::max)(10, width - 2 * padding), bounds.bottom - bounds.top, TRUE);
            return TRUE;
        }
        if (message == WM_TIMER) { update_status(dialog); return TRUE; }
        if (message == WM_COMMAND) {
            if (LOWORD(parameter) == IDC_LOGIN) { show_login(dialog); update_status(dialog); return TRUE; }
            if (LOWORD(parameter) == IDC_TEST_PLAYBACK) { show_playback_test(dialog); update_status(dialog); return TRUE; }
            if (LOWORD(parameter) == IDC_STOP_CONNECTION) { stop_playback(); update_status(dialog); return TRUE; }
            if (LOWORD(parameter) == IDC_LANGUAGE && HIWORD(parameter) == CBN_SELCHANGE) {
                set_german(SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_GETCURSEL, 0, 0) == 0);
                localize_view(dialog); update_status(dialog); return TRUE;
            }
        }
        if (message == WM_DESTROY) {
            KillTimer(dialog, 1);
            views.erase(std::remove(views.begin(), views.end(), dialog), views.end());
            return TRUE;
        }
    } catch (...) { return FALSE; }
    return FALSE;
}
}

HWND create_view(HWND parent) noexcept {
    return CreateDialogParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_VIEW), parent, view_dialog, 0);
}

void show_playback_test(HWND parent) noexcept {
    DialogBoxParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLAYBACK_TEST), parent, test_dialog, 0);
}

void destroy_views() noexcept {
    while (!views.empty()) {
        const auto view = views.back();
        if (IsWindow(view)) DestroyWindow(view);
        else views.pop_back();
    }
}
}
