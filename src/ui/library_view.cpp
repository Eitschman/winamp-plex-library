#include "plugin/plugin.h"
#include "artwork_loader.h"
#include <gen_ml/ml_ipc_0313.h>
#include "plex/page_cache.h"
#include "plex/http_stream.h"
#include "resource.h"
#include <commctrl.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>

namespace plex::plugin {
namespace {
static_assert(sizeof(MLSKINNEDPOPUP) == 48, "Die Zielversion benötigt die dokumentierte Popup-Struktur mit Anpassungsfeldern.");
constexpr UINT result_ready = WM_APP + 42;
PageCache cache;
std::atomic<unsigned long long> cache_scope{0};
void clear_cache() { ++cache_scope; cache.clear(); }
const wchar_t* text(const wchar_t* de, const wchar_t* en) { return german() ? de : en; }

// gen_ml besitzt die Skin-Ressourcen und entfernt seine Subklassen bei WM_NCDESTROY.
// Die Ansicht verwendet dieselbe Skin-Engine wie die eingebauten Bibliotheksansichten.
void skin_view(HWND dialog) {
    const auto library = descriptor().hwndLibraryParent;
    constexpr UINT style = SWS_USESKINCOLORS | SWS_USESKINFONT | SWS_USESKINCURSORS;
    MLSKINWINDOW window{dialog, SKINNEDWND_TYPE_DIALOG, style};
    if (!MLSkinWindow(library, &window)) throw std::runtime_error("skin_dialog");
    for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const bool list = GetDlgCtrlID(child) == IDC_CATALOG;
        window = {child, static_cast<UINT>(list ? SKINNEDWND_TYPE_LISTVIEW : SKINNEDWND_TYPE_AUTO),
                  style | (list ? SWLVS_FULLROWSELECT | SWLVS_DOUBLEBUFFER : 0)};
        if (!MLSkinWindow(library, &window)) throw std::runtime_error("skin_control");
    }
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("utf8_text");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::string input(HWND dialog, int control) {
    const auto window = GetDlgItem(dialog, control);
    const auto length = GetWindowTextLengthW(window);
    if (length > 1024) throw std::invalid_argument("search_limit");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, value.data(), length + 1); value.resize(static_cast<std::size_t>(length));
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::invalid_argument("search_encoding");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring duration(long long milliseconds) {
    const auto seconds = milliseconds / 1000;
    return std::to_wstring(seconds / 60) + L":" + (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60);
}
const wchar_t* kind_title(BrowseKind kind) {
    switch (kind) {
    case BrowseKind::artists: return text(L"Künstler", L"Artists");
    case BrowseKind::albums: return text(L"Alben", L"Albums");
    case BrowseKind::tracks: return text(L"Titel", L"Tracks");
    default: return L"Playlists";
    }
}

// Die UI speichert fertig formatierte Zellen getrennt von den API-Modellen.
struct Row { PlexItem item; std::vector<std::wstring> cells; bool image_ready = false; };
Row make_row(const PlexItem& item) {
    Row row{item, {}};
    auto title = wide(item_title(item));
    if (title.empty()) title = text(L"(Ohne Titel)", L"(Untitled)");
    row.cells.push_back(std::move(title));
    std::visit([&](const auto& value) {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, PlexArtist>) row.cells.push_back(value.album_count ? std::to_wstring(value.album_count) : L"");
        if constexpr (std::is_same_v<Type, PlexAlbum>) {
            row.cells.push_back(wide(value.artist)); row.cells.push_back(value.year ? std::to_wstring(value.year) : L"");
            row.cells.push_back(value.track_count ? std::to_wstring(value.track_count) : L"");
        }
        if constexpr (std::is_same_v<Type, PlexPlaylist>) {
            row.cells.push_back(std::to_wstring(value.track_count)); row.cells.push_back(duration(value.duration_ms));
        }
        if constexpr (std::is_same_v<Type, PlexTrack>) {
            row.cells.push_back(wide(value.artist)); row.cells.push_back(wide(value.album)); row.cells.push_back(duration(value.duration_ms));
            row.cells.push_back(value.number ? std::to_wstring(value.number) : L""); row.cells.push_back(value.year ? std::to_wstring(value.year) : L"");
            row.cells.push_back(value.parts.empty() ? L"" : wide(value.parts.front().container.empty() ? value.parts.front().audio_codec : value.parts.front().container));
        }
    }, item);
    return row;
}
enum class Action { browse, play, enqueue };
struct Job {
    Action action = Action::browse;
    BrowseQuery query;
    PlexConnectionSettings settings;
    std::vector<PlexItem> selected;
    unsigned generation = 0;
    bool load_libraries = false;
    unsigned long long cache_scope = 0;
};
struct Result {
    Action action = Action::browse;
    CatalogPage page;
    std::vector<PlexTrack> tracks;
    PlaybackArtwork artwork;
    std::vector<PlexLibrary> libraries;
    bool libraries_loaded = false;
    DWORD error = 0;
};
struct Navigation { BrowseQuery query; std::wstring caption; std::vector<std::size_t> offsets{0}; };

HBITMAP artwork_bitmap(const std::optional<ArtworkPixels>& pixels) {
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 48; info.bmiHeader.biHeight = -48;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* destination = nullptr;
    const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &destination, nullptr, 0);
    if (!bitmap || !destination) throw std::runtime_error("artwork_bitmap");
    ZeroMemory(destination, 48 * 48 * 4);
    if (pixels && pixels->width <= 48 && pixels->height <= 48) {
        const auto left = (48 - pixels->width) / 2, top = (48 - pixels->height) / 2;
        for (unsigned row = 0; row < pixels->height; ++row)
            CopyMemory(static_cast<BYTE*>(destination) + ((top + row) * 48 + left) * 4,
                       pixels->bgra.data() + row * pixels->width * 4, pixels->width * 4);
    }
    return bitmap;
}

