#include "plugin/track_info.h"
#include <commctrl.h>
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <gen_tray/WINAMPCMD.H>
#include <stdexcept>
#include <vector>

namespace plex::plugin {
namespace {
// Verifizierter Winamp-Core-Befehl ID_PE_ID3 aus resource.h; kein Plugin-SDK-Makro.
// Native Zielversion und gemischte Auswahl werden zusätzlich zur Laufzeit geprüft.
constexpr unsigned playlist_information_command = 40208;
std::wstring wide_filename(const char* name) {
    if (!name || !*name) return {};
    const int length = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    if (length <= 0) throw std::runtime_error("information_encoding");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, result.data(), length)) throw std::runtime_error("information_encoding");
    result.pop_back(); return result;
}
}
struct TrackInformationController::State {
    HWND player, editor;
    Lookup lookup;
    Show show;
    State(HWND window, Lookup find, Show display) : player(window),
        editor(reinterpret_cast<HWND>(SendMessageW(window, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND))),
        lookup(std::move(find)), show(std::move(display)) {
        if (!IsWindow(player) || !lookup || !show) throw std::invalid_argument("information_controller");
        if (!SetWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("information_subclass");
        if (IsWindow(editor) && editor != player &&
            !SetWindowSubclass(editor, procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) {
            RemoveWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this));
            throw std::runtime_error("information_editor_subclass");
        }
    }
    ~State() {
        RemoveWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this));
        if (IsWindow(editor) && editor != player) RemoveWindowSubclass(editor, procedure, reinterpret_cast<UINT_PTR>(this));
    }
    std::wstring playlist_name(int position) const {
        const auto name = reinterpret_cast<const wchar_t*>(SendMessageW(player, WM_WA_IPC, position, IPC_GETPLAYLISTFILEW));
        return name && reinterpret_cast<UINT_PTR>(name) > 1 ? name : L"";
    }
    LRESULT message(HWND window, UINT event, WPARAM parameter, LPARAM detail) {
        if (window == player && event == WM_WA_IPC && parameter && (detail == IPC_INFOBOXW || detail == IPC_INFOBOX)) {
            HWND parent = player;
            std::wstring filename;
            if (detail == IPC_INFOBOXW) {
                const auto& request = *reinterpret_cast<const infoBoxParamW*>(parameter);
                parent = request.parent; if (request.filename) filename = request.filename;
            } else {
                const auto& request = *reinterpret_cast<const infoBoxParam*>(parameter);
                parent = request.parent; filename = wide_filename(request.filename);
            }
            if (const auto information = lookup(filename)) return show(IsWindow(parent) ? parent : player, *information) ? 0 : 1;
        }
        if (window == player && event == WM_COMMAND && LOWORD(parameter) == WINAMP_EDIT_ID3) {
            const auto name = reinterpret_cast<const wchar_t*>(SendMessageW(player, WM_WA_IPC, 0, IPC_GET_PLAYING_FILENAME));
            if (name && reinterpret_cast<UINT_PTR>(name) > 1) {
                const std::wstring filename(name);
                if (const auto information = lookup(filename)) { show(player, *information); return 0; }
            }
        }
        if (window == editor && event == WM_COMMAND && LOWORD(parameter) == playlist_information_command) {
            std::vector<std::wstring> selection;
            bool owns = false;
            auto position = static_cast<int>(SendMessageW(editor, WM_USER, IPC_PE_GETNEXTSELECTED, -1));
            while (position >= 0 && selection.size() < 10000) {
                auto filename = playlist_name(position);
                owns = owns || lookup(filename).has_value();
                selection.push_back(std::move(filename));
                const auto next = static_cast<int>(SendMessageW(editor, WM_USER, IPC_PE_GETNEXTSELECTED, position));
                if (next >= 0 && next <= position) throw std::runtime_error("information_selection");
                position = next;
            }
            if (owns) {
                if (position >= 0) throw std::runtime_error("information_selection_limit");
                for (const auto& filename : selection) {
                    infoBoxParamW request{editor, filename.c_str()};
                    if (SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&request), IPC_INFOBOXW)) break;
                }
                return 0;
            }
        }
        return DefSubclassProc(window, event, parameter, detail);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT event, WPARAM parameter, LPARAM detail, UINT_PTR, DWORD_PTR context) noexcept {
        try { return reinterpret_cast<State*>(context)->message(window, event, parameter, detail); }
        catch (...) {
            MessageBoxW(window, L"Plex: Titelinformationen konnten nicht geöffnet werden. / Could not open track information.", L"Winamp Plex Library", MB_OK | MB_ICONERROR);
            return event == WM_WA_IPC ? 1 : 0;
        }
    }
};
TrackInformationController::TrackInformationController(HWND player, Lookup lookup, Show show)
    : state_(std::make_unique<State>(player, std::move(lookup), std::move(show))) {}
TrackInformationController::~TrackInformationController() = default;
}
