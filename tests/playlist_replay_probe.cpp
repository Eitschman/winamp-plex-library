#include <windows.h>
#include <commctrl.h>
#include <wincrypt.h>
#include <objbase.h>
#include <gen_ml/ml.h>
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <gen_tray/WINAMPCMD.H>
#include "plex/catalog.h"
#include "plex/local_media.h"
#include "plex/proxy.h"
#include "plex/http_stream.h"
#include "plex/playlist.h"
#include "plex/winamp_playlist.h"
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>

// Begrenzter Laufzeitnachweis, kein Bestandteil des auslieferbaren Plugins.
// Alle Zugriffe sind auf die eigene Winamp-Testkopie beschränkt. Keine Tokens loggen.
namespace {
int initialize() noexcept;
void shutdown() noexcept;
INT_PTR message(int, INT_PTR, INT_PTR, INT_PTR) noexcept { return 0; }
char description[] = "Plex playlist replay technical probe (isolated host only)";
winampMediaLibraryPlugin plugin{MLHDR_VER, description, initialize, shutdown, message, nullptr, nullptr, nullptr};
std::filesystem::path root;
UINT operation = 0;
HANDLE stop = nullptr;
HWND playlist_editor = nullptr;
std::thread worker;
std::atomic<unsigned> probe_state{0}, failure{0};
std::atomic<unsigned> phase{0};
unsigned restored = 0, blocked = 0;
bool changing = false, pending_play = false, pending_start = false;
int previous_position = 0;
nlohmann::json aliases = nlohmann::json::object();
struct Slot { int index; std::wstring original; std::string identifier; };
std::vector<Slot> slots;
struct Resolved { plex::PlaylistEntry entry; std::string identifier; };
std::vector<Resolved> resolved;
std::set<std::string> current_targets;
std::unique_ptr<plex::PlaybackProxy> proxy, next_proxy;
std::shared_ptr<plex::LocalMediaStore> media, next_media;
std::vector<std::shared_ptr<const plex::LocalMediaFile>> files, next_files;

std::wstring wide(const std::string& value) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("utf8");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string narrow(const std::wstring& value) {
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("utf16");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
LRESULT ipc(WPARAM parameter, LPARAM command) { return SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, parameter, command); }

// Nur die Zuordnung wird persistiert; die Zugangsdaten kommen aus dem regulären DPAPI-Profil.
void save_aliases(const nlohmann::json& document) {
    auto plain = document.dump();
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data())}, encrypted{};
    if (!CryptProtectData(&input, L"Winamp Plex replay test references", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
        throw std::runtime_error("protect");
    try {
        const auto temporary = root / L"replay-probe.pending";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData)) throw std::runtime_error("write");
        output.close();
        if (!MoveFileExW(temporary.c_str(), (root / L"replay-probe.dpapi").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("replace");
        LocalFree(encrypted.pbData);
    } catch (...) { LocalFree(encrypted.pbData); throw; }
}
void load_aliases() {
    const auto path = root / L"replay-probe.dpapi";
    if (!std::filesystem::exists(path)) return;
    const auto size = std::filesystem::file_size(path);
    if (!size || size > 1024 * 1024) throw std::runtime_error("reference_size");
    std::vector<BYTE> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("read");
    DATA_BLOB source{static_cast<DWORD>(bytes.size()), bytes.data()}, plain{};
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain)) throw std::runtime_error("unprotect");
    try {
        aliases = nlohmann::json::parse(plain.pbData, plain.pbData + plain.cbData);
        if (!aliases.is_object() || aliases.size() > 100) throw std::runtime_error("reference_count");
        LocalFree(plain.pbData);
    } catch (...) { LocalFree(plain.pbData); throw; }
}

