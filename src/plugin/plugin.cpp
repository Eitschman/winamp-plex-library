#include "plugin/plugin.h"
#include <Winamp/wa_ipc.h>
#include <gen_tray/WINAMPCMD.H>
#include "plex/playlist.h"
#include "plex/http_stream.h"
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <array>
#include <mutex>
#include <algorithm>
#include <limits>

namespace plex::plugin {
namespace {
int initialize() noexcept;
void shutdown() noexcept;
INT_PTR message(int type, INT_PTR first, INT_PTR second, INT_PTR third) noexcept;
using Metadata = std::unordered_map<std::wstring, std::wstring>;
struct PlaybackBinding { std::string identifier; long long duration_ms = 0, offset_ms = 0; bool reportable = true; };
struct QueueItem { PlaybackSource source; std::wstring title; int seconds; ArtworkBytes artwork; Metadata metadata; std::shared_ptr<const LocalMediaFile> file; PlaybackBinding binding; };
void enqueue_items(std::vector<QueueItem> items, bool play);
char description[] = "Winamp Plex Library 0.1";
winampMediaLibraryPlugin plugin{MLHDR_VER, description, initialize, shutdown, message, nullptr, nullptr, nullptr};
UINT_PTR tree_id = 0;
std::array<UINT_PTR, 4> child_ids{};
bool use_german = false;
std::unique_ptr<PlaybackProxy> proxy;
std::shared_ptr<LocalMediaStore> media_store;
std::unordered_map<std::wstring, std::shared_ptr<const LocalMediaFile>> playback_files;
DWORD ui_thread = 0;
PlexConnectionSettings settings;
unsigned long long settings_revision = 0;
bool verified_connection = false;
std::unique_ptr<PlaybackReporter> reporter;
TimelineTracker timeline;
std::unordered_map<std::wstring, PlaybackBinding> playback_bindings;
HWND monitor = nullptr;
DWORD monitor_error = 0;
unsigned long long monitor_polls = 0, monitor_matches = 0;
constexpr wchar_t monitor_class[] = L"WinampPlexPlaybackMonitor";
std::unordered_map<std::wstring, std::wstring> playback_titles;
std::unordered_map<std::wstring, Metadata> playback_metadata;
std::mutex metadata_mutex;
std::string utf8(const std::wstring& value);

// Ausschließlich der UI-Thread fragt Winamp ab. Der Reporter erhält fertige
// Schnappschüsse und führt sämtliche HTTP-Operationen auf seinem Worker aus.
void poll_playback() {
    ++monitor_polls;
    if (!reporter) return;
    std::optional<PlaybackSnapshot> snapshot;
    const auto state = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_ISPLAYING);
    if (state == 1 || state == 3) {
        const auto* filename = reinterpret_cast<const wchar_t*>(SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GET_PLAYING_FILENAME));
        if (reinterpret_cast<UINT_PTR>(filename) > 65535) {
            const auto found = playback_bindings.find(filename);
            if (found != playback_bindings.end()) {
                ++monitor_matches;
                const auto& binding = found->second;
                if (!binding.reportable) { monitor_error = ERROR_NOT_SUPPORTED; return; }
                const auto position = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
                auto duration = binding.duration_ms;
                if (duration <= 0) duration = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 2, IPC_GETOUTPUTTIME);
                if (position < 0 || duration <= 0) return;
                const auto time = (std::min)(duration, static_cast<long long>(position) + binding.offset_ms);
                snapshot = PlaybackSnapshot{utf8(std::wstring(filename)),
                    {binding.identifier, state == 3 ? PlaybackState::paused : PlaybackState::playing, time, duration}};
            }
        }
    }
    for (auto& report : timeline.observe(std::move(snapshot), GetTickCount64())) reporter->submit(std::move(report));
    monitor_error = 0;
}
LRESULT CALLBACK monitor_message(HWND window, UINT message, WPARAM parameter, LPARAM detail) noexcept {
    if (message == WM_TIMER && parameter == 1) {
        try { poll_playback(); }
        catch (const HttpError& error) { monitor_error = error.code; }
        catch (...) { monitor_error = ERROR_INVALID_DATA; }
        return 0;
    }
    return DefWindowProcW(window, message, parameter, detail);
}

