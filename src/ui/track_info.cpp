#include "plugin/plugin.h"
#include "plugin/track_info.h"
#include "resource.h"
#include "plex/artwork.h"
#include <objbase.h>
#include <commctrl.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace plex::plugin {
namespace {
// Rahmenlose EDITs erhalten exakt den Pinsel und die Textfarbe der benachbarten
// skinierten Beschriftung. Native Auswahl, Fokus und Kopieren bleiben erhalten.
LRESULT CALLBACK information_colors(HWND dialog, UINT message, WPARAM parameter, LPARAM detail, UINT_PTR identifier, DWORD_PTR) noexcept {
    if ((message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) && detail) {
        const auto control = GetDlgCtrlID(reinterpret_cast<HWND>(detail));
        if (control >= IDC_TRACK_INFO_VALUE && control <= IDC_TRACK_INFO_VALUE + 8)
            return SendMessageW(dialog, WM_CTLCOLORSTATIC, parameter, reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDC_TRACK_INFO_LABEL)));
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(dialog, information_colors, identifier);
    return DefSubclassProc(dialog, message, parameter, detail);
}
struct InformationPanel {
    const TrackInformation& information;
    HBITMAP bitmap = nullptr;
    HFONT title_font = nullptr;
    unsigned width = 0, height = 0;
    bool artwork_error = false;
    ~InformationPanel() { if (bitmap) DeleteObject(bitmap); if (title_font) DeleteObject(title_font); }

