#include "plugin/plugin.h"
#include <Winamp/wa_ipc.h>
#include <gen_tray/WINAMPCMD.H>
#include "plex/playlist.h"
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <array>
#include <mutex>

namespace plex::plugin {
namespace {
int initialize() noexcept;
void shutdown() noexcept;
INT_PTR message(int type, INT_PTR first, INT_PTR second, INT_PTR third) noexcept;
using Metadata = std::unordered_map<std::wstring, std::wstring>;
struct QueueItem { PlaybackSource source; std::wstring title; int seconds; ArtworkBytes artwork; Metadata metadata; std::shared_ptr<const LocalMediaFile> file; };
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
std::unordered_map<std::wstring, std::wstring> playback_titles;
std::unordered_map<std::wstring, Metadata> playback_metadata;
std::mutex metadata_mutex;

std::wstring unicode(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("metadata_encoding");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string utf8(const std::wstring& value) {
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
        return 0;
    } catch (...) { shutdown(); return 1; }
}

void shutdown() noexcept {
    destroy_views();
    stop_playback();
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
        Metadata metadata{{L"title", title}, {L"artist", unicode(track.artist)}, {L"albumartist", unicode(track.artist)},
                          {L"album", unicode(track.album)}, {L"year", track.year ? std::to_wstring(track.year) : L""},
                          {L"track", track.number ? std::to_wstring(track.number) : L""},
                          {L"disc", track.disc ? std::to_wstring(track.disc) : L""}, {L"length", std::to_wstring(track.duration_ms)}};
        for (const auto& part : parts) {
            const auto seconds = part.duration_ms / 1000;
            if (seconds > 2147483647) throw std::invalid_argument("duration_limit");
            const auto file = prepared.find(part.key);
            if (requires_local_media(part.key) && file == prepared.end()) throw std::invalid_argument("local_media_missing");
            items.push_back({{parse_server_address(settings.server_origin), settings.server_token, part.key}, title,
                part.duration_ms ? static_cast<int>(seconds) : -1, cover == artwork.end() ? nullptr : cover->second, metadata,
                file == prepared.end() ? nullptr : file->second});
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
    proxy.reset();
    { std::lock_guard<std::mutex> lock(metadata_mutex); playback_titles.clear(); playback_metadata.clear(); }
    clear_playback_artwork();
    playback_files.clear(); media_store.reset();
}
ProxyStatus playback_status() noexcept { return proxy ? proxy->status() : ProxyStatus{}; }
bool playback_active() noexcept { return proxy != nullptr; }
PlexConnectionSettings connection_settings() { return settings; }
// Hintergrundanfragen benötigen nur den ausgewählten Zugang, keine weiteren
// Server-Tokens oder Kopien aller gespeicherten Verbindungen pro Coverauftrag.
PlexConnectionSettings request_connection_settings() {
    return {settings.client_identifier, settings.server_identifier, settings.server_origin, settings.server_name,
            settings.server_token, settings.library_identifier, settings.library_title, settings.cache_enabled};
}
void update_connection_settings(PlexConnectionSettings value) {
    save_settings(settings_path(), value);
    if (value.server_identifier != settings.server_identifier || value.server_origin != settings.server_origin ||
        value.server_token != settings.server_token || value.library_identifier != settings.library_identifier ||
        value.cache_enabled != settings.cache_enabled) clear_library_cache();
    if (value.server_origin != settings.server_origin || value.server_token != settings.server_token) stop_playback();
    settings = std::move(value);
}
}

extern "C" __declspec(dllexport) winampMediaLibraryPlugin* __cdecl winampGetMediaLibraryPlugin() noexcept {
    return &plex::plugin::descriptor();
}
