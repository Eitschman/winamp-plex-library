#include "plugin/plugin.h"
#include "plugin/track_info.h"
#include "resource.h"
#include <array>

namespace plex::plugin {
namespace {
// Der Dialog bekommt eine Kopie der erlaubten Musikfelder. Decoderzustand,
// Dateinamen und Verbindungsobjekte werden weder gelesen noch angezeigt.
INT_PTR CALLBACK information_dialog(HWND dialog, UINT event, WPARAM parameter, LPARAM context) noexcept {
    try {
        if (event == WM_INITDIALOG) {
            const auto& information = *reinterpret_cast<const TrackInformation*>(context);
            const std::array<const wchar_t*, 9> labels = german()
                ? std::array<const wchar_t*, 9>{L"Titel", L"Künstler", L"Album", L"Albumkünstler", L"Genre", L"Jahr", L"Titelnummer", L"Laufzeit", L"Audioformat"}
                : std::array<const wchar_t*, 9>{L"Title", L"Artist", L"Album", L"Album artist", L"Genre", L"Year", L"Track number", L"Duration", L"Audio format"};
            const std::array<const std::wstring*, 9> values{&information.title, &information.artist, &information.album,
                &information.album_artist, &information.genre, &information.year, &information.track, &information.duration, &information.format};
            for (std::size_t index = 0; index < labels.size(); ++index) {
                SetDlgItemTextW(dialog, IDC_TRACK_INFO_LABEL + static_cast<int>(index), labels[index]);
                SetDlgItemTextW(dialog, IDC_TRACK_INFO_VALUE + static_cast<int>(index), values[index]->empty() ? L"\u2014" : values[index]->c_str());
            }
            SetDlgItemTextW(dialog, IDOK, german() ? L"Schließen" : L"Close");
            skin_library_dialog(dialog); return TRUE;
        }
        if (event == WM_COMMAND && (LOWORD(parameter) == IDOK || LOWORD(parameter) == IDCANCEL)) {
            EndDialog(GetParent(dialog), LOWORD(parameter)); return TRUE;
        }
    } catch (...) { EndDialog(GetParent(dialog), IDCANCEL); return TRUE; }
    return FALSE;
}
}
bool show_track_information(HWND parent, const TrackInformation& information) {
    return show_library_panel(parent, IDD_PLEX_TRACK_INFO, information_dialog, reinterpret_cast<LPARAM>(&information),
                              german() ? L"Plex-Titelinformationen" : L"Plex track information");
}
}
