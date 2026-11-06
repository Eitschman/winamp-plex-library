#include "plugin/playlist_replay.h"
#include "plugin/plugin.h"
#include "plex/auth.h"
#include "plex/winamp_playlist.h"
#include "plex/http_stream.h"
#include "plex/artwork.h"
#include <objbase.h>
#include <commctrl.h>
#include <Winamp/wa_ipc.h>
#include <Winamp/ipc_pe.h>
#include <gen_tray/WINAMPCMD.H>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace plex::plugin {
namespace {
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("replay_encoding");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string& text) { return std::filesystem::u8path(text).wstring(); }
bool candidate(const std::wstring& name) {
    if (name.rfind(L"http://127.0.0.1:", 0) == 0) {
        const auto slash = name.find(L'/', 17);
        if (slash != name.npos) {
            const auto path = name.substr(slash);
            if (path.size() == 66 && path.substr(0, 8) == L"/stream/" &&
                path.substr(56, 6) == L"/audio" && (path.substr(62) == L".mp3" || path.substr(62) == L".m4a") &&
                path.substr(8, 48).find_first_not_of(L"0123456789abcdef") == std::wstring::npos) return true;
        }
    }
    static const auto root = (settings_path().parent_path() / L"playback").wstring();
    return name.size() > root.size() && CompareStringOrdinal(name.c_str(), static_cast<int>(root.size()), root.c_str(), static_cast<int>(root.size()), TRUE) == CSTR_EQUAL &&
        (name[root.size()] == L'/' || name[root.size()] == L'\\');
}
struct Snapshot { std::vector<std::wstring> names; int position = 0; unsigned long long revision = 0; };
struct Batch { std::vector<int> positions; std::vector<ReplayTrack> tracks; PreparedMedia prepared; PlaybackArtwork artwork; PlexConnectionSettings settings; };
}
struct PlaylistReplay::State {
    HWND player, editor = nullptr, notification;
    HINSTANCE instance;
    UINT diagnostic_message = RegisterWindowMessageW(L"WinampPlex.PlaylistReplay");
    std::function<bool(const std::wstring&)> active;
    Bind bind;
    std::function<void(const std::vector<PlaylistEntry>&)> release;
    ReplayReferences references{settings_path().parent_path() / L"playlist-references.dpapi"};
    struct Job { std::function<void(HANDLE)> work; std::function<void(DWORD)> finish; bool handoff = false; };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Job> pending;
    std::deque<std::function<void()>> completed;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE handoff_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool closing = false, busy = false, changing_playlist = false, reporting = false, speculative = false;
    int prefetch_position = -1;
    std::wstring prefetch_name;
    DWORD prefetch_error = 0;
    std::optional<std::pair<LPARAM, LRESULT>> queue_answer;
    std::thread worker;
    std::atomic<unsigned long long> generation{0};
    std::atomic<unsigned long long> handoff_generation{0};
    unsigned long long restored = 0, blocked = 0;
    DWORD failure = 0;
    std::map<std::wstring, PlaylistEntry> renewed;
    std::map<std::wstring, PlaylistEntry> owned;
    struct Action { HWND window = nullptr; UINT event = 0; WPARAM parameter = 0; LPARAM detail = 0; int selection = -1;
        std::optional<std::pair<LPARAM, LRESULT>> queue; } action;

