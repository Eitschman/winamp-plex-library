#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "plex/proxy.h"
#include "plex/http_stream.h"
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace plex {
namespace {
void send_all(SOCKET client, const char* buffer, std::size_t size) {
    while (size) {
        const auto sent = send(client, buffer, static_cast<int>(size), 0);
        if (sent <= 0) throw std::runtime_error("local_socket");
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
}

PlaybackSource::~PlaybackSource() {
    if (!token.empty()) SecureZeroMemory(token.data(), token.size());
}

struct PlaybackProxy::State {
    PlaybackSource source;
    SOCKET listener = INVALID_SOCKET;
    bool sockets_started = false;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::array<std::thread, 4> workers;
    std::string authority;
    std::string capability;
    std::string local_url;
    std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::now() + std::chrono::hours(2);
    std::atomic<DWORD> upstream{0};
    std::atomic<DWORD> error{0};
    std::atomic<unsigned long long> bytes{0};

    explicit State(PlaybackSource value) : source(std::move(value)) {}

    void serve(SOCKET client) {
        DWORD timeout = 500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        bool response_started = false;
        try {
            if (std::chrono::steady_clock::now() >= expires) { send_status(client, 410); return; }
            std::string request;
            std::array<char, 2048> chunk{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (request.find("\r\n\r\n") == request.npos) {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 || std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("local_timeout");
                const auto count = recv(client, chunk.data(), static_cast<int>(chunk.size()), 0);
                if (count == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) continue;
                if (count <= 0) throw std::runtime_error("local_socket");
                request.append(chunk.data(), static_cast<std::size_t>(count));
                if (request.size() > 16384) throw std::invalid_argument("headers_limit");
            }
            const auto parsed = parse_local_request(request, authority, capability);
            HttpStream stream(source.server, source.token, source.media_path, parsed.head, parsed.range, stop);
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
            std::array<char, 65536> buffer{};
            while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
                const auto count = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
                if (!count) break;
                send_all(client, buffer.data(), count);
                bytes.fetch_add(count);
            }
        } catch (const std::invalid_argument&) {
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
            try { serve(client); } catch (...) { error.store(ERROR_CONNECTION_ABORTED); }
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

PlaybackProxy::PlaybackProxy(PlaybackSource source) : state_(std::make_unique<State>(std::move(source))) {
    auto& state = *state_;
    validate_token(state.source.token);
    validate_media_path(state.source.media_path);
    const auto extension = media_extension(state.source.media_path);
    // Auch Aufrufer außerhalb der UI müssen die Origin-Sicherheitsregeln einhalten.
    const auto origin = std::string(state.source.server.secure ? "https://" : "http://") +
                        state.source.server.host + ":" + std::to_string(state.source.server.port);
    state.source.server = parse_server_address(origin);
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
    state.capability = "/stream/" + random_key() + "/audio" + extension;
    state.local_url = "http://" + state.authority + state.capability;
    for (auto& worker : state.workers) worker = std::thread([&state] { state.run(); });
}

PlaybackProxy::~PlaybackProxy() = default;
const std::string& PlaybackProxy::url() const { return state_->local_url; }
ProxyStatus PlaybackProxy::status() const { return {state_->upstream.load(), state_->error.load(), state_->bytes.load()}; }
}