std::wstring unicode(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("metadata_encoding");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string utf8(const std::wstring& value) {
    // Optionale Pfade bleiben leer, solange kein lokales Medium vorbereitet wurde.
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::invalid_argument("title_encoding");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

class TemporaryPlaylist {
public:
    explicit TemporaryPlaylist(const std::string& content) {
        const auto identifier = create_client_identifier();
        path_ = std::filesystem::temp_directory_path() / (L"WinampPlex-" + std::wstring(identifier.begin(), identifier.end()) + L".m3u8");
        const auto file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("playlist_create");
        DWORD written = 0;
        const auto success = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
        const auto closed = CloseHandle(file);
        if (!success || written != content.size() || !closed) {
            DeleteFileW(path_.c_str()); throw std::runtime_error("playlist_write");
        }
    }
    ~TemporaryPlaylist() { DeleteFileW(path_.c_str()); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

// Diese Layoutprüfungen beziehen sich auf die unveränderten SDK-5.55-Header.
static_assert(sizeof(void*) == 4);
static_assert(sizeof(winampMediaLibraryPlugin) == 32);
static_assert(offsetof(winampMediaLibraryPlugin, hwndWinampParent) == 20);
static_assert(sizeof(MLTREEITEMW) == 28);
static_assert(sizeof(enqueueFileWithMetaStructW) == 12);

int initialize() noexcept {
    try {
        if (!IsWindow(plugin.hwndLibraryParent) || !IsWindow(plugin.hwndWinampParent) || !plugin.hDllInstance)
            return 1;
        ui_thread = GetCurrentThreadId();
        settings = load_settings(settings_path());
        ++settings_revision; verified_connection = false;
        const auto language = reinterpret_cast<const wchar_t*>(SendMessageW(plugin.hwndWinampParent, WM_WA_IPC,
                                                                                 MAKEWPARAM(5, LANG_LANG_CODE), IPC_GETLANGUAGEPACKINSTANCE));
        if (reinterpret_cast<UINT_PTR>(language) <= 65535) throw std::runtime_error("host_language_unavailable");
        use_german = CompareStringOrdinal(language, -1, L"de", -1, TRUE) == CSTR_EQUAL;
        MLTREEITEMW item{};
        item.size = sizeof(item);
        item.title = const_cast<wchar_t*>(L"Plex");
        item.imageIndex = -1;
        SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, reinterpret_cast<WPARAM>(&item), ML_IPC_TREEITEM_ADDW);
        tree_id = item.id;
        if (!tree_id) return 1;
        const std::array<const wchar_t*, 4> labels = use_german ? std::array{L"Künstler", L"Alben", L"Titel", L"Playlists"} : std::array{L"Artists", L"Albums", L"Tracks", L"Playlists"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            MLTREEITEMW child{}; child.size = sizeof(child); child.parentId = tree_id;
            child.title = const_cast<wchar_t*>(labels[index]); child.imageIndex = -1;
            SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, reinterpret_cast<WPARAM>(&child), ML_IPC_TREEITEM_ADDW);
            if (!child.id) throw std::runtime_error("child_navigation");
            child_ids[index] = child.id;
        }
        initialize_artwork_provider(reinterpret_cast<api_service*>(SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE)));
        WNDCLASSW window_class{}; window_class.lpfnWndProc = monitor_message;
        window_class.hInstance = plugin.hDllInstance; window_class.lpszClassName = monitor_class;
        if (!RegisterClassW(&window_class)) throw std::runtime_error("playback_monitor_class");
        monitor = CreateWindowExW(0, monitor_class, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, plugin.hDllInstance, nullptr);
        if (!monitor || !SetTimer(monitor, 1, 500, nullptr)) throw std::runtime_error("playback_monitor_window");
        return 0;
    } catch (...) { shutdown(); return 1; }
}

void shutdown() noexcept {
    if (monitor) { KillTimer(monitor, 1); DestroyWindow(monitor); monitor = nullptr; }
    destroy_views();
    stop_playback();
    UnregisterClassW(monitor_class, plugin.hDllInstance);
    shutdown_artwork_provider();
    if (tree_id && IsWindow(plugin.hwndLibraryParent)) {
        const auto handle = SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, tree_id, ML_IPC_TREEITEM_GETHANDLE);
        if (handle) SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, handle, ML_IPC_TREEITEM_DELETE);
    }
    tree_id = 0;
    child_ids.fill(0);
}