    State(HWND main, HWND target, HINSTANCE module, std::function<bool(const std::wstring&)> current, Bind binding,
          std::function<void(const std::vector<PlaylistEntry>&)> discard)
        : player(main), notification(target), instance(module), active(std::move(current)), bind(std::move(binding)), release(std::move(discard)) {
        try {
        if (!stop || !handoff_stop || !diagnostic_message) throw std::runtime_error("replay_initialization");
        editor = reinterpret_cast<HWND>(ipc(IPC_GETWND_PE, IPC_GETWND));
        if (!SetWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) throw std::runtime_error("replay_subclass");
        if (IsWindow(editor) && !SetWindowSubclass(editor, procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) {
            throw std::runtime_error("replay_editor_subclass");
        }
        worker = std::thread([this] { run(); });
        }
        catch (...) {
            RemoveWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this));
            if (IsWindow(editor)) RemoveWindowSubclass(editor, procedure, reinterpret_cast<UINT_PTR>(this));
            if (stop) CloseHandle(stop); stop = nullptr;
            if (handoff_stop) CloseHandle(handoff_stop); handoff_stop = nullptr; throw;
        }
    }
    ~State() {
        RemoveWindowSubclass(player, procedure, reinterpret_cast<UINT_PTR>(this));
        if (IsWindow(editor)) RemoveWindowSubclass(editor, procedure, reinterpret_cast<UINT_PTR>(this));
        { std::lock_guard<std::mutex> lock(mutex); closing = true; SetEvent(stop); SetEvent(handoff_stop); pending.clear(); }
        changed.notify_one();
        if (worker.joinable()) worker.join();
        CloseHandle(stop);
        CloseHandle(handoff_stop);
    }
    LRESULT ipc(WPARAM value, LPARAM operation) const { return SendMessageW(player, WM_WA_IPC, value, operation); }
    void submit(Job job) {
        { std::lock_guard<std::mutex> lock(mutex);
          if (closing || pending.size() >= 32) throw std::runtime_error("replay_queue_limit");
          pending.push_back(std::move(job)); }
        changed.notify_one();
    }
    void run() noexcept {
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            Job job;
            { std::unique_lock<std::mutex> lock(mutex);
              changed.wait(lock, [this] { return closing || !pending.empty(); });
              if (closing) break;
              job = std::move(pending.front()); pending.pop_front(); ResetEvent(job.handoff ? handoff_stop : stop); }
            DWORD error = 0;
            try { if (FAILED(apartment)) throw HttpError(ERROR_NOT_SUPPORTED); job.work(job.handoff ? handoff_stop : stop); }
            catch (const HttpError& problem) { error = problem.code; }
            catch (...) { error = WaitForSingleObject(job.handoff ? handoff_stop : stop, 0) == WAIT_OBJECT_0 ? ERROR_CANCELLED : ERROR_INVALID_DATA; }
            { std::lock_guard<std::mutex> lock(mutex);
              if (closing) break;
              completed.push_back([finish = std::move(job.finish), error] { finish(error); }); }
            PostMessageW(notification, replay_ready_message, 0, 0);
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
    void cancel(bool cancel_handoffs = true) noexcept {
        ++generation; action = {}; busy = false; speculative = false;
        if (cancel_handoffs) { ++handoff_generation; SetEvent(handoff_stop); }
        prefetch_position = -1; prefetch_name.clear(); SetEvent(stop);
    }
    Snapshot snapshot() const {
        Snapshot result; result.revision = connection_revision();
        result.position = static_cast<int>(ipc(0, IPC_GETLISTPOS));
        const auto length = ipc(0, IPC_GETLISTLENGTH);
        if (length < 0 || length > 10000) throw HttpError(ERROR_BUFFER_OVERFLOW);
        for (int index = 0; index < length; ++index) {
            const auto value = reinterpret_cast<const wchar_t*>(ipc(index, IPC_GETPLAYLISTFILEW));
            if (reinterpret_cast<UINT_PTR>(value) <= 65535) throw HttpError(ERROR_INVALID_DATA);
            result.names.emplace_back(value);
        }
        return result;
    }
    bool unchanged(const Snapshot& original) const {
        const auto current = snapshot();
        return current.revision == original.revision && current.names == original.names && current.position == original.position;
    }
    void report(DWORD error) noexcept {
        if (speculative) { prefetch_error = error; speculative = false; busy = false; action = {}; return; }
        failure = error; busy = false; action = {};
        if (error == ERROR_CANCELLED || reporting) return;
        reporting = true;
        try {
        const auto text = german() ?
            L"Die Plex-Playlist konnte nicht wiederhergestellt werden. Bitte Anmeldung, ausgewählten Server und Erreichbarkeit prüfen. Alte Einträge ohne gespeicherte Zuordnung müssen aus der Plex-Bibliothek neu hinzugefügt werden. Fehlercode: " :
            L"Could not restore the Plex playlist. Check sign-in, selected server and connectivity. Old entries without a saved reference must be added again from the Plex library. Error code: ";
        const auto message = std::wstring(text) + std::to_wstring(error);
        MessageBoxW(player, message.c_str(), L"Winamp Plex Library", MB_OK | MB_ICONERROR);
        } catch (...) {}
        reporting = false;
    }
    // Vor dem ersten Eingriff wird der gesamte Schnappschuss geprüft. Während
    // synchroner SDK-Aufrufe werden eigene Änderungsnachrichten nicht als Benutzereingriff gewertet.
    void replace(const Snapshot& original, const Batch& batch, const std::vector<PlaylistEntry>& entries) {
        if (!unchanged(original) || entries.size() != batch.positions.size()) throw HttpError(ERROR_CANCELLED);
        try {
            changing_playlist = true;
            for (std::size_t index = 0; index < entries.size(); ++index) {
                const auto name = wide(entries[index].url);
                ipc(batch.positions[index], IPC_SETPLAYLISTPOS);
                // Die SDK-Änderung ersetzt den Dateinamen am Platz. Einfügen und
                // Löschen würde Winamps Shuffle-Tabelle erneut aufbauen.
                ipc(reinterpret_cast<WPARAM>(name.c_str()), IPC_CHANGECURRENTFILEW);
                const auto actual = reinterpret_cast<const wchar_t*>(ipc(batch.positions[index], IPC_GETPLAYLISTFILEW));
                if (reinterpret_cast<UINT_PTR>(actual) <= 65535 || name != actual) throw HttpError(ERROR_INVALID_DATA);
                if (!active(original.names[batch.positions[index]]))
                    release({{utf8(original.names[batch.positions[index]]), {}, -1, false}});
                ++restored;
            }
            ipc(original.position, IPC_SETPLAYLISTPOS);
            changing_playlist = false;
        } catch (...) { ipc(original.position, IPC_SETPLAYLISTPOS); changing_playlist = false; throw; }
    }
    void restore(const Snapshot& original, int target) {
        auto batch = std::make_shared<Batch>();
        batch->settings = request_connection_settings(true);
        std::vector<std::string> aliases;
        for (int index = 0; index < static_cast<int>(original.names.size()); ++index) {
            if (index == target && candidate(original.names[index]) && !active(original.names[index])) {
                batch->positions.push_back(index); aliases.push_back(utf8(original.names[index]));
            }
        }
        if (aliases.empty()) { resume(); return; }
        std::vector<PlaylistEntry> cached;
        for (const auto& alias : aliases) {
            const auto found = renewed.find(wide(alias));
            if (found == renewed.end() || !active(wide(found->second.url))) break;
            cached.push_back(found->second);
        }
        if (cached.size() == aliases.size()) {
            replace(original, *batch, cached); busy = false; failure = 0; speculative = false; resume(); return;
        }
        const auto media = local_media_store();
        const auto request = ++generation;
        submit({[this, request, batch, aliases = std::move(aliases), media](HANDLE cancelled) {
            if (request != generation.load()) throw HttpError(ERROR_CANCELLED);
            const auto records = references.find(aliases);
            for (const auto& record : records) if (!record) throw HttpError(ERROR_NOT_FOUND);
            auto& settings = batch->settings;
            for (const auto& record : records) if (record->server != settings.server_identifier) throw HttpError(ERROR_ACCESS_DENIED);
            const PlexAuth auth(settings.client_identifier, cancelled);
            const auto route = auth.connect_server({settings.server_identifier, settings.server_name, settings.server_token, settings.server_connections}, settings.preferred_origin);
            settings.server_origin = route.origin;
            std::map<std::string, PlexTrack> tracks;
            std::size_t artwork_bytes = 0;
            for (const auto& record : records) {
                if (WaitForSingleObject(cancelled, 0) == WAIT_OBJECT_0) throw HttpError(ERROR_CANCELLED);
                if (!tracks.count(record->track)) {
                    auto page = parse_catalog_page(auth.get_server(settings.server_origin, settings.server_token, "/library/metadata/" + record->track), 0);
                    if (page.items.size() != 1 || !std::holds_alternative<PlexTrack>(page.items.front())) throw HttpError(ERROR_NOT_FOUND);
                    auto track = std::get<PlexTrack>(std::move(page.items.front()));
                    if (track.identifier != record->track) throw HttpError(ERROR_INVALID_DATA);
                    tracks.emplace(track.identifier, std::move(track));
                }
                const auto& track = tracks.at(record->track);
                const auto parts = direct_parts(track);
                const auto part = std::find_if(parts.begin(), parts.end(), [&](const auto& value) { return value.key == record->part; });
                if (part == parts.end()) throw HttpError(ERROR_FILE_NOT_FOUND);
                if (requires_local_media(part->key) && !batch->prepared.count(part->key))
                    batch->prepared.emplace(part->key, media->stage({parse_server_address(settings.server_origin), settings.server_token, part->key}, cancelled));
                const auto& thumb = track_artwork(track);
                if (!thumb.empty() && !batch->artwork.count(thumb)) {
                    const auto pixels = fetch_artwork(settings, thumb, cancelled, 256);
                    ArtworkBytes image;
                    if (pixels) {
                        image = std::make_shared<const std::vector<std::uint8_t>>(encode_artwork_png(*pixels)); artwork_bytes += image->size();
                        if (artwork_bytes > 64 * 1024 * 1024) throw HttpError(ERROR_BUFFER_OVERFLOW);
                    }
                    batch->artwork.emplace(thumb, std::move(image));
                }
                batch->tracks.push_back({*record, track});
            }
        }, [this, request, original, batch](DWORD error) {
            if (request != generation) return;
            if (error) { report(error); return; }
            if (!unchanged(original)) { report(ERROR_CANCELLED); return; }
            // Die gewählte Verbindung bleibt ein sitzungslokales, geprüftes Ziel.
            // Bereits laufende Grants dürfen dabei nicht durch einen Routenwechsel widerrufen werden.
            const auto entries = std::make_shared<std::vector<PlaylistEntry>>(bind(batch->tracks, batch->settings, batch->prepared, batch->artwork));
            std::vector<ReplayReference> aliases;
            for (std::size_t index = 0; index < entries->size(); ++index) {
                auto reference = batch->tracks[index].reference; reference.alias = (*entries)[index].url; aliases.push_back(std::move(reference));
            }
            try { submit({[this, request, aliases = std::move(aliases)](HANDLE cancelled) {
                if (request != generation.load()) throw HttpError(ERROR_CANCELLED);
                references.merge(aliases, cancelled);
            },
                [this, request, original, batch, entries](DWORD saved) {
                    if (request != generation) { release(*entries); return; }
                    if (saved) { release(*entries); report(saved); return; }
                    try {
                        replace(original, *batch, *entries);
                        for (std::size_t index = 0; index < entries->size(); ++index) {
                            owned[wide((*entries)[index].url)] = (*entries)[index];
                            if (renewed.size() >= 10000) renewed.clear();
                            renewed[original.names[batch->positions[index]]] = (*entries)[index];
                        }
                        busy = false; failure = 0; speculative = false; prefetch_error = 0; resume();
                    }
                    catch (const HttpError& problem) { release(*entries); report(problem.code); }
                    catch (...) { release(*entries); report(ERROR_INVALID_DATA); }
                }}); } catch (...) { release(*entries); throw; }
        }});
    }
    void resume() {
        const auto pending_action = action; action = {};
        if (!pending_action.window) return;
        changing_playlist = true;
        queue_answer = pending_action.queue;
        if (pending_action.selection >= 0) ipc(pending_action.selection, IPC_SETPLAYLISTPOS);
        if (pending_action.window == editor) SendMessageW(player, WM_COMMAND, WINAMP_BUTTON2, 0);
        else SendMessageW(pending_action.window, pending_action.event, pending_action.parameter, pending_action.detail);
        queue_answer.reset(); changing_playlist = false;
    }
    static LRESULT CALLBACK procedure(HWND window, UINT event, WPARAM parameter, LPARAM detail, UINT_PTR, DWORD_PTR data) noexcept {
        auto& current = *reinterpret_cast<State*>(data);
        try { return current.message(window, event, parameter, detail); }
        catch (const HttpError& problem) { current.report(problem.code); return 0; }
        catch (...) { current.report(ERROR_INVALID_DATA); return 0; }
    }
    LRESULT message(HWND window, UINT event, WPARAM parameter, LPARAM detail) {
        if (window == player && event == WM_WA_IPC && queue_answer && detail == queue_answer->first) return queue_answer->second;
        if (event == diagnostic_message) {
            if (parameter == 0) return busy ? 1 : failure ? 3 : 2;
            if (parameter == 1) return static_cast<LRESULT>(restored);
            if (parameter == 2) return static_cast<LRESULT>(blocked);
            if (parameter == 3) return failure;
            if (parameter == 4) return reporting;
            if (parameter == 5) return static_cast<LRESULT>(owned.size());
            if (parameter == 6) return prefetch_error;
            return 0;
        }
        if (!changing_playlist && window == player && event == WM_COMMAND && (LOWORD(parameter) == WINAMP_BUTTON4 || LOWORD(parameter) == WINAMP_BUTTON4_SHIFT)) cancel();
        if (window == player && event == WM_WA_IPC && detail == IPC_PLAYLIST_MODIFIED && !changing_playlist && busy) cancel(false);
        bool play = false;
        int selection = -1;
        if (window == player) {
            play = (event == WM_COMMAND && (LOWORD(parameter) == WINAMP_BUTTON2 || LOWORD(parameter) == WINAMP_BUTTON1 || LOWORD(parameter) == WINAMP_BUTTON5 ||
                LOWORD(parameter) == WINAMP_BUTTON1_CTRL || LOWORD(parameter) == WINAMP_BUTTON5_CTRL)) || event == WM_WA_MPEG_EOF ||
                (event == WM_WA_IPC && (detail == IPC_STARTPLAY || detail == IPC_STARTPLAY_INT));
        } else if (window == editor) {
            if ((event == WM_COMMAND && LOWORD(parameter) == IDC_PLAYLIST_PLAY) || (event == WM_KEYDOWN && parameter == VK_RETURN)) {
                selection = static_cast<int>(SendMessageW(editor, WM_USER, IPC_PE_GETNEXTSELECTED, -1)); play = selection >= 0;
            }
            if (event == WM_LBUTTONDBLCLK) {
                POINT point{static_cast<short>(LOWORD(detail)), static_cast<short>(HIWORD(detail))};
                RECT bounds{}; GetClientRect(editor, &bounds);
                if (point.x >= 12 && point.x <= bounds.right - 20 && point.y >= 20 && point.y <= bounds.bottom - 38) {
                    selection = static_cast<int>(SendMessageW(editor, WM_USER, IPC_PE_GETIDXFROMPOINT, reinterpret_cast<LPARAM>(&point)));
                    play = selection >= 0 && selection < ipc(0, IPC_GETLISTLENGTH);
                }
            }
        }
        if (!changing_playlist && play) {
            if (speculative) cancel(false);
            int target = selection;
            const auto count = ipc(0, IPC_GETLISTLENGTH);
            const auto current_position = static_cast<int>(ipc(0, IPC_GETLISTPOS));
            const auto command = event == WM_COMMAND ? LOWORD(parameter) : 0;
            if (window == player && command == WINAMP_BUTTON2) {
                // Play setzt einen pausierten Decoder fort, auch wenn der Cursor
                // inzwischen auf einem anderen Playlist-Eintrag steht.
                if (ipc(0, IPC_ISPLAYING) == 3) { if (busy) cancel(false); return DefSubclassProc(window, event, parameter, detail); }
                target = current_position;
            }
            Action requested{window, event, parameter, detail, selection};
            const bool navigation = window == player && (command == WINAMP_BUTTON1 || command == WINAMP_BUTTON5 ||
                command == WINAMP_BUTTON1_CTRL || command == WINAMP_BUTTON5_CTRL ||
                (event == WM_WA_IPC && (detail == IPC_STARTPLAY || detail == IPC_STARTPLAY_INT)));
            if (navigation || event == WM_WA_MPEG_EOF) {
                // Ohne eigene Einträge bleibt auch die Auswahl vollständig bei Winamp.
                bool own = false;
                for (int index = 0; index < count && !own; ++index) {
                    const auto name = reinterpret_cast<const wchar_t*>(ipc(index, IPC_GETPLAYLISTFILEW));
                    own = reinterpret_cast<UINT_PTR>(name) > 65535 && candidate(name);
                }
                if (!own) return DefSubclassProc(window, event, parameter, detail);
                if (count > 10000) {
                    for (int index = 0; index < count; ++index) {
                        const auto name = reinterpret_cast<const wchar_t*>(ipc(index, IPC_GETPLAYLISTFILEW));
                        if (reinterpret_cast<UINT_PTR>(name) > 65535 && candidate(name) && !active(name)) throw HttpError(ERROR_BUFFER_OVERFLOW);
                    }
                    return DefSubclassProc(window, event, parameter, detail);
                }
                if (busy) cancel(false);
                std::optional<std::pair<LPARAM, LRESULT>> answer;
                if (command == WINAMP_BUTTON1 || command == WINAMP_BUTTON5 || event == WM_WA_MPEG_EOF) {
                    const auto operation = command == WINAMP_BUTTON1 ? IPC_GET_PREVIOUS_PLITEM : IPC_GET_NEXT_PLITEM;
                    answer = std::make_pair(operation, ipc(0, operation));
                }
                const bool eof = event == WM_WA_MPEG_EOF;
                const bool repeat = ipc(0, IPC_GET_REPEAT) != 0;
                if (eof && answer->second < 0 && (!ipc(0, IPC_GET_STOP_AFTER_CURRENT) ||
                    (ipc(0, IPC_GET_MANUALPLADVANCE) && !repeat) ||
                    (!ipc(0, IPC_GET_SHUFFLE) && !ipc(0, IPC_GET_MANUALPLADVANCE) && current_position + 1 >= count && !repeat))) {
                    // Reguläres Ende und native Stop-Einstellungen bleiben beim Host.
                    action = requested; action.queue = answer; resume(); return 0;
                } else {
                    const auto playing = ipc(0, IPC_ISPLAYING);
                    const auto elapsed = playing ? ipc(0, IPC_GETOUTPUTTIME) : 0;
                    // Winamp wählt im gestoppten Zustand selbst, einschließlich Shuffle.
                    // Ein Queue-Plugin wird dabei genau einmal nach seinem Ziel gefragt.
                    changing_playlist = true;
                    if (playing) SendMessageW(player, WM_COMMAND, WINAMP_BUTTON4, 0);
                    queue_answer = answer;
                    if (answer && answer->second >= 0) ipc(answer->second, IPC_SETPLAYLISTPOS);
                    else if (eof && ipc(0, IPC_GET_MANUALPLADVANCE)) ipc(current_position, IPC_SETPLAYLISTPOS);
                    else if (eof) SendMessageW(player, WM_COMMAND, WINAMP_BUTTON5, 0);
                    else if (event == WM_WA_IPC) SendMessageW(player, WM_COMMAND, WINAMP_BUTTON1_CTRL, 0);
                    else DefSubclassProc(window, event, parameter, detail);
                    queue_answer.reset(); changing_playlist = false;
                    target = static_cast<int>(ipc(0, IPC_GETLISTPOS));
                    if (eof && !repeat && target == current_position && answer->second < 0) return 0;
                    if (!eof && playing && answer && answer->second < 0 && target == current_position) {
                        // Ein Rand der nativen Navigation kann ein unveränderter Titel
                        // sein. Dessen Fortschritt und Pause bleiben erhalten.
                        changing_playlist = true;
                        SendMessageW(player, WM_COMMAND, WINAMP_BUTTON2, 0);
                        if (elapsed > 0) ipc(elapsed, IPC_JUMPTOTIME);
                        if (playing == 3) SendMessageW(player, WM_COMMAND, WINAMP_BUTTON3, 0);
                        changing_playlist = false; return 0;
                    }
                    if (!eof && !playing && event != WM_WA_IPC && !(answer && answer->second >= 0)) return 0;
                    requested = {player, WM_COMMAND, WINAMP_BUTTON2, 0, target};
                }
            }
            if (target >= 0 && target < count) {
                const auto name = reinterpret_cast<const wchar_t*>(ipc(target, IPC_GETPLAYLISTFILEW));
                if (reinterpret_cast<UINT_PTR>(name) > 65535 && !candidate(name)) {
                    // Fremde Quellen benötigen weder Plex noch dessen Registry.
                    // Die eigentliche Auswahl und Wiedergabe bleibt vollständig bei Winamp.
                    if (busy) cancel(false);
                    action = requested; resume(); return 0;
                }
            }
            if (reporting) return 0;
            if (busy) cancel(false);
            // Die eigene Auflösungsgrenze darf große fremde Playlists nicht sperren.
            if (count > 10000) {
                for (int index = 0; index < count; ++index) {
                    const auto name = reinterpret_cast<const wchar_t*>(ipc(index, IPC_GETPLAYLISTFILEW));
                    if (reinterpret_cast<UINT_PTR>(name) > 65535 && candidate(name) && !active(name)) throw HttpError(ERROR_BUFFER_OVERFLOW);
                }
                return DefSubclassProc(window, event, parameter, detail);
            }
            const auto original = snapshot();
            if (target >= 0 && target < count && candidate(original.names[target]) && !active(original.names[target])) {
                action = requested; busy = true; failure = 0; ++blocked;
                restore(original, target); return 0;
            }
            action = requested; resume(); return 0;
        }
        return DefSubclassProc(window, event, parameter, detail);
    }
};
PlaylistReplay::PlaylistReplay(HWND player, HWND notification, HINSTANCE instance, std::function<bool(const std::wstring&)> active, Bind bind,
                               std::function<void(const std::vector<PlaylistEntry>&)> release)
    : state_(std::make_unique<State>(player, notification, instance, std::move(active), std::move(bind), std::move(release))) {}
