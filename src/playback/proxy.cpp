#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <bcrypt.h>
#include "plex/proxy.h"
#include "plex/http_stream.h"
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstddef>

namespace plex {
namespace {
struct LocalDisconnect : std::runtime_error { LocalDisconnect() : std::runtime_error("local_disconnect") {} };
void send_all(SOCKET client, const char* buffer, std::size_t size) {
    while (size) {
        const auto sent = send(client, buffer, static_cast<int>(size), 0);
        if (sent <= 0) throw LocalDisconnect();
        buffer += sent; size -= static_cast<std::size_t>(sent);
    }
}

void send_status(SOCKET client, unsigned status) {
    const auto response = "HTTP/1.1 " + std::to_string(status) + " Error\r\nContent-Length: 0\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(client, response.data(), response.size());
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
        DWORD timeout = 500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        bool response_started = false;
        try {
            std::string request;
            std::array<char, 2048> chunk{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (request.find("\r\n\r\n") == request.npos) {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("local_timeout");
                const auto count = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
                if (count == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) continue;
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
                send_status(client, 410); return;
            }
            const auto parsed = parse_local_request(request, authority, capability);
            rejection.store(nullptr);
            HttpStream stream(grant->source.server, grant->source.token, grant->source.media_path, parsed.head, parsed.range, stop);
            const auto status = stream.status();
            upstream.store(status); error.store(0);
            if (status != 200 && status != 206 && status != 416) {
                send_status(client, status == 401 || status == 403 || status == 404 ? status : 502); return;
            }
            if (status == 416) { send_status(client, 416); return; }
            std::string headers = "HTTP/1.1 " + std::to_string(status) + " " +
                (status == 200 ? "OK" : status == 206 ? "Partial Content" : "Range Not Satisfiable") +
                "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
            // Ausschließlich Medienheader weiterreichen, niemals Cookies, Redirects oder Fehlermeldungen.
            for (const auto* name : {L"Content-Length", L"Content-Range", L"Content-Type", L"Accept-Ranges"}) {
                const auto value = stream.header(name);
                if (!value.empty()) {
                    for (const auto* character = name; *character; ++character) headers += static_cast<char>(*character);
                    headers += ": " + value + "\r\n";
                }
            }
            const auto encoding = stream.header(L"Content-Encoding");
            if (!encoding.empty() && encoding != "identity") throw HttpError(ERROR_INVALID_DATA);
            headers += "\r\n";
            response_started = true;
            send_all(client, headers.data(), headers.size());
            if (parsed.head) return;
            const auto length = stream.header(L"Content-Length");
            unsigned long long expected = 0, transferred = 0;
            if (!length.empty()) {
                if (length.size() > 19 || length.find_first_not_of("0123456789") != length.npos) throw HttpError(ERROR_INVALID_DATA);
                expected = std::stoull(length);
            }
            std::array<char, 65536> buffer{};
            while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
                const auto count = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!count) break;
                send_all(client, buffer.data(), count);
                bytes.fetch_add(count);
                transferred += count;
            }
            if (!length.empty() && transferred != expected && WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)
                throw HttpError(ERROR_HANDLE_EOF);
        } catch (const std::invalid_argument& failure) {
            // Nur feste Diagnosecodes veröffentlichen; fremde Texte bleiben intern.
            const char* code = "local_request";
            for (const auto* candidate : {"request_headers", "request_line", "request_target", "header_line",
                 "header_name", "header_value", "request_host", "duplicate_range", "request_body_or_origin",
                 "request_cross_site", "missing_host", "range_unit", "range_interval", "range_value", "range_order", "range_suffix"})
                if (std::string_view(failure.what()) == candidate) { code = candidate; break; }
            rejection.store(code);
            if (!response_started) send_status(client, 403);
        } catch (const HttpError& failure) {
            error.store(failure.code);
            if (!response_started) send_status(client, 502);
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
            u_long blocking = 0;
            ioctlsocket(client, FIONBIO, &blocking);
            // Decoder schließen beim Seeking alte Requests. Das ist kein PMS-Fehler.
            try { serve(client); } catch (const LocalDisconnect&) {} catch (...) { error.store(ERROR_CONNECTION_ABORTED); }
            shutdown(client, SD_BOTH); closesocket(client);
        }
    }

    ~State() {
        if (stop) SetEvent(stop);
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        if (listener != INVALID_SOCKET) closesocket(listener);
        if (stop) CloseHandle(stop);
        if (sockets_started) WSACleanup();
    }
};

PlaybackProxy::PlaybackProxy(PlaybackSource source, std::chrono::milliseconds lifetime, ProxyClientPolicy policy) : state_(std::make_unique<State>()) {
    auto& state = *state_;
    if (lifetime <= std::chrono::milliseconds::zero() || lifetime > std::chrono::hours(2)) throw std::invalid_argument("grant_lifetime");
    state.lifetime = lifetime;
    if (policy != ProxyClientPolicy::capability_only && policy != ProxyClientPolicy::host_session) throw std::invalid_argument("client_policy");
    state.policy = policy;
    // Auch Aufrufer außerhalb der UI müssen die Origin-Sicherheitsregeln einhalten.
    if (!state.stop) throw std::runtime_error("stop_event");
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
ProxyStatus PlaybackProxy::status() const { return {state_->upstream.load(), state_->error.load(), state_->bytes.load(), state_->rejection.load()}; }
}