INT_PTR message(int type, INT_PTR first, INT_PTR second, INT_PTR) noexcept {
    try {
        if (type == ML_IPC_HOOKEXTINFOW && first) {
            std::lock_guard<std::mutex> lock(metadata_mutex);
            auto* metadata = reinterpret_cast<extendedFileInfoStructW*>(first);
            if (!metadata->filename || !metadata->metadata || !metadata->ret || !metadata->retlen) return 0;
            const auto entry = playback_metadata.find(metadata->filename);
            if (entry == playback_metadata.end()) return 0;
            const auto field = entry->second.find(metadata->metadata);
            if (field == entry->second.end()) return 0;
            wcsncpy_s(metadata->ret, metadata->retlen, field->second.c_str(), _TRUNCATE);
            return 1;
        }
        if (type == ML_IPC_HOOKTITLEW && first) {
            std::lock_guard<std::mutex> lock(metadata_mutex);
            auto* metadata = reinterpret_cast<waHookTitleStructW*>(first);
            if (metadata->filename && metadata->title) {
                const auto found = playback_titles.find(metadata->filename);
                if (found != playback_titles.end()) {
                    wcsncpy_s(metadata->title, 2048, found->second.c_str(), _TRUNCATE);
                    return TRUE;
                }
            }
            return 0;
        }
        if (type == ML_MSG_TREE_ONCREATEVIEW && tree_id && static_cast<UINT_PTR>(first) == tree_id)
            return reinterpret_cast<INT_PTR>(create_view(reinterpret_cast<HWND>(second)));
        if (type == ML_MSG_TREE_ONCREATEVIEW && first)
            for (std::size_t index = 0; index < child_ids.size(); ++index)
                if (static_cast<UINT_PTR>(first) == child_ids[index])
                    return reinterpret_cast<INT_PTR>(create_view(reinterpret_cast<HWND>(second), static_cast<BrowseKind>(index)));
        if (type == ML_MSG_CONFIG) {
            show_settings(IsWindow(reinterpret_cast<HWND>(first)) ? reinterpret_cast<HWND>(first) : plugin.hwndLibraryParent);
            return 1;
        }
    } catch (...) { return 0; }
    return 0;
}
}

winampMediaLibraryPlugin& descriptor() noexcept { return plugin; }
bool german() noexcept { return use_german; }