struct View {
    HWND dialog;
    HANDLE stop = nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> pending;
    std::optional<Result> completed;
    std::thread worker;
    bool closing = false;
    unsigned generation = 0;
    BrowseKind root;
    Navigation current;
    std::vector<Navigation> history;
    std::vector<Row> rows;
    std::vector<PlexLibrary> libraries;
    CatalogPage page;
    bool busy = false, suppress_search = false;
    HIMAGELIST images = nullptr;
    std::unique_ptr<ArtworkLoader> artwork;
    std::vector<bool> requested_artwork;
    unsigned artwork_generation = 0;

    View(HWND window, BrowseKind kind) : dialog(window), root(kind) {
        current.query.kind = kind; current.caption = kind_title(kind);
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop) throw std::runtime_error("worker_event");
        try { worker = std::thread([this] { run(); }); } catch (...) { CloseHandle(stop); throw; }
    }
    ~View() {
        artwork.reset();
        if (images) {
            const auto list = GetDlgItem(dialog, IDC_CATALOG);
            if (IsWindow(list)) ListView_SetImageList(list, nullptr, LVSIL_SMALL);
            ImageList_Destroy(images);
        }
        { std::lock_guard<std::mutex> lock(mutex); closing = true; SetEvent(stop); }
        wake.notify_one(); if (worker.joinable()) worker.join(); CloseHandle(stop);
    }

    void prepare_artwork() {
        const auto list = GetDlgItem(dialog, IDC_CATALOG);
        ListView_SetImageList(list, nullptr, LVSIL_SMALL);
        if (images) { ImageList_Destroy(images); images = nullptr; }
        requested_artwork.assign(rows.size(), false);
        ++artwork_generation;
        if (artwork) artwork->reset(artwork_generation);
        if ((current.query.kind != BrowseKind::albums && current.query.kind != BrowseKind::tracks) || rows.empty()) return;
        if (!artwork) { artwork = std::make_unique<ArtworkLoader>(dialog); artwork->reset(artwork_generation); }
        images = ImageList_Create(48, 48, ILC_COLOR32, static_cast<int>(rows.size()) + 1, 1);
        if (!images) throw std::runtime_error("artwork_images");
        const auto blank = artwork_bitmap(std::nullopt);
        const auto index = ImageList_Add(images, blank, nullptr); DeleteObject(blank);
        if (index != 0 || !ImageList_SetImageCount(images, static_cast<UINT>(rows.size()) + 1)) throw std::runtime_error("artwork_images");
        ListView_SetImageList(list, images, LVSIL_SMALL);
    }
    void request_artwork(int first, int last) {
        if (!images || !artwork || rows.empty() || requested_artwork.size() != rows.size()) return;
        first = (std::max)(0, first); last = (std::min)(last, static_cast<int>(rows.size()) - 1);
        for (int index = first; index <= last; ++index) {
            if (requested_artwork[static_cast<std::size_t>(index)]) continue;
            const auto* album = std::get_if<PlexAlbum>(&rows[static_cast<std::size_t>(index)].item);
            const auto* track = std::get_if<PlexTrack>(&rows[static_cast<std::size_t>(index)].item);
            if (!album && !track) continue;
            const PlexAlbum inherited = track ? PlexAlbum{track->album_identifier.empty() ? track->identifier : track->album_identifier,
                track->album, track->artist, track_artwork(*track), track->year, 1} : PlexAlbum{};
            artwork->submit(static_cast<std::size_t>(index), album ? *album : inherited, connection_settings(),
                            std::to_string(cache_scope.load()) + (track ? "|track" : "|album"));
            requested_artwork[static_cast<std::size_t>(index)] = true;
        }
    }
    void visible_artwork() {
        const auto list = GetDlgItem(dialog, IDC_CATALOG);
        const auto top = ListView_GetTopIndex(list);
        request_artwork(top, top + ListView_GetCountPerPage(list) + 1);
    }
    void apply_artwork() {
        if (!artwork) return;
        for (auto& result : artwork->take()) {
            if (result.generation != artwork_generation || result.row >= rows.size() || !images) continue;
            if (result.track_count && std::holds_alternative<PlexAlbum>(rows[result.row].item)) {
                rows[result.row].cells[3] = std::to_wstring(*result.track_count);
                std::get<PlexAlbum>(rows[result.row].item).track_count = *result.track_count;
                ListView_RedrawItems(GetDlgItem(dialog, IDC_CATALOG), static_cast<int>(result.row), static_cast<int>(result.row));
            }
            if (result.error) {
                SetDlgItemTextW(dialog, IDC_STATUS, text(L"Ein Albumcover konnte nicht geladen werden.", L"An album cover could not be loaded."));
                continue;
            }
            if (!result.pixels) continue;
            const auto bitmap = artwork_bitmap(result.pixels);
            const auto replaced = ImageList_Replace(images, static_cast<int>(result.row) + 1, bitmap, nullptr);
            DeleteObject(bitmap);
            if (!replaced) throw std::runtime_error("artwork_replace");
            rows[result.row].image_ready = true;
            ListView_RedrawItems(GetDlgItem(dialog, IDC_CATALOG), static_cast<int>(result.row), static_cast<int>(result.row));
        }
    }

    void run() noexcept {
        try {
        struct Apartment {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        } apartment;
        if (FAILED(apartment.result)) throw std::runtime_error("catalog_com");
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return closing || pending.has_value(); });
                if (closing) return;
                job = std::move(*pending); pending.reset(); ResetEvent(stop);
            }
            Result result; result.action = job.action;
            try {
                const PlexCatalog client(job.settings, stop);
                if (job.load_libraries) {
                    const PlexAuth auth(job.settings.client_identifier, stop);
                    result.libraries = parse_libraries(auth.get_server(job.settings.server_origin, job.settings.server_token, "/library/sections"));
                    result.libraries_loaded = true;
                }
                if (job.action == Action::browse) {
                    const auto key = std::to_string(job.cache_scope) + "|" + job.settings.server_identifier + "|" + catalog_path(job.query);
                    const auto cached = cache.get(key);
                    result.page = cached ? *cached : client.page(job.query);
                    if (!cached) cache.put(key, result.page);
                } else {
                    for (const auto& item : job.selected) {
                        if (std::holds_alternative<PlexTrack>(item)) result.tracks.push_back(std::get<PlexTrack>(item));
                        else {
                            BrowseQuery query; query.kind = BrowseKind::tracks;
                            query.library_identifier = job.settings.library_identifier; query.parent_identifier = item_identifier(item);
                            query.playlist_items = std::holds_alternative<PlexPlaylist>(item); query.sort = "albumOrder";
                            auto tracks = client.tracks(query);
                            result.tracks.insert(result.tracks.end(), tracks.begin(), tracks.end());
                        }
                        if (result.tracks.size() > 10000) throw std::invalid_argument("queue_limit");
                    }
                    for (const auto& track : result.tracks) direct_parts(track);
                    std::size_t artwork_size = 0;
                    for (const auto& track : result.tracks) {
                        const auto& thumb = track_artwork(track);
                        if (thumb.empty() || result.artwork.count(thumb)) continue;
                        const auto pixels = fetch_artwork(job.settings, thumb, stop, 256);
                        ArtworkBytes bytes;
                        if (pixels) {
                            bytes = std::make_shared<const std::vector<std::uint8_t>>(encode_artwork_png(*pixels));
                            artwork_size += bytes->size();
                            if (artwork_size > 64 * 1024 * 1024) throw std::invalid_argument("playback_artwork_limit");
                        }
                        result.artwork.emplace(thumb, std::move(bytes));
                    }
                }
            } catch (const HttpError& failure) { result.error = failure.code; }
              catch (const std::invalid_argument& failure) {
                result.error = std::string_view(failure.what()) == "unsupported_media" ? ERROR_NOT_SUPPORTED : ERROR_INVALID_DATA;
            } catch (...) { result.error = ERROR_INVALID_DATA; }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (closing) return;
                if (job.generation != generation) continue;
                completed = std::move(result);
            }
            PostMessageW(dialog, result_ready, 0, 0);
        }
        } catch (...) { PostMessageW(dialog, result_ready, ERROR_NOT_ENOUGH_MEMORY, 0); }
    }
    void controls() {
        const auto list = GetDlgItem(dialog, IDC_CATALOG);
        EnableWindow(GetDlgItem(dialog, IDC_PREVIOUS), !busy && current.offsets.size() > 1);
        EnableWindow(GetDlgItem(dialog, IDC_NEXT), !busy && page.more);
        EnableWindow(GetDlgItem(dialog, IDC_BACK), !history.empty());
        EnableWindow(list, !busy);
    }
    void submit(Job job) {
        job.settings = connection_settings(); job.query = current.query;
        job.cache_scope = cache_scope.load();
        if (job.settings.server_origin.empty() || job.settings.library_identifier.empty()) {
            SetDlgItemTextW(dialog, IDC_STATUS, text(L"Bitte mit Plex anmelden und eine Musikbibliothek auswählen.", L"Sign in with Plex and choose a music library."));
            return;
        }
        job.query.library_identifier = job.settings.library_identifier;
        job.load_libraries = libraries.empty();
        { std::lock_guard<std::mutex> lock(mutex); job.generation = ++generation; pending = std::move(job); completed.reset(); SetEvent(stop); }
        busy = true; controls();
        SetDlgItemTextW(dialog, IDC_STATUS, text(L"Wird geladen...", L"Loading..."));
        wake.notify_one();
    }
    void load() { submit(Job{}); }
    void columns() {
        const auto list = GetDlgItem(dialog, IDC_CATALOG);
        while (ListView_DeleteColumn(list, 0)) {}
        std::vector<std::pair<const wchar_t*, int>> labels;
        const auto kind = current.query.kind;
        if (kind == BrowseKind::artists) labels = {{text(L"Künstler", L"Artist"), 360}, {text(L"Alben", L"Albums"), 80}};
        if (kind == BrowseKind::albums) labels = {{text(L"Album", L"Album"), 300}, {text(L"Künstler", L"Artist"), 230}, {text(L"Jahr", L"Year"), 60}, {text(L"Titel", L"Tracks"), 60}};
        if (kind == BrowseKind::tracks) labels = {{text(L"Titel", L"Title"), 260}, {text(L"Künstler", L"Artist"), 180}, {L"Album", 200}, {text(L"Laufzeit", L"Duration"), 75}, {L"Nr.", 50}, {text(L"Jahr", L"Year"), 60}, {text(L"Format", L"Format"), 70}};
        if (kind == BrowseKind::playlists) labels = {{L"Playlist", 360}, {text(L"Titel", L"Tracks"), 80}, {text(L"Laufzeit", L"Duration"), 100}};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(labels[index].first); column.cx = labels[index].second;
            ListView_InsertColumn(list, static_cast<int>(index), &column);
        }
        // Ein Ansichtswechsel übernimmt keine horizontale Scrollposition der anderen Spalten.
        ListView_Scroll(list, -GetScrollPos(list, SB_HORZ), 0);
    }
    void localize() {
        SetDlgItemTextW(dialog, IDC_PAGE_TITLE, current.caption.c_str());
        for (const auto& label : {std::pair{IDC_LOGIN, text(L"Einstellungen...", L"Settings...")}, {IDC_BACK, text(L"Zurück", L"Back")},
             {IDC_SEARCH_LABEL, text(L"Suche", L"Search")}, {IDC_REFRESH, text(L"Aktualisieren", L"Refresh")}, {IDC_CLEAR_CACHE, text(L"Cache leeren", L"Clear cache")},
             {IDC_PREVIOUS, text(L"Vorherige", L"Previous")}, {IDC_NEXT, text(L"Nächste", L"Next")}})
            SetDlgItemTextW(dialog, label.first, label.second);
        columns();
    }
    void count_status() {
        std::wstring value;
        if (rows.empty()) value = text(L"Keine Einträge gefunden.", L"No items found.");
        else {
            value = std::to_wstring(page.offset + 1) + L" ... " + std::to_wstring(page.offset + rows.size());
            if (page.total) value += text(L" von ", L" of ") + std::to_wstring(*page.total);
        }
        SetDlgItemTextW(dialog, IDC_STATUS, value.c_str());
    }
    void apply() {
        std::optional<Result> result;
        { std::lock_guard<std::mutex> lock(mutex); result = std::move(completed); completed.reset(); }
        if (!result) return;
        busy = false;
        if (result->error) {
            const auto error = result->error;
            const auto http = error >= 0x20000000UL ? error - 0x20000000UL : 0;
            if (http == 401 || http == 403) SetDlgItemTextW(dialog, IDC_STATUS, text(L"Zugriff verweigert. Bitte erneut mit Plex anmelden.", L"Access denied. Sign in with Plex again."));
            else if (error == ERROR_NOT_SUPPORTED) SetDlgItemTextW(dialog, IDC_STATUS, text(L"Für die Auswahl ist kein unterstütztes Direct-Play-Medium verfügbar.", L"No supported direct-play media is available for this selection."));
            else if (error == ERROR_INVALID_DATA) SetDlgItemTextW(dialog, IDC_STATUS, text(L"Die Plex-Antwort konnte nicht verarbeitet werden.", L"The Plex response could not be processed."));
            else {
                const auto message = std::wstring(text(L"Plex-Verbindungsfehler, Code ", L"Plex connection error, code ")) + std::to_wstring(http ? http : error);
                SetDlgItemTextW(dialog, IDC_STATUS, message.c_str());
            }
            controls(); return;
        }
        if (result->libraries_loaded) {
            libraries = std::move(result->libraries);
            const auto selected = connection_settings().library_identifier;
            SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_RESETCONTENT, 0, 0);
            for (std::size_t index = 0; index < libraries.size(); ++index) {
                const auto label = wide(libraries[index].title);
                SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                if (libraries[index].identifier == selected) SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_SETCURSEL, index, 0);
            }
        }
        if (result->action == Action::browse) {
            ListView_SetItemCount(GetDlgItem(dialog, IDC_CATALOG), 0);
            page = std::move(result->page); rows.clear(); rows.reserve(page.items.size());
            for (const auto& item : page.items) rows.push_back(make_row(item));
            prepare_artwork();
            ListView_SetItemCount(GetDlgItem(dialog, IDC_CATALOG), static_cast<int>(rows.size()));
            if (!rows.empty()) ListView_EnsureVisible(GetDlgItem(dialog, IDC_CATALOG), 0, FALSE);
            count_status();
            visible_artwork();
        } else {
            try { enqueue_tracks(result->tracks, result->action == Action::play, result->artwork); count_status(); }
            catch (...) { SetDlgItemTextW(dialog, IDC_STATUS, text(L"Die Auswahl konnte nicht an Winamp übergeben werden.", L"Could not send the selection to Winamp.")); }
        }
        controls();
    }
    std::vector<PlexItem> selection() const {
        std::vector<PlexItem> result;
        const auto list = GetDlgItem(dialog, IDC_CATALOG);
        for (int index = ListView_GetNextItem(list, -1, LVNI_SELECTED); index >= 0; index = ListView_GetNextItem(list, index, LVNI_SELECTED))
            if (static_cast<std::size_t>(index) < rows.size()) result.push_back(rows[static_cast<std::size_t>(index)].item);
        return result;
    }
    void play(bool now) {
        if (busy || current.query.kind == BrowseKind::artists) return;
        auto selected = selection(); if (selected.empty()) return;
        Job job; job.action = now ? Action::play : Action::enqueue; job.selected = std::move(selected); submit(std::move(job));
    }
    void open() {
        if (busy) return;
        const auto selected = selection(); if (selected.empty()) return;
        if (std::holds_alternative<PlexTrack>(selected.front())) { play(true); return; }
        history.push_back(current);
        current.query.parent_identifier = item_identifier(selected.front()); current.query.playlist_items = std::holds_alternative<PlexPlaylist>(selected.front());
        current.query.kind = std::holds_alternative<PlexArtist>(selected.front()) ? BrowseKind::albums : BrowseKind::tracks;
        current.query.sort = current.query.kind == BrowseKind::tracks ? "albumOrder" : "titleSort";
        current.query.offset = 0; current.query.search.clear(); current.query.descending = false; current.offsets = {0};
        current.caption = wide(item_title(selected.front()));
        suppress_search = true; SetDlgItemTextW(dialog, IDC_SEARCH, L""); suppress_search = false;
        localize(); load();
    }
    void back() {
        if (history.empty()) return;
        current = std::move(history.back()); history.pop_back();
        suppress_search = true; SetDlgItemTextW(dialog, IDC_SEARCH, wide(current.query.search).c_str()); suppress_search = false;
        localize(); load();
    }
};

