#include "plugin/track_info.h"
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <gen_tray/WINAMPCMD.H>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
HWND editor = nullptr;
std::vector<std::wstring> playlist;
std::vector<int> selected;
std::wstring playing;
std::vector<std::wstring> forwarded;
bool foreign_cancel = false;
unsigned foreign_commands = 0;
int checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
LRESULT CALLBACK host(HWND window, UINT event, WPARAM parameter, LPARAM detail) {
    if (event == WM_WA_IPC) {
        if (detail == IPC_GETWND) return reinterpret_cast<LRESULT>(editor);
        if (detail == IPC_GET_PLAYING_FILENAME) return reinterpret_cast<LRESULT>(playing.c_str());
        if (detail == IPC_GETPLAYLISTFILEW) return parameter < playlist.size() ? reinterpret_cast<LRESULT>(playlist[parameter].c_str()) : 0;
        if (detail == IPC_INFOBOXW) {
            forwarded.push_back(reinterpret_cast<const infoBoxParamW*>(parameter)->filename);
            return foreign_cancel ? 1 : 0;
        }
        if (detail == IPC_INFOBOX) {
            forwarded.push_back(L"ANSI foreign"); return foreign_cancel ? 1 : 0;
        }
    }
    if (event == WM_USER && parameter == IPC_PE_GETNEXTSELECTED) {
        for (const auto position : selected) if (position > static_cast<int>(detail)) return position;
        return -1;
    }
    if (event == WM_COMMAND) { ++foreign_commands; return 77; }
    return DefWindowProcW(window, event, parameter, detail);
}
}
int main() {
    HWND player = nullptr;
    try {
        using namespace plex::plugin;
        WNDCLASSW type{}; type.lpfnWndProc = host; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"PlexTrackInformationTest";
        check(RegisterClassW(&type) != 0, "register_host");
        player = CreateWindowW(type.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, type.hInstance, nullptr);
        editor = CreateWindowW(type.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, type.hInstance, nullptr);
        check(player && editor, "create_hosts");
        const std::wstring first = L"http://127.0.0.1:43210/stream/owned/audio.mp3", second = L"C:\\synthetic\\owned.flac";
        TrackInformation information{L"Grüße 東京", L"Björk", L"Album", L"Album artist", L"Metal", L"2026", L"4", L"3:29", L"mp3"};
        const std::map<std::wstring, TrackInformation> known{{first, information}, {second, information}};
        std::vector<TrackInformation> displayed;
        bool accepted = true;
        {
            TrackInformationController controller(player, [&](const std::wstring& filename) -> std::optional<TrackInformation> {
                const auto found = known.find(filename); return found == known.end() ? std::nullopt : std::optional<TrackInformation>{found->second};
            }, [&](HWND owner, const TrackInformation& value) { check(IsWindow(owner), "valid_dialog_parent"); displayed.push_back(value); return accepted; });
            infoBoxParamW unicode{player, first.c_str()};
            check(SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&unicode), IPC_INFOBOXW) == 0, "unicode_info_ok_result");
            check(displayed.size() == 1 && displayed.back().title == information.title && displayed.back().artist == information.artist && forwarded.empty(), "owned_unicode_information");
            accepted = false;
            check(SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&unicode), IPC_INFOBOXW) == 1, "unicode_info_cancel_result");
            accepted = true;
            const std::string ansi_name = "http://127.0.0.1:43210/stream/owned/audio.mp3";
            infoBoxParam ansi{player, ansi_name.c_str()};
            const auto before_ansi = displayed.size();
            check(SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&ansi), IPC_INFOBOX) == 0 && displayed.size() == before_ansi + 1, "ansi_owned_information");
            unicode.filename = L"https://radio.example/live";
            SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&unicode), IPC_INFOBOXW);
            check(forwarded == std::vector<std::wstring>{L"https://radio.example/live"}, "radio_info_forwarded_unchanged");
            playing = first; displayed.clear();
            SendMessageW(player, WM_COMMAND, WINAMP_EDIT_ID3, 0);
            check(displayed.size() == 1 && foreign_commands == 0, "main_alt3_owned_information");
            playing = L"C:\\music\\foreign.mp3";
            check(SendMessageW(player, WM_COMMAND, WINAMP_EDIT_ID3, 0) == 77 && foreign_commands == 1, "main_foreign_command_preserved");
            playlist = {L"C:\\music\\foreign.mp3", first, L"https://radio.example/live", second}; selected = {0, 1, 2, 3};
            displayed.clear(); forwarded.clear();
            SendMessageW(editor, WM_COMMAND, 40208, 0);
            check(displayed.size() == 2 && forwarded == std::vector<std::wstring>{playlist[0], playlist[2]}, "mixed_selection_delegates_foreign_entries");
            displayed.clear(); forwarded.clear(); accepted = false; selected = {1, 2, 3};
            SendMessageW(editor, WM_COMMAND, 40208, 0);
            check(displayed.size() == 1 && forwarded.empty(), "owned_cancel_stops_selection");
            accepted = true; foreign_cancel = true; selected = {0, 1, 2, 3}; displayed.clear(); forwarded.clear();
            SendMessageW(editor, WM_COMMAND, 40208, 0);
            check(displayed.empty() && forwarded == std::vector<std::wstring>{playlist[0]}, "foreign_cancel_stops_selection");
            foreign_cancel = false; selected = {0, 2}; const auto commands_before = foreign_commands;
            check(SendMessageW(editor, WM_COMMAND, 40208, 0) == 77 && foreign_commands == commands_before + 1, "foreign_only_selection_native_command");
            selected.clear();
            check(SendMessageW(editor, WM_COMMAND, 40208, 0) == 77, "empty_selection_native_command");
            unicode.filename = L"http://127.0.0.1:43210/stream/unknown/audio.mp3";
            forwarded.clear(); SendMessageW(player, WM_WA_IPC, reinterpret_cast<WPARAM>(&unicode), IPC_INFOBOXW);
            check(forwarded.size() == 1 && displayed.empty(), "unbound_alias_not_claimed");
        }
        playing = first; const auto commands_before = foreign_commands;
        check(SendMessageW(player, WM_COMMAND, WINAMP_EDIT_ID3, 0) == 77 && foreign_commands == commands_before + 1, "subclasses_removed_on_shutdown");
        DestroyWindow(editor); editor = nullptr; DestroyWindow(player); player = nullptr;
        UnregisterClassW(type.lpszClassName, type.hInstance);
        std::cout << checks << " track-information routing checks passed\n"; return 0;
    } catch (const std::exception& error) {
        if (editor) DestroyWindow(editor); if (player) DestroyWindow(player);
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
