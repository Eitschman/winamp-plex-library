#include "plugin/plugin.h"
#include "resource.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace {
bool german_host = false, active = false, fail_message = false;
plex::ProxyStatus proxy_status;
plex::ReporterStatus reporter_status;
winampMediaLibraryPlugin test_descriptor{};

void check(bool condition, const char* stage) {
    if (!condition) throw std::runtime_error(stage);
}
std::wstring status(HWND view) {
    wchar_t value[2048]{};
    GetDlgItemTextW(view, IDC_STATUS, value, 2048);
    return value;
}
void tick(HWND view) { SendMessageW(view, WM_TIMER, 1, 0); }
const wchar_t* localized(const wchar_t* german, const wchar_t* english) {
    return german_host ? german : english;
}

// Die echte Dialogprozedur verarbeitet kontrollierte Transportzustände. Hier
// werden weder ein Decoderfehler noch eine echte Serverantwort vorgetäuscht.
void verify_status(HWND parent) {
    proxy_status = {}; reporter_status = {}; active = false;
    const auto first = plex::plugin::create_view(parent);
    const auto second = plex::plugin::create_view(parent, plex::BrowseKind::tracks);
    check(IsWindow(first) && IsWindow(second), "native_views_created");
    SetDlgItemTextW(first, IDC_STATUS, L"Katalog A");
    SetDlgItemTextW(second, IDC_STATUS, L"Katalog B");
    tick(first); tick(second);
    check(status(first) == L"Katalog A" && status(second) == L"Katalog B", "independent_catalog_status");
    active = true;
    const auto denied = localized(L"Zugriff verweigert. Erneut mit Plex anmelden.", L"Access denied. Sign in with Plex again.");
    for (const auto code : {401UL, 403UL}) {
        proxy_status.upstream_status = code;
        tick(first); tick(second);
        check(status(first) == denied && status(second) == denied, "authentication_error_visible");
        proxy_status = {}; tick(first); tick(second);
        check(status(first) == L"Katalog A" && status(second) == L"Katalog B", "catalog_status_restored");
    }
    proxy_status.upstream_status = 404; tick(first);
    check(status(first) == localized(L"Mediendatei nicht gefunden.", L"Media file not found."), "missing_media_visible");
    SetDlgItemTextW(first, IDC_STATUS, L"Aktualisierter Katalog");
    tick(first);
    check(status(first) == localized(L"Mediendatei nicht gefunden.", L"Media file not found."), "error_still_overlays_new_catalog");
    proxy_status = {}; tick(first);
    check(status(first) == L"Aktualisierter Katalog", "new_catalog_preserved_after_recovery");

    proxy_status.transport_error = 12002; tick(first);
    check(status(first) == localized(L"Verbindungsfehler (Windows-Code 12002).", L"Connection error (Windows code 12002)."), "timeout_visible");
    SetDlgItemTextW(first, IDC_STATUS, L"Neue Ladeantwort");
    proxy_status = {}; tick(first);
    check(status(first) == L"Neue Ladeantwort", "fresh_catalog_not_overwritten_during_recovery");

    proxy_status.upstream_status = 302; tick(first);
    check(status(first) == localized(L"Plex-Anfrage abgelehnt (HTTP 302).", L"Plex request rejected (HTTP 302)."), "redirect_error_visible");
    proxy_status.transport_error = 12002;
    proxy_status.local_rejection = "request_host";
    tick(first);
    check(status(first) == localized(L"Lokale Wiedergabe abgewiesen. Titel erneut aus Plex hinzufügen.", L"Local playback rejected. Add the track from Plex again."), "local_rejection_priority");
    proxy_status.local_rejection = "request_expired";
    tick(first);
    check(status(first) == localized(L"Wiedergabelink abgelaufen. Titel erneut aus Plex hinzufügen.", L"Playback link expired. Add the track from Plex again."), "expired_playback_link_visible");
    proxy_status = {}; tick(first);
    check(status(first) == L"Neue Ladeantwort", "expired_link_overlay_clears_after_recovery");
    active = false; tick(first);
    check(status(first) == L"Neue Ladeantwort", "inactive_playback_does_not_retain_proxy_error");

    reporter_status.error = 1234; tick(first);
    check(status(first) == localized(L"Plex-Wiedergabestatus konnte nicht gemeldet werden (Code 1234).", L"Plex playback status could not be reported (code 1234)."), "reporter_error_visible");
    reporter_status = {}; tick(first);
    check(status(first) == L"Neue Ladeantwort", "reporter_recovery_restores_catalog");
    check(status(second) == L"Katalog B", "other_view_status_not_overwritten");

    fail_message = true;
    SendMessageW(first, WM_APP + 90, 0, 0);
    check(status(first) == localized(L"Der Vorgang konnte nicht ausgeführt werden.", L"The operation could not be completed."), "exception_stays_inside_dialog_boundary");
    DestroyWindow(first);
    plex::plugin::destroy_views();
    check(!IsWindow(second), "remaining_view_destroyed");
}
}

namespace plex::plugin {
winampMediaLibraryPlugin& descriptor() noexcept { return test_descriptor; }
bool german() noexcept { return german_host; }
bool playback_active() noexcept { return active; }
ProxyStatus playback_status() noexcept { return proxy_status; }
ReporterStatus playback_reporting_status() noexcept { return reporter_status; }
INT_PTR library_view_message(HWND, UINT, WPARAM, LPARAM) {
    if (fail_message) { fail_message = false; throw std::runtime_error("synthetic_catalog_failure"); }
    return FALSE;
}
}

int main() {
    HWND parent = nullptr;
    try {
        const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        check(InitCommonControlsEx(&controls) != FALSE, "initialize_controls");
        test_descriptor.hDllInstance = GetModuleHandleW(nullptr);
        parent = CreateWindowW(L"STATIC", L"Plex status test", WS_OVERLAPPED, 0, 0, 500, 400,
                               nullptr, nullptr, test_descriptor.hDllInstance, nullptr);
        check(parent != nullptr, "hidden_parent_created");
        for (const auto language : {false, true}) { german_host = language; verify_status(parent); }
        DestroyWindow(parent);
        std::cout << "Native status overlays, priorities, recovery, independent views and ABI exception boundary checked in both languages.\n";
        return 0;
    } catch (const std::exception& failure) {
        plex::plugin::destroy_views();
        if (parent) DestroyWindow(parent);
        std::cerr << "FAILED: " << failure.what() << '\n';
        return 1;
    }
}
