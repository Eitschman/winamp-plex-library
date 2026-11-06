#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <windows.h>
#include <bcrypt.h>
#include "plex/proxy.h"
#include "plex/http_stream.h"
#include "plex/stream_buffer.h"
#include "plex/settings.h"
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <condition_variable>
#include <algorithm>

namespace plex {
namespace {
struct LocalDisconnect : std::runtime_error { LocalDisconnect() : std::runtime_error("local_disconnect") {} };
bool socket_ready(SOCKET client, bool writing, HANDLE stop) {
    if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) throw LocalDisconnect();
    fd_set ready; FD_ZERO(&ready); FD_SET(client, &ready);
    timeval timeout{0, 100000};
    const auto result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, nullptr, &timeout);
    if (result == SOCKET_ERROR) throw LocalDisconnect();
    return result > 0;
}

// Ein voller Decoderpuffer ist regulärer Rückstau, kein Verbindungsabbruch.
// Nicht blockierende Sockets erhalten den Stream auch bei Pause; das Stop-Event
// bleibt beim Warten auf weitere Empfangsbereitschaft regelmäßig prüfbar.
void send_all(SOCKET client, const char* buffer, std::size_t size, HANDLE stop) {
    while (size) {
        if (!socket_ready(client, true, stop)) continue;
        const auto sent = send(client, buffer, static_cast<int>(size), 0);
        if (sent == SOCKET_ERROR && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR)) continue;
        if (sent <= 0) throw LocalDisconnect();
        buffer += sent; size -= static_cast<std::size_t>(sent);
    }
}

void send_status(SOCKET client, unsigned status, HANDLE stop) {
    const auto response = "HTTP/1.1 " + std::to_string(status) + " Error\r\nContent-Length: 0\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(client, response.data(), response.size(), stop);
}