namespace {
void enqueue_items(std::vector<QueueItem> items, bool play) {
    if (GetCurrentThreadId() != ui_thread || items.empty() || playback_titles.size() + items.size() > 10000)
        throw std::invalid_argument("playback_input");
    std::vector<PlaylistEntry> entries;
    std::vector<std::string> issued;
    try {
        for (auto& item : items) {
            if (item.title.empty() || item.seconds < -1) throw std::invalid_argument("playback_input");
            std::string local;
            if (item.file) local = utf8(item.file->path().wstring());
            else if (!proxy) { proxy = std::make_unique<PlaybackProxy>(std::move(item.source)); local = proxy->url(); }
            else local = proxy->add(std::move(item.source));
            issued.push_back(local);
            for (auto& character : item.title) if (character < 32 || character == 127) character = L' ';
            { std::lock_guard<std::mutex> lock(metadata_mutex);
              playback_titles[unicode(local)] = item.title;
              playback_metadata[unicode(local)] = std::move(item.metadata); }
            if (item.file) playback_files[unicode(local)] = item.file;
            playback_bindings[unicode(local)] = std::move(item.binding);
            bind_playback_artwork(unicode(local), std::move(item.artwork));
            entries.push_back({local, utf8(item.title), item.seconds, item.file != nullptr});
        }
    const TemporaryPlaylist playlist(make_playlist(entries, media_store ? utf8(media_store->directory().wstring()) : std::string{}));
    const auto url = playlist.path().wstring();
    COPYDATASTRUCT item{};
    item.dwData = IPC_ENQUEUEFILEW;
    item.lpData = const_cast<wchar_t*>(url.c_str());
    item.cbData = static_cast<DWORD>((url.size() + 1) * sizeof(wchar_t));
    const auto position = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
    SendMessageW(plugin.hwndWinampParent, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&item));
    const auto length = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
    if (length != position + static_cast<LRESULT>(entries.size())) throw std::runtime_error("playlist_enqueue");
    if (!reporter) reporter = std::make_unique<PlaybackReporter>(request_connection_settings());
    if (play) {
        SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, position, IPC_SETPLAYLISTPOS);
        // Der SDK-Kommentar zu IPC_SETPLAYLISTPOS verlangt den Play-Befehl.
        SendMessageW(plugin.hwndWinampParent, WM_COMMAND, MAKEWPARAM(WINAMP_BUTTON2, 0), 0);
    }
    } catch (...) {
        for (const auto& url : issued) {
            if (proxy) proxy->revoke(url);
            { std::lock_guard<std::mutex> lock(metadata_mutex);
              playback_titles.erase(unicode(url));
              playback_metadata.erase(unicode(url)); }
            playback_files.erase(unicode(url));
            playback_bindings.erase(unicode(url));
            unbind_playback_artwork(unicode(url));
        }
        throw;
    }
}
}

std::shared_ptr<LocalMediaStore> local_media_store() {
    if (!media_store) media_store = std::make_shared<LocalMediaStore>(settings_path().parent_path() / L"playback");
    return media_store;
}
void enqueue_tracks(const std::vector<PlexTrack>& tracks, bool play, const PlaybackArtwork& artwork, const PreparedMedia& prepared) {
    std::vector<QueueItem> items;
    for (const auto& track : tracks) {
        const auto parts = direct_parts(track);
        std::string label = track.title.empty() ? (german() ? "(Ohne Titel)" : "(Untitled)") : track.title;
        const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(), static_cast<int>(label.size()), nullptr, 0);
        if (!size) throw std::invalid_argument("title_encoding");
        std::wstring title(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(), static_cast<int>(label.size()), title.data(), size);
        const auto cover = artwork.find(track_artwork(track));
        Metadata metadata{{L"title", title}, {L"artist", unicode(track.artist)}, {L"albumartist", unicode(track.album_artist)},
                          {L"album", unicode(track.album)}, {L"year", track.year ? std::to_wstring(track.year) : L""},
                          {L"track", track.number ? std::to_wstring(track.number) : L""},
                          {L"disc", track.disc ? std::to_wstring(track.disc) : L""}, {L"length", std::to_wstring(track.duration_ms)}};
        long long total = 0, offset = 0;
        bool offsets_known = true;
        for (const auto& part : parts) {
            if (part.duration_ms > 0 && total <= (std::numeric_limits<long long>::max)() - part.duration_ms) total += part.duration_ms;
            else if (parts.size() > 1) offsets_known = false;
        }
        const auto timeline_duration = track.duration_ms > 0 ? track.duration_ms : total;
        for (const auto& part : parts) {
            const auto seconds = part.duration_ms / 1000;
            if (seconds > 2147483647) throw std::invalid_argument("duration_limit");
            const auto file = prepared.find(part.key);
            if (requires_local_media(part.key) && file == prepared.end()) throw std::invalid_argument("local_media_missing");
            items.push_back({{parse_server_address(settings.server_origin), settings.server_token, part.key}, title,
                part.duration_ms ? static_cast<int>(seconds) : -1, cover == artwork.end() ? nullptr : cover->second, metadata,
                file == prepared.end() ? nullptr : file->second, {track.identifier, timeline_duration, offset, offsets_known}});
            if (part.duration_ms > 0 && offset <= (std::numeric_limits<long long>::max)() - part.duration_ms) offset += part.duration_ms;
        }
    }
    enqueue_items(std::move(items), play);
}

