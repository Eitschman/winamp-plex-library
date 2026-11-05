#pragma once
#include "plex/winamp_playlist.h"
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <limits>
#include <stdexcept>

namespace playlist_checks {
struct Host {
    int length = 5, current = 1, added = 3, inserted = -1, mutations = 0, commands = 0;
    bool valid = true, editor_missing = false;
};
inline void require(bool value, const char* code) { if (!value) throw std::runtime_error(code); }
template<class Operation> void rejects(Operation operation, const char* code) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, code);
}
// Der kontrollierte Host prüft Nachrichten, Originalstrukturen und Fehlergrenzen.
// Er simuliert weder einen Decoder noch die native M3U8-Verarbeitung von Winamp.
inline LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams));
    const auto host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (host && message == WM_WA_IPC) {
        if (second == IPC_GETLISTLENGTH) return host->length;
        if (second == IPC_GETLISTPOS) return host->current;
        if (second == IPC_GETWND && first == IPC_GETWND_PE) return host->editor_missing ? 0 : reinterpret_cast<LRESULT>(window);
    }
    if (host && message == WM_COMMAND) ++host->commands;
    if (host && message == WM_COPYDATA) {
        const auto copy = reinterpret_cast<COPYDATASTRUCT*>(second);
        if (copy->dwData == IPC_PE_INSERTFILENAMEW && copy->cbData == sizeof(fileinfoW)) {
            const auto file = static_cast<const fileinfoW*>(copy->lpData);
            host->valid = host->valid && wcscmp(file->file, L"C:\\synthetic\\queue.m3u8") == 0;
            host->inserted = file->index;
        } else if (copy->dwData == IPC_ENQUEUEFILEW && copy->cbData == sizeof(L"C:\\synthetic\\queue.m3u8")) {
            host->valid = host->valid && wcscmp(static_cast<const wchar_t*>(copy->lpData), L"C:\\synthetic\\queue.m3u8") == 0;
            host->inserted = host->length;
        } else { host->valid = false; return 0; }
        host->length += host->added; ++host->mutations; return 1;
    }
    return DefWindowProcW(window, message, first, second);
}
inline void verify() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{}; type.lpfnWndProc = procedure; type.hInstance = instance; type.lpszClassName = L"PlexPlaylistBoundaryHost";
    require(RegisterClassW(&type) != 0, "playlist_host_class");
    Host host;
    const auto window = CreateWindowW(type.lpszClassName, L"", 0, 0, 0, 0, 0, nullptr, nullptr, instance, &host);
    require(window != nullptr, "playlist_host_window");
    struct Cleanup {
        HWND window; HINSTANCE instance;
        ~Cleanup() { DestroyWindow(window); UnregisterClassW(L"PlexPlaylistBoundaryHost", instance); }
    } cleanup{window, instance};
    const auto insert = [&](const std::wstring& path = L"C:\\synthetic\\queue.m3u8", std::size_t count = 3) {
        return plex::send_playlist_to_winamp(window, path, count, plex::PlaylistPlacement::after_current);
    };
    require(insert() == 2 && host.inserted == 2 && host.length == 8 && host.current == 1 && host.valid && !host.commands,
            "insert_after_current_without_playback_command");
    host = {}; host.length = 0; host.current = -1;
    require(insert() == 0 && host.inserted == 0 && host.length == 3, "insert_into_empty_playlist");
    host = {}; host.current = 4;
    require(insert() == 5 && host.inserted == 5 && host.length == 8, "insert_after_last_track");
    host = {};
    require(plex::send_playlist_to_winamp(window, L"C:\\synthetic\\queue.m3u8", 3, plex::PlaylistPlacement::append) == 5 &&
            host.inserted == 5 && host.length == 8 && host.valid, "append_preserves_existing_entries");
    host = {}; host.editor_missing = true;
    rejects([&] { insert(); }, "missing_playlist_editor_rejected");
    require(!host.mutations, "missing_editor_without_mutation");
    host = {}; host.current = -1;
    rejects([&] { insert(); }, "invalid_current_position_rejected");
    host = {}; host.length = (std::numeric_limits<int>::max)();
    rejects([&] { insert(); }, "playlist_length_overflow_rejected");
    host = {};
    rejects([&] { insert(L"C:\\" + std::wstring(260, L'x')); }, "insertion_path_limit_rejected");
    rejects([&] { insert(L"relative.m3u8"); }, "relative_playlist_path_rejected");
    rejects([&] { insert(std::wstring(L"C:\\synthetic\\queue.m3u8") + L'\0' + L"suffix"); }, "embedded_null_path_rejected");
    rejects([&] { insert(L"C:\\synthetic\\queue.m3u8", 0); }, "empty_insertion_rejected");
    rejects([&] { insert(L"C:\\synthetic\\queue.m3u8", 10001); }, "oversized_insertion_rejected");
    require(!host.mutations, "invalid_input_without_mutation");
    host = {}; host.added = 2;
    rejects([&] { insert(); }, "partial_host_import_reported");
}
}