PlaylistReplay::~PlaylistReplay() = default;
void PlaylistReplay::persist(std::vector<ReplayReference> references, std::function<void()> accepted, std::function<void()> rejected) {
    auto* current = state_.get();
    if (current->speculative) current->cancel(false);
    // Vorladen und einzelne Auflösungen dürfen unabhängig eingereichte
    // Bibliotheksübergaben nicht verwerfen. Stop/Verbindungswechsel tun dies.
    const auto request = current->handoff_generation.load();
    state_->submit({[current, request, references = std::move(references)](HANDLE stop) {
        if (request != current->handoff_generation.load()) throw HttpError(ERROR_CANCELLED);
        current->references.merge(references, stop);
    },
        [current, request, accepted = std::move(accepted), rejected = std::move(rejected)](DWORD error) {
            if (request != current->handoff_generation.load()) { rejected(); return; }
            if (error) { rejected(); current->cancel(false); current->report(error); } else accepted();
        }, true});
}
void PlaylistReplay::dispatch() noexcept {
    try {
    std::deque<std::function<void()>> callbacks;
    { std::lock_guard<std::mutex> lock(state_->mutex); callbacks.swap(state_->completed); }
    for (auto& callback : callbacks) {
        try { callback(); }
        catch (const HttpError& problem) { state_->report(problem.code); }
        catch (...) { state_->report(ERROR_INVALID_DATA); }
    }
    } catch (...) { state_->report(ERROR_NOT_ENOUGH_MEMORY); }
}
void PlaylistReplay::cancel() noexcept { state_->cancel(); }
// Nur ein spekulativer Titel läuft neben dem aktuellen Decoder. Ein Benutzerauftrag
// bricht diesen Download ab und erhält vor dem nächsten Workerauftrag Vorrang.
void PlaylistReplay::prefetch(int position) noexcept {
    auto& current = *state_;
    try {
        if (current.busy && !current.speculative) return;
        const auto count = current.ipc(0, IPC_GETLISTLENGTH);
        const auto read_name = [&](int index) {
            if (index < 0 || index >= count) return std::wstring{};
            const auto value = reinterpret_cast<const wchar_t*>(current.ipc(index, IPC_GETPLAYLISTFILEW));
            return reinterpret_cast<UINT_PTR>(value) > 65535 ? std::wstring(value) : std::wstring{};
        };
        const auto name = read_name(position);
        std::wstring playing;
        if (current.ipc(0, IPC_ISPLAYING)) {
            const auto actual = reinterpret_cast<const wchar_t*>(current.ipc(0, IPC_GET_PLAYING_FILENAME));
            if (reinterpret_cast<UINT_PTR>(actual) > 65535) playing = actual;
        } else playing = read_name(static_cast<int>(current.ipc(0, IPC_GETLISTPOS)));
        for (auto entry = current.owned.begin(); entry != current.owned.end();) {
            if (entry->first != playing && entry->first != name) {
                current.release({entry->second}); entry = current.owned.erase(entry);
            } else ++entry;
        }
        if (current.prefetch_position == position && current.prefetch_name == name) return;
        if (current.speculative) current.cancel(false);
        current.prefetch_position = position; current.prefetch_name = name;
        if (name.empty() || !candidate(name) || current.active(name)) return;
        current.busy = true; current.speculative = true;
        current.restore(current.snapshot(), position);
    } catch (const HttpError& problem) { current.report(problem.code); }
    catch (...) { current.report(ERROR_INVALID_DATA); }
}
unsigned long long PlaylistReplay::diagnostic(unsigned counter) const noexcept {
    switch (counter) {
    case 0: return state_->busy ? 1 : state_->failure ? 3 : 2;
    case 1: return state_->restored;
    case 2: return state_->blocked;
    case 3: return state_->failure;
    case 4: return state_->reporting;
    case 5: return state_->owned.size();
    case 6: return state_->prefetch_error;
    default: return 0;
    }
}
}
