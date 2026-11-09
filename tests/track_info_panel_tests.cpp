#include "plugin/plugin.h"
#include "plugin/track_info.h"
#include "plex/artwork.h"
#include "resource.h"
#include <commctrl.h>
#include <objbase.h>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
HBRUSH skin_brush = nullptr;
COLORREF skin_color = RGB(40, 50, 60);
std::function<void(HWND)> inspect;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
LRESULT CALLBACK skin(HWND window, UINT message, WPARAM parameter, LPARAM detail, UINT_PTR, DWORD_PTR) {
    if (message == WM_CTLCOLORSTATIC) {
        SetTextColor(reinterpret_cast<HDC>(parameter), RGB(230, 240, 250));
        SetBkColor(reinterpret_cast<HDC>(parameter), skin_color);
        return reinterpret_cast<LRESULT>(skin_brush);
    }
    return DefSubclassProc(window, message, parameter, detail);
}
// Der Test zeichnet den tatsächlichen Owner-Draw-Handler in eine eigene Bitmap.
// Farbwechsel und ein halbtransparentes, nicht quadratisches Bild erkennen
// feste Hintergrundfarben, verzerrte Bilder und verlorene Alphakanäle.
void check_picture(HWND dialog, bool has_picture) {
    const auto cover = GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK);
    RECT bounds{}; GetClientRect(cover, &bounds);
    check(bounds.right > 100 && bounds.bottom > 100, "usable_cover_area");
    const auto screen = GetDC(dialog);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, bounds.right, bounds.bottom);
    ReleaseDC(dialog, screen);
    check(dc && bitmap, "capture_resources");
    const auto previous = SelectObject(dc, bitmap);
    DRAWITEMSTRUCT item{}; item.CtlType = ODT_STATIC; item.CtlID = IDC_TRACK_INFO_ARTWORK;
    item.itemAction = ODA_DRAWENTIRE; item.hwndItem = cover; item.hDC = dc; item.rcItem = bounds;
    SendMessageW(dialog, WM_DRAWITEM, IDC_TRACK_INFO_ARTWORK, reinterpret_cast<LPARAM>(&item));
    const auto margin = GetPixel(dc, bounds.right / 2, 2);
    const auto left = GetPixel(dc, 2, bounds.bottom / 2);
    const auto right = GetPixel(dc, bounds.right - 3, bounds.bottom / 2);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    check(margin == skin_color, "letterbox_uses_current_skin_background");
    if (has_picture) {
        check(GetRValue(left) > 245 && GetGValue(left) < 10 && GetBValue(left) < 10, "cover_visible_without_distortion");
        check(GetRValue(right) >= 15 && GetRValue(right) <= 25 && GetGValue(right) >= 148 && GetGValue(right) <= 158 &&
              GetBValue(right) >= 25 && GetBValue(right) <= 35, "cover_alpha_blends_with_skin");
    }
}
}
namespace plex::plugin {
bool german() noexcept { return false; }
void skin_library_dialog(HWND dialog) {
    check(SetWindowSubclass(dialog, skin, 1, 0) != FALSE, "skin_parent_registered");
    for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
}
bool show_library_panel(HWND parent, int resource, DLGPROC procedure, LPARAM context, const wchar_t*) {
    const auto dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), parent, procedure, context);
    check(dialog != nullptr, "create_real_panel_resource");
    try { inspect(dialog); }
    catch (...) { DestroyWindow(dialog); throw; }
    DestroyWindow(dialog); return true;
}
}
int main() {
    using namespace plex::plugin;
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) return 2;
    HWND parent = nullptr;
    try {
        skin_brush = CreateSolidBrush(skin_color);
        parent = CreateWindowExW(0, L"STATIC", L"Test host", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(parent && skin_brush, "create_host");
        TrackInformation information{L"Grüße 東京", L"Artist", L"Album", L"Album artist", L"Metal", L"2026", L"4", L"3:29", L"mp3"};
        information.artwork = std::make_shared<const std::vector<std::uint8_t>>(
            plex::encode_artwork_png({2, 1, {0, 0, 255, 255, 0, 128, 0, 128}}));
        inspect = [&](HWND dialog) {
            check(GetPropW(GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK), L"WinampPlexArtworkState") == reinterpret_cast<HANDLE>(1), "cached_cover_ready");
            for (int index = 0; index < 9; ++index) {
                const auto field = GetDlgItem(dialog, IDC_TRACK_INFO_VALUE + index);
                check((GetWindowLongPtrW(field, GWL_STYLE) & (ES_READONLY | WS_BORDER)) == ES_READONLY, "copyable_borderless_read_only_field");
                check(!(GetWindowLongPtrW(field, GWL_EXSTYLE) & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)), "no_native_client_edge");
                const auto dc = GetDC(field);
                const auto brush = SendMessageW(dialog, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(field));
                const auto background = GetBkColor(dc), foreground = GetTextColor(dc);
                ReleaseDC(field, dc);
                check(brush == reinterpret_cast<LRESULT>(skin_brush) && background == skin_color && foreground == RGB(230, 240, 250),
                      "read_only_field_uses_skin_static_colors");
            }
            const auto title = GetDlgItem(dialog, IDC_TRACK_INFO_VALUE);
            SendMessageW(title, EM_SETSEL, 1, 4);
            DWORD first = 0, last = 0;
            SendMessageW(title, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            check(first == 1 && last == 4, "native_text_selection_retained");
            SendMessageW(title, WM_CHAR, L'X', 0);
            wchar_t text[64]{}; GetWindowTextW(title, text, 64);
            check(text == information.title, "typing_cannot_change_music_information");
            check_picture(dialog, true);
        };
        check(show_track_information(parent, information), "covered_panel");
        information.artwork.reset();
        inspect = [&](HWND dialog) {
            check(GetPropW(GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK), L"WinampPlexArtworkState") == reinterpret_cast<HANDLE>(2), "missing_cover_explicit");
            check_picture(dialog, false);
        };
        check(show_track_information(parent, information), "missing_panel");
        information.artwork = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3});
        inspect = [&](HWND dialog) {
            check(GetPropW(GetDlgItem(dialog, IDC_TRACK_INFO_ARTWORK), L"WinampPlexArtworkState") == reinterpret_cast<HANDLE>(3), "corrupt_cover_explicit_error");
            wchar_t title[64]{}; GetDlgItemTextW(dialog, IDC_TRACK_INFO_VALUE, title, 64);
            check(title == information.title, "corrupt_cover_preserves_music_information");
        };
        check(show_track_information(parent, information), "corrupt_panel");
        // Die Hintergrundfarbe darf auch beim nächsten Panel nicht fest eingebaut sein.
        DeleteObject(skin_brush); skin_color = RGB(70, 80, 90); skin_brush = CreateSolidBrush(skin_color);
        information.artwork.reset();
        inspect = [&](HWND dialog) { check_picture(dialog, false); };
        check(show_track_information(parent, information), "changed_skin_panel");
        DestroyWindow(parent); DeleteObject(skin_brush); CoUninitialize();
        std::cout << checks << " track-information panel checks passed\n"; return 0;
    } catch (const std::exception& error) {
        if (parent) DestroyWindow(parent); if (skin_brush) DeleteObject(skin_brush); CoUninitialize();
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
