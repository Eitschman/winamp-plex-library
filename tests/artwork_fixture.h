#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <string>
#include <vector>
#include <stdexcept>

// Ausschließlich synthetische BMP-Daten auf Loopback, ohne echte Zugangsdaten.
class ArtworkFixture {
public:
    std::atomic<unsigned> requests{0};
    std::atomic<bool> valid{true};
    explicit ArtworkFixture(const std::vector<std::uint8_t>& bitmap) : bitmap_(bitmap) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("artwork_fixture_wsa");
        try {
            listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int size = sizeof(address);
            if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&address), size) ||
                getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) || listen(listener_, 4))
                throw std::runtime_error("artwork_fixture_listen");
            port_ = ntohs(address.sin_port);
            worker_ = std::thread([this] { run(); });
        } catch (...) { if (listener_ != INVALID_SOCKET) closesocket(listener_); WSACleanup(); throw; }
    }
    ~ArtworkFixture() { stop_.store(true); if (worker_.joinable()) worker_.join(); closesocket(listener_); WSACleanup(); }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port_); }
private:
    void run() noexcept {
        SOCKET client = INVALID_SOCKET;
        try {
            while (!stop_.load()) {
                fd_set readable; FD_ZERO(&readable); FD_SET(listener_, &readable);
                timeval timeout{0, 100000};
                const auto ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready < 0) throw std::runtime_error("artwork_fixture_select");
                if (!ready) continue;
                client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) throw std::runtime_error("artwork_fixture_accept");
                DWORD wait = 1000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&wait), sizeof(wait));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&wait), sizeof(wait));
                std::string request;
                while (request.find("\r\n\r\n") == request.npos && request.size() < 16384) {
                    char buffer[1024]; const auto count = recv(client, buffer, sizeof(buffer), 0);
                    if (count <= 0) throw std::runtime_error("artwork_fixture_request");
                    request.append(buffer, count);
                }
                if (request.find("GET /library/metadata/42/thumb HTTP/1.1\r\n") != 0 || request.find("\r\n\r\n") == request.npos)
                    throw std::runtime_error("artwork_fixture_path");
                ++requests;
                std::string reply = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: image/bmp\r\nContent-Length: " + std::to_string(bitmap_.size()) + "\r\n\r\n";
                reply.append(reinterpret_cast<const char*>(bitmap_.data()), bitmap_.size());
                std::size_t sent = 0;
                while (sent < reply.size()) {
                    const auto count = send(client, reply.data() + sent, static_cast<int>(reply.size() - sent), 0);
                    if (count <= 0) throw std::runtime_error("artwork_fixture_reply");
                    sent += count;
                }
                closesocket(client); client = INVALID_SOCKET;
            }
        } catch (...) { valid.store(false); if (client != INVALID_SOCKET) closesocket(client); }
    }
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::vector<std::uint8_t> bitmap_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};