std::string random_key() {
    std::array<unsigned char, 24> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("random_source");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

// Nur der vom Kernel bestätigte Clientprozess erhält Sitzungszugriff.
// Host-/Origin-Header oder übertragene Prozessnummern sind kein Identitätsnachweis.
bool request_from_host(SOCKET client) {
    sockaddr_in peer{}, local{};
    int peer_size = sizeof(peer), local_size = sizeof(local);
    if (getpeername(client, reinterpret_cast<sockaddr*>(&peer), &peer_size) ||
        getsockname(client, reinterpret_cast<sockaddr*>(&local), &local_size)) throw HttpError(WSAGetLastError());
    if (peer.sin_family != AF_INET || local.sin_family != AF_INET ||
        peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || local.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return false;
    DWORD size = 0;
    auto outcome = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    for (unsigned attempt = 0; attempt < 3 && outcome == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        if (size < sizeof(MIB_TCPTABLE_OWNER_PID) || size > 8 * 1024 * 1024) throw HttpError(ERROR_INVALID_DATA);
        std::vector<unsigned char> buffer(size);
        outcome = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (outcome == ERROR_INSUFFICIENT_BUFFER) continue;
        if (outcome != NO_ERROR) throw HttpError(outcome);
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        if (size < offsetof(MIB_TCPTABLE_OWNER_PID, table) ||
            table->dwNumEntries > (size - offsetof(MIB_TCPTABLE_OWNER_PID, table)) / sizeof(MIB_TCPROW_OWNER_PID))
            throw HttpError(ERROR_INVALID_DATA);
        for (DWORD index = 0; index < table->dwNumEntries; ++index) {
            const auto& row = table->table[index];
            if (row.dwLocalAddr == peer.sin_addr.s_addr && static_cast<USHORT>(row.dwLocalPort) == peer.sin_port &&
                row.dwRemoteAddr == local.sin_addr.s_addr && static_cast<USHORT>(row.dwRemotePort) == local.sin_port)
                return row.dwOwningPid == GetCurrentProcessId();
        }
        return false;
    }
    throw HttpError(outcome == NO_ERROR ? ERROR_INVALID_DATA : outcome);
}
}

PlaybackSource::~PlaybackSource() {
    if (!token.empty()) SecureZeroMemory(token.data(), token.size());
}

struct PlaybackProxy::State {
    struct Grant {
        PlaybackSource source;
        std::chrono::steady_clock::time_point expires;
    };
    std::mutex grants_mutex;
    std::unordered_map<std::string, std::shared_ptr<Grant>> grants;
    SOCKET listener = INVALID_SOCKET;
    bool sockets_started = false;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::array<std::thread, 4> workers;
    std::string authority;
    std::string local_url;
    std::chrono::milliseconds lifetime{std::chrono::hours(2)};
    ProxyClientPolicy policy = ProxyClientPolicy::capability_only;
    std::atomic<DWORD> upstream{0};
    std::atomic<DWORD> error{0};
    std::atomic<const char*> rejection{nullptr};
    std::atomic<unsigned long long> local_disconnects{0};
    mutable std::mutex failure_mutex;
    ProxyFailure last_failure;
    std::shared_ptr<LocalMediaStore> media_store;
    std::atomic<unsigned long long> bytes_received{0}, active_downloads{0};
    struct Prefetched {
        std::shared_ptr<LocalMediaBuffer> file;
        unsigned long long size;
        std::string content_type;
    };
    std::mutex prefetch_mutex;
    std::condition_variable prefetch_changed;
    std::thread prefetch_worker;
    HANDLE prefetch_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::string current_target, next_target;
    unsigned long long prefetch_generation = 0;
    std::unordered_map<std::string, std::shared_ptr<Prefetched>> prefetched;
    std::atomic<unsigned long long> prefetch_bytes{0}, prefetch_hits{0};
    std::atomic<DWORD> prefetch_error{0};
    std::atomic<bool> prefetch_active{false};

    void plan_prefetch(std::string current, std::string next) {
        std::lock_guard<std::mutex> lock(prefetch_mutex);
        if (current == current_target && next == next_target) return;
        current_target = std::move(current); next_target = std::move(next);
        ++prefetch_generation; SetEvent(prefetch_stop); prefetch_changed.notify_one();
    }

    // Höchstens zwei vollständige Dateien mit je 32 MiB. Teilweise geladene
    // Dateien werden nie an den Decoder ausgegeben. Das gemeinsame Budget gilt zusätzlich.
    void run_prefetch() noexcept {
      try {
        unsigned long long handled = 0;
        for (;;) {
            std::string target;
            unsigned long long generation;
            std::vector<std::shared_ptr<Prefetched>> retired;
            {
                std::unique_lock<std::mutex> lock(prefetch_mutex);
                prefetch_changed.wait(lock, [&] { return WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || handled != prefetch_generation; });
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return;
                handled = generation = prefetch_generation;
                for (auto entry = prefetched.begin(); entry != prefetched.end();) {
                    if (entry->first == current_target || entry->first == next_target) { ++entry; continue; }
                    prefetch_bytes.fetch_sub(entry->second->size);
                    retired.push_back(std::move(entry->second)); entry = prefetched.erase(entry);
                }
                if (!next_target.empty() && prefetched.find(next_target) == prefetched.end()) target = next_target;
                ResetEvent(prefetch_stop);
            }
            retired.clear();
            if (target.empty()) continue;
            try {
                std::shared_ptr<Grant> grant;
                {
                    std::lock_guard<std::mutex> lock(grants_mutex);
                    const auto found = grants.find(target);
                    if (found == grants.end() || found->second->expires <= std::chrono::steady_clock::now()) continue;
                    grant = found->second;
                }
                // Der laufende Download hat Vorrang vor spekulativen Anfragen.
                while (active_downloads.load() && WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && WaitForSingleObject(prefetch_stop, 100) == WAIT_TIMEOUT) {}
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || WaitForSingleObject(prefetch_stop, 0) == WAIT_OBJECT_0) continue;
                prefetch_active.store(true); prefetch_error.store(0);
                HttpStream stream(grant->source.server, grant->source.token, grant->source.media_path, false, {}, stop, prefetch_stop);
                if (stream.status() != 200) throw HttpError(ERROR_BAD_NET_RESP);
                const auto length = stream.header(L"Content-Length"), encoding = stream.header(L"Content-Encoding");
                if (length.empty() || length.size() > 19 || length.find_first_not_of("0123456789") != length.npos ||
                    (!encoding.empty() && encoding != "identity")) throw HttpError(ERROR_INVALID_DATA);
                const auto size = std::stoull(length);
                if (!size || size > 32ULL * 1024 * 1024) throw HttpError(ERROR_FILE_TOO_LARGE);
                auto file = media_store->buffer(size);
                std::array<char, 65536> buffer{};
                unsigned long long received = 0;
                for (;;) {
                    const auto count = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
                    if (!count) break;
                    if (count > size - received) throw HttpError(ERROR_INVALID_DATA);
                    file->append(buffer.data(), count); received += count; bytes_received.fetch_add(count);
                }
                if (received != size) throw HttpError(ERROR_HANDLE_EOF);
                auto ready = std::make_shared<Prefetched>(Prefetched{std::move(file), size, stream.header(L"Content-Type")});
                {
                    std::lock_guard<std::mutex> lock(prefetch_mutex);
                    if (generation == prefetch_generation && WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
                        prefetched.emplace(target, std::move(ready)); prefetch_bytes.fetch_add(size);
                    }
                }
            } catch (const HttpError& failure) {
                prefetch_error.store(WaitForSingleObject(prefetch_stop, 0) == WAIT_OBJECT_0 || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 ? 0 : failure.code);
            }
              catch (...) { prefetch_error.store(ERROR_INVALID_DATA); }
            prefetch_active.store(false);
        }
      } catch (...) { prefetch_active.store(false); prefetch_error.store(ERROR_NOT_ENOUGH_MEMORY); }
    }

    bool serve_prefetched(SOCKET client, const std::string& target, const LocalRequest& request, bool& response_started) {
        std::shared_ptr<Prefetched> cached;
        {
            std::lock_guard<std::mutex> lock(prefetch_mutex);
            const auto found = prefetched.find(target);
            if (found == prefetched.end()) return false;
            cached = found->second;
        }
        auto first = 0ULL, last = cached->size - 1;
        if (!request.range.empty()) {
            const auto interval = request.range.substr(6);
            const auto separator = interval.find('-');
            const auto start = interval.substr(0, separator), end = interval.substr(separator + 1);
            if (start.empty()) first = cached->size - (std::min)(cached->size, std::stoull(end));
            else {
                first = std::stoull(start);
                if (!end.empty()) last = (std::min)(last, std::stoull(end));
            }
            if (first >= cached->size) { response_started = true; send_status(client, 416, stop); return true; }
        }
        std::string headers = request.range.empty() ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 206 Partial Content\r\n";
        headers += "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nAccept-Ranges: bytes\r\nContent-Length: " + std::to_string(last - first + 1) + "\r\n";
        if (!request.range.empty()) headers += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(cached->size) + "\r\n";
        if (!cached->content_type.empty()) headers += "Content-Type: " + cached->content_type + "\r\n";
        headers += "\r\n"; response_started = true; send_all(client, headers.data(), headers.size(), stop);
        ++prefetch_hits;
        if (request.head) return true;
        std::array<char, 65536> buffer{};
        while (first <= last) {
            const auto count = cached->file->read(first, buffer.data(), static_cast<DWORD>((std::min)(static_cast<unsigned long long>(buffer.size()), last - first + 1)));
            if (!count) throw HttpError(ERROR_HANDLE_EOF);
            send_all(client, buffer.data(), count, stop); first += count; bytes.fetch_add(count);
        }
        return true;
    }

    // Ein nachfolgender erfolgreicher Request darf den letzten Medienfehler
    // nicht aus der Diagnose löschen. Es werden ausschließlich Zahlen erfasst.
    void remember_failure(unsigned long status, unsigned long code, unsigned long long expected, unsigned long long transferred) {
        std::lock_guard<std::mutex> lock(failure_mutex);
        last_failure = {last_failure.count + 1, status, code, expected, transferred};
    }
    std::atomic<unsigned long long> bytes{0};

    std::string add(PlaybackSource source) {
        validate_token(source.token);
        validate_media_path(source.media_path);
        const auto extension = media_extension(source.media_path);
        const auto origin = std::string(source.server.secure ? "https://" : "http://") + source.server.host + ":" + std::to_string(source.server.port);
        source.server = parse_server_address(origin);
        const auto capability = "/stream/" + random_key() + "/audio" + extension;
        std::lock_guard<std::mutex> lock(grants_mutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto entry = grants.begin(); entry != grants.end();) {
            if (policy == ProxyClientPolicy::capability_only && entry->second->expires <= now) entry = grants.erase(entry); else ++entry;
        }
        if (grants.size() >= 10000) throw std::invalid_argument("grant_limit");
        grants.emplace(capability, std::make_shared<Grant>(Grant{std::move(source), now + lifetime}));
        return "http://" + authority + capability;
    }

    void serve(SOCKET client) {
        bool response_started = false;
        unsigned long response_status = 0;
        unsigned long long expected = 0, transferred = 0;
        std::atomic<unsigned long long> received{0};
        try {
            std::string request;
            std::array<char, 2048> chunk{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (request.find("\r\n\r\n") == request.npos) {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("local_timeout");
                if (!socket_ready(client, false, stop)) continue;
                const auto count = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
                if (count == SOCKET_ERROR && (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR)) continue;
                if (count <= 0) throw LocalDisconnect();
                request.append(chunk.data(), static_cast<std::size_t>(count));
                if (request.size() > 16384) throw std::invalid_argument("headers_limit");
            }
            const auto first_space = request.find(' ');
            const auto target_end = first_space == request.npos ? request.npos : request.find(' ', first_space + 1);
            if (target_end == request.npos) throw std::invalid_argument("request_line");
            const auto capability = request.substr(first_space + 1, target_end - first_space - 1);
            std::shared_ptr<Grant> grant;
            {
                std::lock_guard<std::mutex> lock(grants_mutex);
                const auto found = grants.find(capability);
                if (found == grants.end()) throw std::invalid_argument("request_target");
                grant = found->second;
            }
            if (std::chrono::steady_clock::now() >= grant->expires &&
                !(policy == ProxyClientPolicy::host_session && request_from_host(client))) {
                upstream.store(0); error.store(0); rejection.store("request_expired");
                send_status(client, 410, stop); return;
            }
            const auto parsed = parse_local_request(request, authority, capability);
            rejection.store(nullptr);
            if (serve_prefetched(client, capability, parsed, response_started)) return;
            if (!parsed.head && prefetch_active.load()) {
                std::lock_guard<std::mutex> lock(prefetch_mutex);
                SetEvent(prefetch_stop);
            }
            StreamBuffer body(stop);
            auto stream = std::make_shared<HttpStream>(grant->source.server, grant->source.token, grant->source.media_path, parsed.head, parsed.range, stop, body.cancellation());
            const auto status = stream->status();
            response_status = status;
            upstream.store(status); error.store(0);
            if (status != 200 && status != 206 && status != 416) {
                remember_failure(status, 0, 0, 0);
                send_status(client, status == 401 || status == 403 || status == 404 ? status : 502, stop); return;
            }
            if (status == 416) { send_status(client, 416, stop); return; }
            std::string headers = "HTTP/1.1 " + std::to_string(status) + " " +
                (status == 200 ? "OK" : status == 206 ? "Partial Content" : "Range Not Satisfiable") +
                "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
            // Ausschließlich Medienheader weiterreichen, niemals Cookies, Redirects oder Fehlermeldungen.
            for (const auto* name : {L"Content-Length", L"Content-Range", L"Content-Type", L"Accept-Ranges"}) {
                const auto value = stream->header(name);
                if (!value.empty()) {
                    for (const auto* character = name; *character; ++character) headers += static_cast<char>(*character);
                    headers += ": " + value + "\r\n";
                }
            }
            const auto encoding = stream->header(L"Content-Encoding");
            if (!encoding.empty() && encoding != "identity") throw HttpError(ERROR_INVALID_DATA);
            headers += "\r\n";
            if (parsed.head) {
                response_started = true;
                send_all(client, headers.data(), headers.size(), stop);
                return;
            }
            const auto length = stream->header(L"Content-Length");
            if (!length.empty()) {
                if (length.size() > 19 || length.find_first_not_of("0123456789") != length.npos) throw HttpError(ERROR_INVALID_DATA);
                expected = std::stoull(length);
            }
            const auto identity = stream->header(L"ETag");
            body.start(media_store->buffer(expected), [this, stream = std::move(stream), grant, status, identity, length, expected, &received](StreamBuffer& destination) mutable {
              struct ActiveDownload {
                  std::atomic<unsigned long long>& count;
                  explicit ActiveDownload(std::atomic<unsigned long long>& value) : count(value) { count.fetch_add(1); }
                  ~ActiveDownload() { count.fetch_sub(1); }
              } active(active_downloads);
              std::array<char, 65536> buffer{};
              unsigned reconnects = 0;
              unsigned long long downloaded = 0;
              while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && WaitForSingleObject(destination.cancellation(), 0) != WAIT_OBJECT_0) {
                unsigned long count = 0;
                try {
                    count = stream->read(buffer.data(), static_cast<DWORD>(buffer.size()));
                    if (!count && !length.empty() && downloaded < expected) throw HttpError(ERROR_HANDLE_EOF);
                } catch (const HttpError& failure) {
                    // Ausschließlich dieselbe unveränderte Datei bytegenau fortsetzen.
                    // Ohne starken ETag, bekannte Länge oder bei Stop bleibt es ein Fehler.
                    if (status != 200 || identity.size() < 2 || identity.front() != '"' || identity.back() != '"' ||
                        length.empty() || !downloaded || downloaded >= expected || reconnects >= 3 ||
                        WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || WaitForSingleObject(destination.cancellation(), 0) == WAIT_OBJECT_0 ||
                        (failure.code != ERROR_HANDLE_EOF && failure.code != ERROR_WINHTTP_TIMEOUT &&
                         failure.code != ERROR_TIMEOUT && failure.code != ERROR_WINHTTP_CONNECTION_ERROR)) throw;
                    remember_failure(status, failure.code, expected, downloaded);
                    ++reconnects;
                    stream.reset();
                    const auto range = "bytes=" + std::to_string(downloaded) + "-" + std::to_string(expected - 1);
                    stream = std::make_shared<HttpStream>(grant->source.server, grant->source.token, grant->source.media_path, false, range, stop, destination.cancellation());
                    const auto content_range = "bytes " + std::to_string(downloaded) + "-" + std::to_string(expected - 1) + "/" + std::to_string(expected);
                    if (stream->status() != 206 || stream->header(L"ETag") != identity ||
                        stream->header(L"Content-Range") != content_range ||
                        stream->header(L"Content-Length") != std::to_string(expected - downloaded) ||
                        (!stream->header(L"Content-Encoding").empty() && stream->header(L"Content-Encoding") != "identity"))
                        throw HttpError(ERROR_INVALID_DATA);
                    continue;
                }
                if (!count) break;
                if (!length.empty() && count > expected - downloaded) throw HttpError(ERROR_INVALID_DATA);
                destination.append(buffer.data(), count);
                downloaded += count; received.store(downloaded); bytes_received.fetch_add(count);
              }
              if (!length.empty() && downloaded != expected && WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 &&
                  WaitForSingleObject(destination.cancellation(), 0) != WAIT_OBJECT_0) throw HttpError(ERROR_HANDLE_EOF);
              stream.reset();
            });
            response_started = true;
            send_all(client, headers.data(), headers.size(), stop);
            std::array<char, 65536> buffer{};
            for (;;) {
                const auto count = body.read(buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!count) {
                    char next = 0;
                    const auto pending = recv(client, &next, 1, MSG_PEEK);
                    if (!pending || (pending == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR)) throw LocalDisconnect();
                    continue;
                }
                if (!*count) break;
                send_all(client, buffer.data(), *count, stop);
                bytes.fetch_add(*count);
                transferred += *count;
            }
        } catch (const std::invalid_argument& failure) {
            // Nur feste Diagnosecodes veröffentlichen; fremde Texte bleiben intern.
            const char* code = "local_request";
            for (const auto* candidate : {"request_headers", "request_line", "request_target", "header_line",
                 "header_name", "header_value", "request_host", "duplicate_range", "request_body_or_origin",
                 "request_cross_site", "missing_host", "range_unit", "range_interval", "range_value", "range_order", "range_suffix"})
                if (std::string_view(failure.what()) == candidate) { code = candidate; break; }
            rejection.store(code);
            if (!response_started) send_status(client, 403, stop);
        } catch (const HttpError& failure) {
            error.store(failure.code);
            if (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)
                remember_failure(response_status, failure.code, expected, received.load());
            if (!response_started) send_status(client, 502, stop);
        }
    }

    void run() noexcept {
        while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
            fd_set ready;
            FD_ZERO(&ready); FD_SET(listener, &ready);
            timeval timeout{0, 100000};
            if (select(0, &ready, nullptr, nullptr, &timeout) <= 0) continue;
            const auto client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            u_long nonblocking = 1;
            if (ioctlsocket(client, FIONBIO, &nonblocking)) {
                error.store(WSAGetLastError()); closesocket(client); continue;
            }
            // Decoder schließen beim Seeking alte Requests. Das ist kein PMS-Fehler.
            try { serve(client); } catch (const LocalDisconnect&) { local_disconnects.fetch_add(1); } catch (...) { error.store(ERROR_CONNECTION_ABORTED); }
            shutdown(client, SD_BOTH); closesocket(client);
        }
    }

    ~State() {
        {
            std::lock_guard<std::mutex> lock(prefetch_mutex);
            if (stop) SetEvent(stop);
            if (prefetch_stop) SetEvent(prefetch_stop);
        }
        prefetch_changed.notify_one();
        if (prefetch_worker.joinable()) prefetch_worker.join();
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        if (listener != INVALID_SOCKET) closesocket(listener);
        if (stop) CloseHandle(stop);
        if (prefetch_stop) CloseHandle(prefetch_stop);
        if (sockets_started) WSACleanup();
    }
};