void layout(HWND dialog) {
    RECT area{}, units{0, 0, 4, 8}; GetClientRect(dialog, &area); MapDialogRect(dialog, &units);
    const auto horizontal = [&](int value) { return value * units.right / 4; };
    const auto vertical = [&](int value) { return value * units.bottom / 8; };
    const int width = area.right, height = area.bottom, padding = horizontal(6);
    const bool narrow = width < horizontal(350);
    auto place = [&](int control, int left, int top, int control_width, int control_height) {
        MoveWindow(GetDlgItem(dialog, control), left, top, (std::max)(1, control_width), (std::max)(1, control_height), TRUE);
    };
    place(IDC_PAGE_TITLE, padding, vertical(7), narrow ? width - horizontal(188) : horizontal(70), vertical(13));
    const int library_y = narrow ? vertical(25) : vertical(5);
    place(IDC_LIBRARY, narrow ? padding : horizontal(82), library_y, narrow ? width - 2 * padding : width - horizontal(270), vertical(100));
    place(IDC_LANGUAGE, width - horizontal(182), vertical(5), horizontal(68), vertical(100));
    place(IDC_LOGIN, width - horizontal(108), vertical(5), horizontal(102), vertical(15));
    const int search_y = narrow ? vertical(45) : vertical(27);
    place(IDC_BACK, padding, search_y, horizontal(48), vertical(15));
    place(IDC_SEARCH_LABEL, horizontal(60), search_y + vertical(2), horizontal(28), vertical(12));
    place(IDC_SEARCH, horizontal(90), search_y, width - horizontal(narrow ? 96 : 265), vertical(15));
    const int refresh_y = narrow ? search_y + vertical(20) : search_y;
    place(IDC_REFRESH, width - horizontal(169), refresh_y, horizontal(78), vertical(15));
    place(IDC_CLEAR_CACHE, width - horizontal(85), refresh_y, horizontal(79), vertical(15));
    const int bottom = height - vertical(22), list_y = refresh_y + vertical(21);
    place(IDC_CATALOG, padding, list_y, width - 2 * padding, bottom - list_y - vertical(7));
    place(IDC_STATUS, padding, bottom + vertical(3), width - horizontal(160), vertical(14));
    place(IDC_PREVIOUS, width - horizontal(140), bottom, horizontal(64), vertical(15));
    place(IDC_NEXT, width - horizontal(70), bottom, horizontal(64), vertical(15));
}
}