bool stale_slots() {
    slots.clear();
    const auto length = ipc(0, IPC_GETLISTLENGTH);
    if (length < 0 || length > 10000) throw std::runtime_error("playlist_size");
    for (int index = 0; index < length; ++index) {
        const auto path = reinterpret_cast<const wchar_t*>(ipc(index, IPC_GETPLAYLISTFILEW));
        if (reinterpret_cast<UINT_PTR>(path) <= 65535 || !*path) continue;
        if (current_targets.count(narrow(path))) continue;
        const auto found = aliases.find(narrow(path));
        if (found != aliases.end()) slots.push_back({index, path, found->at("track").get<std::string>()});
    }
    return !slots.empty();
}

// Sämtliche Metadaten-, TLS- und Medienoperationen bleiben außerhalb des UI-Threads.
void begin_resolution(bool bootstrap) {
    if (probe_state.load() == 1) return;
    if (worker.joinable()) worker.join();
    probe_state.store(1); failure.store(0); resolved.clear(); next_files.clear();
    const auto requested = slots;
    worker = std::thread([bootstrap, requested] {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            phase.store(1);
            if (FAILED(apartment)) throw std::runtime_error("com");
            auto settings = plex::load_settings(plex::settings_path());
            plex::PlexAuth auth(settings.client_identifier, stop);
            phase.store(2);
            const auto route = auth.connect_server({settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections}, settings.preferred_origin);
            settings.server_origin = route.origin;
            const plex::PlexCatalog catalog(settings, stop);
            std::vector<plex::PlexTrack> tracks;
            if (bootstrap) {
                phase.store(3);
                std::ifstream input(root / L"replay-probe.json");
                const auto terms = nlohmann::json::parse(input).at("titles");
                if (!terms.is_array() || terms.size() != 2) throw std::runtime_error("two_tracks_required");
                for (const auto& term : terms) {
                    plex::BrowseQuery query; query.kind = plex::BrowseKind::tracks;
                    query.library_identifier = settings.library_identifier; query.search = term.at("title").get<std::string>();
                    auto found = catalog.tracks(query);
                    const auto artist = term.value("artist", std::string{}), album = term.value("album", std::string{});
                    const auto format = term.value("format", std::string{});
                    found.erase(std::remove_if(found.begin(), found.end(), [&](const auto& track) {
                        if ((!artist.empty() && track.artist != artist && track.album_artist != artist) || (!album.empty() && track.album != album)) return true;
                        const auto parts = plex::direct_parts(track);
                        return !format.empty() && (parts.size() != 1 || plex::media_extension(parts.front().key) != "." + format);
                    }), found.end());
                    if (found.size() != 1) throw std::runtime_error("ambiguous_title");
                    tracks.push_back(found.front());
                }
            } else for (const auto& slot : requested) {
                const auto& record = aliases.at(narrow(slot.original));
                if (record.at("server") != settings.server_identifier || slot.identifier.empty() ||
                    slot.identifier.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("identity");
                const auto page = plex::parse_catalog_page(auth.get_server(settings.server_origin, settings.server_token,
                    "/library/metadata/" + slot.identifier), 0);
                if (page.items.size() != 1 || !std::holds_alternative<plex::PlexTrack>(page.items.front())) throw std::runtime_error("track");
                const auto track = std::get<plex::PlexTrack>(page.items.front());
                if (track.identifier != slot.identifier) throw std::runtime_error("track_identity");
                tracks.push_back(track);
            }
            phase.store(4);
            next_media = std::make_shared<plex::LocalMediaStore>(root / L"replay-media");
            next_proxy.reset();
            auto saved = aliases;
            for (const auto& track : tracks) {
                const auto parts = plex::direct_parts(track);
                if (parts.size() != 1) throw std::runtime_error("single_part_probe");
                plex::PlaybackSource source{plex::parse_server_address(settings.server_origin), settings.server_token, parts.front().key};
                std::string target; const auto local = plex::requires_local_media(parts.front().key);
                if (local) {
                    auto file = next_media->stage(source, stop);
                    target = file->path().u8string(); next_files.push_back(std::move(file));
                } else if (!next_proxy) {
                    next_proxy = std::make_unique<plex::PlaybackProxy>(std::move(source), std::chrono::hours(2), plex::ProxyClientPolicy::host_session, next_media);
                    target = next_proxy->url();
                } else target = next_proxy->add(std::move(source));
                saved[target] = {{"track", track.identifier}, {"server", settings.server_identifier}};
                resolved.push_back({{target, plex::playlist_display_title(track.title, track.artist), static_cast<int>(track.duration_ms / 1000), local}, track.identifier});
            }
            if (saved.size() > 100) throw std::runtime_error("probe_alias_limit");
            phase.store(5);
            save_aliases(saved); aliases = std::move(saved);
            probe_state.store(2);
        } catch (const plex::HttpError& error) { failure.store(error.code); probe_state.store(3); }
        catch (...) { failure.store(ERROR_INVALID_DATA); probe_state.store(3); }
        if (SUCCEEDED(apartment)) CoUninitialize();
        PostMessageW(plugin.hwndWinampParent, operation, bootstrap ? 11 : 10, 0);
    });
}

