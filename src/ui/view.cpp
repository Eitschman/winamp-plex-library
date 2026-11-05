#include "plugin/plugin.h"
#include "resource.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <vector>
#include <string_view>

namespace plex::plugin {
namespace {
struct ViewStatus {
    HWND window;
    std::wstring underlying, overlay;
};
std::vector<ViewStatus> views;

const wchar_t* text(const wchar_t* german_text, const wchar_t* english_text) noexcept {
    return german() ? german_text : english_text;
}

void update_status(HWND dialog) {
    const auto found = std::find_if(views.begin(), views.end(), [=](const auto& view) { return view.window == dialog; });
    if (found == views.end()) return;
    const auto status = playback_active() ? playback_status() : ProxyStatus{};
    const auto reporting = playback_reporting_status();
    std::array<wchar_t, 256> message{};
    if (status.local_rejection && std::string_view(status.local_rejection) == "request_expired")
        wcscpy_s(message.data(), message.size(), text(L"Wiedergabelink abgelaufen. Titel erneut aus Plex hinzufügen.", L"Playback link expired. Add the track from Plex again."));
    else if (status.local_rejection)
        wcscpy_s(message.data(), message.size(), text(L"Lokale Wiedergabe abgewiesen. Titel erneut aus Plex hinzufügen.", L"Local playback rejected. Add the track from Plex again."));
    else if (status.upstream_status == 401 || status.upstream_status == 403)
        wcscpy_s(message.data(), message.size(), text(L"Zugriff verweigert. Erneut mit Plex anmelden.", L"Access denied. Sign in with Plex again."));
    else if (status.upstream_status == 404)
        wcscpy_s(message.data(), message.size(), text(L"Mediendatei nicht gefunden.", L"Media file not found."));
    else if (status.transport_error)
        swprintf_s(message.data(), message.size(), text(L"Verbindungsfehler (Windows-Code %lu).", L"Connection error (Windows code %lu)."), status.transport_error);
    else if (status.upstream_status >= 300)
        swprintf_s(message.data(), message.size(), text(L"Plex-Anfrage abgelehnt (HTTP %lu).", L"Plex request rejected (HTTP %lu)."), status.upstream_status);
    else if (reporting.error)
        swprintf_s(message.data(), message.size(), text(L"Plex-Wiedergabestatus konnte nicht gemeldet werden (Code %lu).", L"Plex playback status could not be reported (code %lu)."), reporting.error);
    // Wiedergabefehler überlagern den Katalogstatus nur vorübergehend.
    // Neuere Lade- oder Katalogmeldungen dürfen beim Erholen nicht verloren gehen.
    std::array<wchar_t, 2048> current{};
    GetDlgItemTextW(dialog, IDC_STATUS, current.data(), static_cast<int>(current.size()));
    if (found->overlay.empty() || found->overlay != current.data()) found->underlying = current.data();
    if (*message.data()) SetDlgItemTextW(dialog, IDC_STATUS, message.data());
    else if (!found->overlay.empty() && found->overlay == current.data()) SetDlgItemTextW(dialog, IDC_STATUS, found->underlying.c_str());
    found->overlay = message.data();
}

INT_PTR CALLBACK view_dialog(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            views.push_back({dialog, {}, {}});
            SetTimer(dialog, 1, 500, nullptr);
        }
        if (message == WM_TIMER && parameter == 1) { update_status(dialog); return TRUE; }
        if (message == WM_DESTROY) {
            KillTimer(dialog, 1);
            views.erase(std::remove_if(views.begin(), views.end(), [=](const auto& view) { return view.window == dialog; }), views.end());
        }
        return library_view_message(dialog, message, parameter, detail);
    } catch (...) {
        SetDlgItemTextW(dialog, IDC_STATUS, text(L"Der Vorgang konnte nicht ausgeführt werden.", L"The operation could not be completed."));
        return TRUE;
    }
}
}

HWND create_view(HWND parent, BrowseKind kind) noexcept {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    return CreateDialogParamW(descriptor().hDllInstance, MAKEINTRESOURCEW(IDD_PLEX_VIEW), parent, view_dialog, static_cast<LPARAM>(kind));
}

void destroy_views() noexcept {
    while (!views.empty()) {
        const auto view = views.back().window;
        if (IsWindow(view)) DestroyWindow(view);
        else views.pop_back();
    }
}
}