    // Nur das kanonische, bereits gebundene PNG dekodieren. Die Vorschau führt
    // keinen Netzwerkzugriff aus und teilt die Bilddaten mit Winamps Coverdienst.
    void prepare_artwork() noexcept {
        if (!information.artwork) return;
        const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        try {
            if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) throw std::runtime_error("artwork_com");
            const auto pixels = decode_artwork(*information.artwork, 256);
            BITMAPINFO description{};
            description.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            description.bmiHeader.biWidth = pixels.width;
            description.bmiHeader.biHeight = -static_cast<LONG>(pixels.height);
            description.bmiHeader.biPlanes = 1;
            description.bmiHeader.biBitCount = 32;
            description.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            bitmap = CreateDIBSection(nullptr, &description, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap || !bits) throw std::runtime_error("artwork_bitmap");
            std::memcpy(bits, pixels.bgra.data(), pixels.bgra.size());
            width = pixels.width; height = pixels.height;
        } catch (...) {
            if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
            artwork_error = true;
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
};

// Den Skin-Pinsel und die Textfarbe des nativen STATIC verwenden. Alpha und
// Seitenverhältnis bleiben erhalten; für fehlende Bilder gibt es einen Leerzustand.
void draw_artwork(HWND dialog, const DRAWITEMSTRUCT& item, const InformationPanel& panel) {
    const int saved = SaveDC(item.hDC);
    if (!saved) throw std::runtime_error("artwork_dc");
    const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(dialog, WM_CTLCOLORSTATIC,
        reinterpret_cast<WPARAM>(item.hDC), reinterpret_cast<LPARAM>(item.hwndItem)));
    FillRect(item.hDC, &item.rcItem, brush);
    if (panel.bitmap) {
        const auto memory = CreateCompatibleDC(item.hDC);
        if (!memory) { RestoreDC(item.hDC, saved); throw std::runtime_error("artwork_dc"); }
        const auto previous = SelectObject(memory, panel.bitmap);
        const auto scale = (std::min)(static_cast<double>(item.rcItem.right - item.rcItem.left) / panel.width,
                                     static_cast<double>(item.rcItem.bottom - item.rcItem.top) / panel.height);
        const auto width = (std::max)(1, static_cast<int>(panel.width * scale));
        const auto height = (std::max)(1, static_cast<int>(panel.height * scale));
        const auto left = item.rcItem.left + (item.rcItem.right - item.rcItem.left - width) / 2;
        const auto top = item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - height) / 2;
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const bool painted = AlphaBlend(item.hDC, left, top, width, height, memory, 0, 0, panel.width, panel.height, blend) != FALSE;
        SelectObject(memory, previous); DeleteDC(memory);
        RestoreDC(item.hDC, saved);
        if (!painted) throw std::runtime_error("artwork_paint");
    } else {
        const auto font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
        if (font) SelectObject(item.hDC, font);
        SetBkMode(item.hDC, TRANSPARENT);
        const auto message = panel.artwork_error
            ? (german() ? L"Albumcover konnte nicht geladen werden." : L"Could not load album artwork.")
            : (german() ? L"Kein Albumcover" : L"No album artwork");
        RECT text = item.rcItem;
        DrawTextW(item.hDC, message, -1, &text, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        OffsetRect(&text, 0, ((item.rcItem.bottom - item.rcItem.top) - (text.bottom - text.top)) / 2);
        text.right = item.rcItem.right;
        DrawTextW(item.hDC, message, -1, &text, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        RestoreDC(item.hDC, saved);
    }
}
// Der Dialog bekommt eine Kopie der erlaubten Musikfelder. Decoderzustand,
// Dateinamen und Verbindungsobjekte werden weder gelesen noch angezeigt.
INT_PTR CALLBACK information_dialog(HWND dialog, UINT event, WPARAM parameter, LPARAM context) noexcept {
    try {
        if (event == WM_INITDIALOG) {
            auto& panel = *reinterpret_cast<InformationPanel*>(context);
            SetWindowLongPtrW(dialog, DWLP_USER, context);
            const auto& information = panel.information;
            const std::array<const wchar_t*, 9> labels = german()
                ? std::array<const wchar_t*, 9>{L"Titel", L"Künstler", L"Album", L"Albumkünstler", L"Genre", L"Jahr", L"Titelnummer", L"Laufzeit", L"Audioformat"}
                : std::array<const wchar_t*, 9>{L"Title", L"Artist", L"Album", L"Album artist", L"Genre", L"Year", L"Track number", L"Duration", L"Audio format"};
            const std::array<const std::wstring*, 9> values{&information.title, &information.artist, &information.album,
                &information.album_artist, &information.genre, &information.year, &information.track, &information.duration, &information.format};
            for (std::size_t index = 0; index < labels.size(); ++index) {
                SetDlgItemTextW(dialog, IDC_TRACK_INFO_LABEL + static_cast<int>(index), labels[index]);
                SetDlgItemTextW(dialog, IDC_TRACK_INFO_VALUE + static_cast<int>(index), values[index]->empty() ? L"\u2014" : values[index]->c_str());
                const auto field = GetDlgItem(dialog, IDC_TRACK_INFO_VALUE + static_cast<int>(index));
                SetWindowLongPtrW(field, GWL_STYLE, GetWindowLongPtrW(field, GWL_STYLE) & ~WS_BORDER);
                SetWindowLongPtrW(field, GWL_EXSTYLE, GetWindowLongPtrW(field, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
                SetWindowPos(field, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            SetDlgItemTextW(dialog, IDOK, german() ? L"Schließen" : L"Close");
            skin_library_dialog(dialog);
            if (!SetWindowSubclass(dialog, information_colors, 1, 0)) throw std::runtime_error("information_skin_colors");
            LOGFONTW font{};
            const auto title = GetDlgItem(dialog, IDC_TRACK_INFO_VALUE);
            const auto original = reinterpret_cast<HFONT>(SendMessageW(title, WM_GETFONT, 0, 0));
            if (!GetObjectW(original, sizeof(font), &font)) throw std::runtime_error("information_font");
            font.lfWeight = FW_SEMIBOLD;
            panel.title_font = CreateFontIndirectW(&font);
            if (!panel.title_font) throw std::runtime_error("information_font");
            SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(panel.title_font), TRUE);
            const auto cover = GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK);
            const auto status = panel.bitmap ? 1 : panel.artwork_error ? 3 : 2;
            if (!SetPropW(cover, L"WinampPlexArtworkState", reinterpret_cast<HANDLE>(status))) throw std::runtime_error("artwork_state");
            SetWindowTextW(cover, panel.bitmap ? (german() ? L"Albumcover" : L"Album artwork") :
                panel.artwork_error ? (german() ? L"Albumcover konnte nicht geladen werden." : L"Could not load album artwork.") :
                (german() ? L"Kein Albumcover" : L"No album artwork"));
            return TRUE;
        }
        if (event == WM_DRAWITEM && parameter == IDC_TRACK_INFO_ARTWORK) {
            const auto panel = reinterpret_cast<const InformationPanel*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (panel) draw_artwork(dialog, *reinterpret_cast<const DRAWITEMSTRUCT*>(context), *panel);
            return TRUE;
        }
        if (event == WM_DESTROY) {
            RemovePropW(GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK), L"WinampPlexArtworkState");
        }
        if (event == WM_COMMAND && (LOWORD(parameter) == IDOK || LOWORD(parameter) == IDCANCEL)) {
            EndDialog(GetParent(dialog), LOWORD(parameter)); return TRUE;
        }
    } catch (...) { EndDialog(GetParent(dialog), IDCANCEL); return TRUE; }
    return FALSE;
}
}
bool show_track_information(HWND parent, const TrackInformation& information) {
    InformationPanel panel{information};
    panel.prepare_artwork();
    return show_library_panel(parent, IDD_PLEX_TRACK_INFO, information_dialog, reinterpret_cast<LPARAM>(&panel),
                              german() ? L"Plex-Titelinformationen" : L"Plex track information");
}
}