void import_entry(const plex::PlaylistEntry& entry, int position, bool replace) {
    const auto path = root / L"replay-entry.m3u8";
    const auto content = plex::make_playlist({entry}, entry.local_file ? next_media->directory().u8string() : std::string{});
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(content.data(), content.size())) throw std::runtime_error("playlist_write");
    output.close();
    if (replace) {
        const auto editor = reinterpret_cast<HWND>(ipc(IPC_GETWND_PE, IPC_GETWND));
        fileinfoW info{}; info.index = position;
        const auto name = path.wstring(); if (name.size() >= MAX_PATH) throw std::runtime_error("path_length");
        std::copy(name.begin(), name.end(), info.file);
        COPYDATASTRUCT data{IPC_PE_INSERTFILENAMEW, sizeof(info), &info};
        SendMessageW(editor, WM_COPYDATA, reinterpret_cast<WPARAM>(plugin.hwndWinampParent), reinterpret_cast<LPARAM>(&data));
        SendMessageW(editor, WM_USER, IPC_PE_DELETEINDEX, position + 1);
    } else plex::send_playlist_to_winamp(plugin.hwndWinampParent, path.wstring(), 1, plex::PlaylistPlacement::append);
    std::filesystem::remove(path);
}

LRESULT CALLBACK player_message(HWND window, UINT event, WPARAM parameter, LPARAM detail, UINT_PTR, DWORD_PTR) noexcept {
    try {
        if (event == operation) {
            if (parameter == 1) { slots.clear(); begin_resolution(true); return 1; }
            if (parameter == 2) return probe_state.load();
            if (parameter == 3) return restored;
            if (parameter == 4) return blocked;
            if (parameter == 5) return failure.load();
            if (parameter == 6) return phase.load();
            if (parameter == 10 || parameter == 11) {
                if (worker.joinable()) worker.join();
                if (probe_state.load() != 2) { pending_play = false; return 0; }
                changing = true;
                if (parameter == 10) {
                    if (resolved.size() != slots.size() || ipc(0, IPC_ISPLAYING)) throw std::runtime_error("restore_state");
                    for (std::size_t offset = slots.size(); offset > 0; --offset) {
                        const auto index = offset - 1;
                        const auto current = reinterpret_cast<const wchar_t*>(ipc(slots[index].index, IPC_GETPLAYLISTFILEW));
                        if (!current || slots[index].original != current) throw std::runtime_error("playlist_changed");
                        import_entry(resolved[index].entry, slots[index].index, true); ++restored;
                    }
                    ipc(previous_position, IPC_SETPLAYLISTPOS);
                } else for (const auto& item : resolved) import_entry(item.entry, 0, false);
                proxy = std::move(next_proxy); media = std::move(next_media); files = std::move(next_files);
                current_targets.clear();
                for (const auto& item : resolved) current_targets.insert(item.entry.url);
                changing = false;
                if (pending_play) {
                    pending_play = false;
                    PostMessageW(window, pending_start ? WM_WA_IPC : WM_COMMAND,
                        pending_start ? 0 : WINAMP_BUTTON2, pending_start ? IPC_STARTPLAY : 0);
                }
                return 1;
            }
        }
        int selected = 0;
        auto play = window == plugin.hwndWinampParent && event == WM_COMMAND && LOWORD(parameter) == WINAMP_BUTTON2;
        if (play) selected = static_cast<int>(ipc(0, IPC_GETLISTPOS));
        if (window == playlist_editor && ((event == WM_COMMAND && LOWORD(parameter) == IDC_PLAYLIST_PLAY) ||
            (event == WM_KEYDOWN && parameter == VK_RETURN))) {
            selected = static_cast<int>(SendMessageW(playlist_editor, WM_USER, IPC_PE_GETNEXTSELECTED, -1));
            play = selected >= 0;
        }
        if (window == playlist_editor && event == WM_LBUTTONDBLCLK) {
            POINT point{static_cast<short>(LOWORD(detail)), static_cast<short>(HIWORD(detail))};
            RECT bounds{}; GetClientRect(window, &bounds);
            if (point.x >= 12 && point.x <= bounds.right - 20 && point.y >= 20 && point.y <= bounds.bottom - 38) {
                selected = static_cast<int>(SendMessageW(window, WM_USER, IPC_PE_GETIDXFROMPOINT, reinterpret_cast<LPARAM>(&point)));
                play = selected >= 0 && selected < ipc(0, IPC_GETLISTLENGTH);
            }
        }
        const auto start = window == plugin.hwndWinampParent && event == WM_WA_IPC && (detail == IPC_STARTPLAY || detail == IPC_STARTPLAY_INT);
        if (!changing && (play || start)) {
            if (probe_state.load() == 1) { pending_play = true; pending_start = start; ++blocked; return 0; }
            if (stale_slots()) {
                previous_position = start ? 0 : selected;
                pending_play = true; pending_start = start; ++blocked;
                begin_resolution(false); return 0;
            }
        }
    } catch (...) { changing = false; pending_play = false; failure.store(ERROR_INVALID_DATA); probe_state.store(3); return 0; }
    return DefSubclassProc(window, event, parameter, detail);
}

