#include "plex/winamp_playlist.h"
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace plex {
int send_playlist_to_winamp(HWND player, const std::wstring& path, std::size_t count, PlaylistPlacement placement) {
    if (!IsWindow(player) || path.empty() || path.find(L'\0') != path.npos || !std::filesystem::path(path).is_absolute() ||
        !count || count > 10000 || (placement != PlaylistPlacement::append && placement != PlaylistPlacement::after_current))
        throw std::invalid_argument("playlist_input");
    const auto length = SendMessageW(player, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
    if (length < 0 || length > (std::numeric_limits<int>::max)() - static_cast<int>(count))
        throw std::invalid_argument("playlist_length");
    int position = static_cast<int>(length);
    if (placement == PlaylistPlacement::after_current) {
        const auto current = SendMessageW(player, WM_WA_IPC, 0, IPC_GETLISTPOS);
        if (length && (current < 0 || current >= length)) throw std::runtime_error("playlist_position");
        position = length ? static_cast<int>(current) + 1 : 0;
        const auto editor = reinterpret_cast<HWND>(SendMessageW(player, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND));
        DWORD player_process = 0, editor_process = 0;
        GetWindowThreadProcessId(player, &player_process); GetWindowThreadProcessId(editor, &editor_process);
        if (!IsWindow(editor) || !player_process || player_process != editor_process) throw std::runtime_error("playlist_editor");
        fileinfoW info{};
        static_assert(sizeof(info) == MAX_PATH * sizeof(wchar_t) + sizeof(int));
        if (path.size() >= MAX_PATH) throw std::invalid_argument("playlist_insert_path");
        std::copy(path.begin(), path.end(), info.file); info.index = position;
        COPYDATASTRUCT message{IPC_PE_INSERTFILENAMEW, sizeof(info), &info};
        SendMessageW(editor, WM_COPYDATA, reinterpret_cast<WPARAM>(player), reinterpret_cast<LPARAM>(&message));
    } else {
        COPYDATASTRUCT message{IPC_ENQUEUEFILEW, static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(path.c_str())};
        SendMessageW(player, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&message));
    }
    if (SendMessageW(player, WM_WA_IPC, 0, IPC_GETLISTLENGTH) != length + static_cast<LRESULT>(count))
        throw std::runtime_error("playlist_count");
    return position;
}
}