void clear_library_cache() { clear_cache(); }

INT_PTR library_view_message(HWND dialog, UINT message, WPARAM parameter, LPARAM detail) {
    if (message == artwork_diagnostic_message) {
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, artwork_provider_diagnostic(static_cast<unsigned>(parameter)));
        return TRUE;
    }
    auto* view = reinterpret_cast<View*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        auto created = std::make_unique<View>(dialog, static_cast<BrowseKind>(detail));
        view = created.get(); SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view)); created.release();
        SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Deutsch"));
        SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_SETCURSEL, german() ? 0 : 1, 0);
        SendDlgItemMessageW(dialog, IDC_SEARCH, EM_SETLIMITTEXT, 1024, 0);
        ListView_SetExtendedListViewStyle(GetDlgItem(dialog, IDC_CATALOG), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        const auto settings = connection_settings();
        if (!settings.library_title.empty()) {
            const auto name = wide(settings.library_title);
            SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_SETCURSEL, 0, 0);
        }
        view->localize(); skin_view(dialog); layout(dialog); view->controls(); view->load(); return TRUE;
    }
    if (!view) return FALSE;
    if (message == WM_SIZE) { layout(dialog); view->visible_artwork(); return TRUE; }
    if (message == artwork_ready) {
        if (parameter) SetDlgItemTextW(dialog, IDC_STATUS, text(L"Albumcover-Auftrag konnte nicht ausgeführt werden.", L"The album artwork task could not be completed."));
        else view->apply_artwork();
        return TRUE;
    }
    if (message == result_ready) {
        if (parameter) { view->busy = false; view->controls(); SetDlgItemTextW(dialog, IDC_STATUS, text(L"Der Hintergrundauftrag konnte nicht ausgeführt werden.", L"The background task could not be completed.")); }
        else view->apply();
        return TRUE;
    }
    if (message == WM_TIMER && parameter == 2) {
        KillTimer(dialog, 2); view->current.query.search = input(dialog, IDC_SEARCH); view->current.query.offset = 0; view->current.offsets = {0}; view->load(); return TRUE;
    }
    if (message == WM_COMMAND) {
        const auto control = LOWORD(parameter);
        if (control == IDC_SEARCH && HIWORD(parameter) == EN_CHANGE && !view->suppress_search) { SetTimer(dialog, 2, 350, nullptr); return TRUE; }
        if (control == IDC_BACK) { view->back(); return TRUE; }
        if (control == IDC_OPEN) { view->open(); return TRUE; }
        if (control == IDC_PLAY) { view->play(true); return TRUE; }
        if (control == IDC_ENQUEUE) { view->play(false); return TRUE; }
        if (control == IDC_REFRESH || control == IDC_CLEAR_CACHE) { clear_cache(); view->load(); return TRUE; }
        if (control == IDC_NEXT && !view->busy && view->page.more) {
            view->current.query.offset += view->page.items.size(); view->current.offsets.push_back(view->current.query.offset); view->load(); return TRUE;
        }
        if (control == IDC_PREVIOUS && !view->busy && view->current.offsets.size() > 1) {
            view->current.offsets.pop_back(); view->current.query.offset = view->current.offsets.back(); view->load(); return TRUE;
        }
        if (control == IDC_LOGIN) {
            show_login(dialog); clear_cache(); view->libraries.clear(); view->history.clear(); view->current = Navigation{};
            view->current.query.kind = view->root; view->current.caption = kind_title(view->root);
            view->suppress_search = true; SetDlgItemTextW(dialog, IDC_SEARCH, L""); view->suppress_search = false;
            view->localize(); view->load(); return TRUE;
        }
        if (control == IDC_LANGUAGE && HIWORD(parameter) == CBN_SELCHANGE) {
            set_german(SendDlgItemMessageW(dialog, IDC_LANGUAGE, CB_GETCURSEL, 0, 0) == 0);
            update_connection_settings(connection_settings());
            if (view->history.empty()) view->current.caption = kind_title(view->root);
            view->localize(); for (auto& row : view->rows) {
                const auto image_ready = row.image_ready; row = make_row(row.item); row.image_ready = image_ready;
            }
            InvalidateRect(GetDlgItem(dialog, IDC_CATALOG), nullptr, TRUE); return TRUE;
        }
        if (control == IDC_LIBRARY && HIWORD(parameter) == CBN_SELCHANGE) {
            const auto index = SendDlgItemMessageW(dialog, IDC_LIBRARY, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<std::size_t>(index) < view->libraries.size()) {
                auto settings = connection_settings(); const auto& library = view->libraries[static_cast<std::size_t>(index)];
                settings.library_identifier = library.identifier; settings.library_title = library.title; update_connection_settings(std::move(settings));
                view->history.clear(); view->current = Navigation{}; view->current.query.kind = view->root; view->current.caption = kind_title(view->root);
                view->suppress_search = true; SetDlgItemTextW(dialog, IDC_SEARCH, L""); view->suppress_search = false;
                view->localize(); view->load();
            }
            return TRUE;
        }
    }
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<NMHDR*>(detail);
        if (notification->idFrom != IDC_CATALOG) return FALSE;
        if (notification->code == LVN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(detail);
            const auto row = static_cast<std::size_t>(info->item.iItem), column = static_cast<std::size_t>(info->item.iSubItem);
            if ((info->item.mask & LVIF_TEXT) && row < view->rows.size() && column < view->rows[row].cells.size())
                info->item.pszText = view->rows[row].cells[column].data();
            if ((info->item.mask & LVIF_IMAGE) && row < view->rows.size()) {
                info->item.iImage = view->rows[row].image_ready ? static_cast<int>(row) + 1 : 0;
                view->request_artwork(static_cast<int>(row), static_cast<int>(row));
            }
            return TRUE;
        }
        if (notification->code == LVN_ODCACHEHINT) {
            const auto* hint = reinterpret_cast<NMLVCACHEHINT*>(detail);
            view->request_artwork(hint->iFrom, hint->iTo); return TRUE;
        }
        if (notification->code == LVN_ITEMCHANGED) { view->controls(); return TRUE; }
        if (notification->code == NM_DBLCLK) { view->open(); return TRUE; }
        if (notification->code == LVN_KEYDOWN) {
            const auto key = reinterpret_cast<NMLVKEYDOWN*>(detail)->wVKey;
            if (key == VK_RETURN) view->open();
            if (key == VK_BACK) view->back();
            if (key == 'F' && GetKeyState(VK_CONTROL) < 0) SetFocus(GetDlgItem(dialog, IDC_SEARCH));
            return TRUE;
        }
        if (notification->code == LVN_COLUMNCLICK && !view->busy) {
            const auto column = reinterpret_cast<NMLISTVIEW*>(detail)->iSubItem;
            std::string sort;
            if (column == 0) sort = "titleSort";
            if (view->current.query.kind == BrowseKind::albums) { if (column == 1) sort = "artist.titleSort"; if (column == 2) sort = "year"; }
            if (view->current.query.kind == BrowseKind::tracks) {
                const std::array<const char*, 7> fields{"titleSort", "artist.titleSort", "album.titleSort", "duration", "index", "year", ""};
                if (column >= 0 && column < 7) sort = fields[static_cast<std::size_t>(column)];
            }
            if (!sort.empty() && !view->current.query.playlist_items) {
                view->current.query.descending = sort == view->current.query.sort && !view->current.query.descending;
                view->current.query.sort = sort; view->current.query.offset = 0; view->current.offsets = {0}; view->load();
            }
            return TRUE;
        }
    }
    if (message == WM_CONTEXTMENU && reinterpret_cast<HWND>(parameter) == GetDlgItem(dialog, IDC_CATALOG)) {
        const auto selected = view->selection(); if (selected.empty() || view->busy) return TRUE;
        const auto menu = CreatePopupMenu();
        if (!menu) return TRUE;
        if (!std::holds_alternative<PlexTrack>(selected.front())) AppendMenuW(menu, MF_STRING, IDC_OPEN, text(L"Öffnen", L"Open"));
        if (view->current.query.kind != BrowseKind::artists) {
            AppendMenuW(menu, MF_STRING, IDC_PLAY, text(L"Jetzt abspielen", L"Play now"));
            AppendMenuW(menu, MF_STRING, IDC_ENQUEUE, text(L"Zur Playlist hinzufügen", L"Enqueue"));
        }
        POINT point{static_cast<short>(LOWORD(detail)), static_cast<short>(HIWORD(detail))};
        if (point.x == -1 && point.y == -1) { point = {20, 20}; ClientToScreen(GetDlgItem(dialog, IDC_CATALOG), &point); }
        MLSKINNEDPOPUP popup{sizeof(MLSKINNEDPOPUP), menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                            point.x, point.y, dialog, nullptr, nullptr, 0, SMS_USESKINFONT, nullptr, 0};
        const auto command = static_cast<UINT>(SendMessageW(descriptor().hwndLibraryParent, WM_ML_IPC,
                                                           reinterpret_cast<WPARAM>(&popup), ML_IPC_TRACKSKINNEDPOPUPEX));
        DestroyMenu(menu); if (command) PostMessageW(dialog, WM_COMMAND, command, 0); return TRUE;
    }
    if (message == WM_DESTROY) { KillTimer(dialog, 2); SetWindowLongPtrW(dialog, GWLP_USERDATA, 0); delete view; return TRUE; }
    return FALSE;
}
}