void stop_playback() noexcept {
    try {
        if ((proxy || !playback_files.empty()) && GetCurrentThreadId() == ui_thread && IsWindow(plugin.hwndWinampParent)) {
            const auto index = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETLISTPOS);
            const auto* url = reinterpret_cast<const wchar_t*>(SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, index, IPC_GETPLAYLISTFILEW));
            bool owns = false;
            { std::lock_guard<std::mutex> lock(metadata_mutex); owns = url && playback_titles.find(url) != playback_titles.end(); }
            if (owns)
                SendMessageW(plugin.hwndWinampParent, WM_COMMAND, MAKEWPARAM(WINAMP_BUTTON4, 0), 0);
        }
    } catch (...) {}
    try {
        if (reporter) for (auto& report : timeline.observe(std::nullopt, GetTickCount64())) reporter->submit(std::move(report));
    } catch (...) {}
    reporter.reset(); timeline.reset(); playback_bindings.clear(); monitor_error = 0;
    proxy.reset();
    { std::lock_guard<std::mutex> lock(metadata_mutex); playback_titles.clear(); playback_metadata.clear(); }
    clear_playback_artwork();
    playback_files.clear(); media_store.reset();
}
ProxyStatus playback_status() noexcept { return proxy ? proxy->status() : ProxyStatus{}; }
ReporterStatus playback_reporting_status() noexcept {
    auto result = reporter ? reporter->status() : ReporterStatus{};
    if (monitor_error) result.error = monitor_error;
    return result;
}
unsigned long long playback_diagnostic(unsigned counter) noexcept {
    const auto status = playback_reporting_status();
    switch (counter) {
    case 0: return status.error;
    case 1: return status.sent;
    case 2: return monitor_polls;
    case 3: return monitor_matches;
    default: return 0;
    }
}
bool playback_active() noexcept { return proxy != nullptr; }
PlexConnectionSettings connection_settings() { return settings; }
// Hintergrundanfragen benötigen nur den ausgewählten Zugang, keine weiteren
// Server-Tokens oder Kopien aller gespeicherten Verbindungen pro Coverauftrag.
PlexConnectionSettings request_connection_settings(bool include_connections) {
    PlexConnectionSettings result{settings.client_identifier, settings.server_identifier, settings.server_origin, settings.server_name,
            settings.server_token, settings.library_identifier, settings.library_title, settings.cache_enabled};
    if (include_connections) {
        result.preferred_origin = settings.preferred_origin;
        result.server_connections = settings.server_connections;
    }
    return result;
}
unsigned long long connection_revision() noexcept { return settings_revision; }
bool connection_verified() noexcept { return verified_connection; }
// Nur der UI-Thread übernimmt ein zum aktuellen Profil gehörendes Worker-Ergebnis.
// Die Auswahl gilt für die laufende Sitzung und schreibt keine Konfiguration beim Browsen.
bool accept_verified_connection(unsigned long long revision, const std::string& origin) {
    if (revision != settings_revision) return false;
    if (!parse_server_address(origin).secure) throw std::invalid_argument("insecure_verified_origin");
    if (settings.server_origin != origin) {
        stop_playback(); clear_library_cache(); settings.server_origin = origin;
    }
    verified_connection = true;
    return true;
}
void update_connection_settings(PlexConnectionSettings value) {
    save_settings(settings_path(), value);
    if (value.server_identifier != settings.server_identifier || value.server_origin != settings.server_origin ||
        value.server_token != settings.server_token || value.library_identifier != settings.library_identifier ||
        value.cache_enabled != settings.cache_enabled) clear_library_cache();
    if (value.server_origin != settings.server_origin || value.server_token != settings.server_token) stop_playback();
    settings = std::move(value);
    ++settings_revision; verified_connection = false;
}
}

extern "C" __declspec(dllexport) winampMediaLibraryPlugin* __cdecl winampGetMediaLibraryPlugin() noexcept {
    return &plex::plugin::descriptor();
}
