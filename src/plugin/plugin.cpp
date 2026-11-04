#include "plugin/plugin.h"
#include <Winamp/wa_ipc.h>
#include <gen_tray/WINAMPCMD.H>
#include "plex/playlist.h"
#include <cstddef>
#include <stdexcept>
#include <unordered_map>

namespace plex::plugin {
namespace {
int initialize() noexcept;
void shutdown() noexcept;
INT_PTR message(int type, INT_PTR first, INT_PTR second, INT_PTR third) noexcept;
char description[] = "Winamp Plex Library 0.1 (Phase 1)";
winampMediaLibraryPlugin plugin{MLHDR_VER, description, initialize, shutdown, message, nullptr, nullptr, nullptr};
UINT_PTR tree_id = 0;
bool use_german = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
std::unique_ptr<PlaybackProxy> proxy;
DWORD ui_thread = 0;
PlexConnectionSettings settings;
std::unordered_map<std::wstring, std::wstring> playback_titles;

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
        if (!settings.language.empty()) use_german = settings.language == "de";
        MLTREEITEMW item{};
        item.size = sizeof(item);
        item.title = const_cast<wchar_t*>(L"Plex");
        item.imageIndex = -1;
        SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, reinterpret_cast<WPARAM>(&item), ML_IPC_TREEITEM_ADDW);
        tree_id = item.id;
        return tree_id ? 0 : 1;
    } catch (...) { return 1; }
}

void shutdown() noexcept {
    destroy_views();
    stop_playback();
    if (tree_id && IsWindow(plugin.hwndLibraryParent)) {
        const auto handle = SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, tree_id, ML_IPC_TREEITEM_GETHANDLE);
        if (handle) SendMessageW(plugin.hwndLibraryParent, WM_ML_IPC, handle, ML_IPC_TREEITEM_DELETE);
    }
    tree_id = 0;
}

INT_PTR message(int type, INT_PTR first, INT_PTR second, INT_PTR) noexcept {
    try {
        if (type == ML_IPC_HOOKTITLEW && first) {
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
        if (type == ML_MSG_CONFIG) {
            show_playback_test(IsWindow(reinterpret_cast<HWND>(first)) ? reinterpret_cast<HWND>(first) : plugin.hwndLibraryParent);
            return 1;
        }
    } catch (...) { return 0; }
    return 0;
}
}

winampMediaLibraryPlugin& descriptor() noexcept { return plugin; }
bool german() noexcept { return use_german; }
void set_german(bool enabled) noexcept { use_german = enabled; }

void start_playback(PlaybackSource source, const std::wstring& title, int seconds) {
    if (GetCurrentThreadId() != ui_thread || title.empty() || seconds < -1) throw std::invalid_argument("playback_input");
    auto next = std::make_unique<PlaybackProxy>(std::move(source));
    const auto& local = next->url();
    auto safe_title = title;
    for (auto& character : safe_title) if (character < 32 || character == 127) character = L' ';
    playback_titles[std::wstring(local.begin(), local.end())] = std::move(safe_title);
    const TemporaryPlaylist playlist(make_playlist({{local, utf8(title), seconds}}));
    const auto url = playlist.path().wstring();
    COPYDATASTRUCT item{};
    item.dwData = IPC_ENQUEUEFILEW;
    item.lpData = const_cast<wchar_t*>(url.c_str());
    item.cbData = static_cast<DWORD>((url.size() + 1) * sizeof(wchar_t));
    const auto position = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
    SendMessageW(plugin.hwndWinampParent, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&item));
    const auto length = SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
    if (length != position + 1) throw std::runtime_error("playlist_enqueue");
    proxy = std::move(next);
    SendMessageW(plugin.hwndWinampParent, WM_WA_IPC, position, IPC_SETPLAYLISTPOS);
    // Der SDK-Kommentar zu IPC_SETPLAYLISTPOS verlangt den Play-Befehl.
    SendMessageW(plugin.hwndWinampParent, WM_COMMAND, MAKEWPARAM(WINAMP_BUTTON2, 0), 0);
}

void stop_playback() noexcept { proxy.reset(); playback_titles.clear(); }
ProxyStatus playback_status() noexcept { return proxy ? proxy->status() : ProxyStatus{}; }
bool playback_active() noexcept { return proxy != nullptr; }
PlexConnectionSettings connection_settings() { return settings; }
void update_connection_settings(PlexConnectionSettings value) {
    value.language = german() ? "de" : "en";
    save_settings(settings_path(), value);
    settings = std::move(value);
}
}

extern "C" __declspec(dllexport) winampMediaLibraryPlugin* __cdecl winampGetMediaLibraryPlugin() noexcept {
    return &plex::plugin::descriptor();
}