PlaybackProxy::PlaybackProxy(PlaybackSource source, std::chrono::milliseconds lifetime, ProxyClientPolicy policy,
                             std::shared_ptr<LocalMediaStore> media_store) : state_(std::make_unique<State>()) {
    auto& state = *state_;
    if (lifetime <= std::chrono::milliseconds::zero() || lifetime > std::chrono::hours(2)) throw std::invalid_argument("grant_lifetime");
    state.lifetime = lifetime;
    if (policy != ProxyClientPolicy::capability_only && policy != ProxyClientPolicy::host_session) throw std::invalid_argument("client_policy");
    state.policy = policy;
    state.media_store = media_store ? std::move(media_store) : std::make_shared<LocalMediaStore>(settings_path().parent_path() / L"playback");
    // Auch Aufrufer außerhalb der UI müssen die Origin-Sicherheitsregeln einhalten.
    if (!state.stop || !state.prefetch_stop) throw std::runtime_error("stop_event");
    WSADATA socket_data{};
    if (WSAStartup(MAKEWORD(2, 2), &socket_data) != 0) throw std::runtime_error("socket_startup");
    state.sockets_started = true;
    state.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (state.listener == INVALID_SOCKET) throw std::runtime_error("socket_create");
    BOOL exclusive = TRUE;
    if (setsockopt(state.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0)
        throw std::runtime_error("socket_exclusive");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(state.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(state.listener, 8) != 0)
        throw std::runtime_error("socket_bind");
    int length = sizeof(address);
    if (getsockname(state.listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        throw std::runtime_error("socket_port");
    u_long nonblocking = 1;
    if (ioctlsocket(state.listener, FIONBIO, &nonblocking) != 0) throw std::runtime_error("socket_mode");
    state.authority = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
    state.local_url = state.add(std::move(source));
    for (auto& worker : state.workers) worker = std::thread([&state] { state.run(); });
    state.prefetch_worker = std::thread([&state] { state.run_prefetch(); });
}

PlaybackProxy::~PlaybackProxy() = default;
const std::string& PlaybackProxy::url() const { return state_->local_url; }
std::string PlaybackProxy::add(PlaybackSource source) { return state_->add(std::move(source)); }
void PlaybackProxy::revoke(const std::string& url) {
    const auto origin = "http://" + state_->authority;
    if (url.substr(0, origin.size()) != origin) throw std::invalid_argument("grant_origin");
    std::lock_guard<std::mutex> lock(state_->grants_mutex);
    state_->grants.erase(url.substr(origin.size()));
}
ProxyStatus PlaybackProxy::status() const {
    std::lock_guard<std::mutex> lock(state_->failure_mutex);
    return {state_->upstream.load(), state_->error.load(), state_->bytes.load(), state_->rejection.load(), state_->last_failure, state_->local_disconnects.load(),
            state_->bytes_received.load(), state_->active_downloads.load(), state_->media_store->bytes_used(),
            state_->prefetch_bytes.load(), state_->prefetch_hits.load(), state_->prefetch_error.load(), state_->prefetch_active.load()};
}
void PlaybackProxy::prefetch(const std::string& current_url, const std::string& next_url) {
    const auto origin = "http://" + state_->authority;
    const auto own = [&](const std::string& url) {
        if (url.compare(0, origin.size(), origin) != 0) return std::string{};
        const auto path = url.substr(origin.size());
        std::lock_guard<std::mutex> lock(state_->grants_mutex);
        return state_->grants.find(path) == state_->grants.end() ? std::string{} : path;
    };
    state_->plan_prefetch(own(current_url), own(next_url));
}
}