int initialize() noexcept {
    try {
        wchar_t name[64]{}; GetClassNameW(plugin.hwndWinampParent, name, 64);
        if (wcscmp(name, L"WinampPlexTest")) return 1;
        wchar_t executable[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 1;
        root = std::filesystem::path(executable).parent_path();
        if (root.filename() != L"winamp-test" || root.parent_path().filename() != L".local") return 1;
        operation = RegisterWindowMessageW(L"WinampPlex.PlaylistReplayProbe");
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation || !stop) throw std::runtime_error("probe_event");
        load_aliases();
        playlist_editor = reinterpret_cast<HWND>(ipc(IPC_GETWND_PE, IPC_GETWND));
        if (!IsWindow(playlist_editor)) throw std::runtime_error("playlist_editor");
        if (!SetWindowSubclass(plugin.hwndWinampParent, player_message, reinterpret_cast<UINT_PTR>(plugin.hDllInstance), 0))
            throw std::runtime_error("subclass");
        if (!SetWindowSubclass(playlist_editor, player_message, reinterpret_cast<UINT_PTR>(plugin.hDllInstance), 0))
            throw std::runtime_error("editor_subclass");
        return 0;
    } catch (...) { shutdown(); return 1; }
}
void shutdown() noexcept {
    RemoveWindowSubclass(plugin.hwndWinampParent, player_message, reinterpret_cast<UINT_PTR>(plugin.hDllInstance));
    if (playlist_editor) RemoveWindowSubclass(playlist_editor, player_message, reinterpret_cast<UINT_PTR>(plugin.hDllInstance));
    if (stop) SetEvent(stop);
    if (worker.joinable()) worker.join();
    proxy.reset(); next_proxy.reset(); files.clear(); next_files.clear(); media.reset(); next_media.reset();
    if (stop) { CloseHandle(stop); stop = nullptr; }
}
}
extern "C" __declspec(dllexport) winampMediaLibraryPlugin* winampGetMediaLibraryPlugin() { return &plugin; }
