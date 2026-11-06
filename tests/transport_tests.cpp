#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include "plex/proxy.h"
#include "plex/security.h"
#include "plex/artwork.h"
#include "plex/http_stream.h"
#include "plex/local_media.h"
#include "plex/stream_buffer.h"
#include <fstream>
#include <aclapi.h>
#include <objbase.h>
#include <atomic>
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int checks = 0;
void check(bool condition, const char* name) {
    ++checks;
    if (!condition) throw std::runtime_error(name);
}
void send_text(SOCKET socket, const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto sent = send(socket, text.data() + offset, static_cast<int>(text.size() - offset), 0);
        if (sent <= 0) return;
        offset += sent;
    }
}

struct Fixture {
    SOCKET listener = INVALID_SOCKET;
    std::uint16_t port = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> slow_started{false};
    std::atomic<unsigned> hits{0};
    std::atomic<unsigned> token_errors{0};
    std::string media;
    std::thread worker;

    explicit Fixture(std::size_t media_size = 100000) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("fixture_winsock");
        media.resize(media_size);
        for (std::size_t offset = 0; offset < media.size(); ++offset) media[offset] = static_cast<char>(offset % 251);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "fixture_bind");
        check(listen(listener, 8) == 0, "fixture_listen");
        int length = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
        u_long nonblocking = 1; ioctlsocket(listener, FIONBIO, &nonblocking);
        worker = std::thread([this] { run(); });
    }

    void serve(SOCKET client) {
        DWORD timeout = 500;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::string request;
        char chunk[2048];
        while (request.find("\r\n\r\n") == request.npos && request.size() < 16384) {
            const auto size = recv(client, chunk, sizeof(chunk), 0);
            if (size <= 0) return;
            request.append(chunk, static_cast<std::size_t>(size));
        }
        ++hits;
        if (request.find("X-Plex-Token: synthetic-test-token\r\n") == request.npos) {
            ++token_errors; send_text(client, "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
        }
        if (request.find(" /feedback-method-probe ") != request.npos) {
            const bool valid = (request.rfind("PUT ", 0) == 0 || request.rfind("POST ", 0) == 0) &&
                request.find("X-Plex-Client-Identifier: synthetic-client\r\n") != request.npos;
            if (!valid) ++token_errors;
            send_text(client, valid ? "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" : "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (request.find("/library/metadata/") != request.npos) {
            if (request.find("Accept: image/*\r\n") == request.npos) { ++token_errors; return; }
            for (const auto status : {401, 403, 404, 302, 500}) {
                if (request.find("/library/metadata/" + std::to_string(status) + "/thumb") != request.npos) {
                    send_text(client, "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Length: 0\r\nLocation: http://127.0.0.1:1/foreign\r\n\r\n"); return;
                }
            }
            std::string bitmap(62, '\0'); bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 62; bitmap[10] = 54;
            bitmap[14] = 40; bitmap[18] = 2; bitmap[22] = 1; bitmap[26] = 1; bitmap[28] = 24;
            bitmap[56] = static_cast<char>(255); bitmap[58] = static_cast<char>(255);
            const auto type = request.find("/metadata/2/thumb") == request.npos ? "image/bmp" : "text/html";
            send_text(client, std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type + "\r\nContent-Length: 62\r\n\r\n" + bitmap);
            return;
        }
        if (request.find("/library/parts/997/") != request.npos) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\npartial");
            slow_started.store(true);
            while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return;
        }
        if (request.find("/library/parts/989/") != request.npos) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\n" + media.substr(0, 65536));
            slow_started.store(true);
            while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return;
        }
        if (request.find("/library/parts/998/") != request.npos) {
            slow_started.store(true);
            while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return;
        }
        const bool resume_fixture = request.find("/library/parts/995/") != request.npos || request.find("/library/parts/994/") != request.npos ||
            request.find("/library/parts/993/") != request.npos || request.find("/library/parts/992/") != request.npos ||
            request.find("/library/parts/991/") != request.npos || request.find("/library/parts/990/") != request.npos;
        if (resume_fixture) {
            const bool repeated = request.find("/library/parts/990/") != request.npos;
            const auto range = request.find("Range: bytes=");
            if (range == request.npos) {
                send_text(client, "HTTP/1.1 200 OK\r\nETag: \"fixture\"\r\nContent-Length: " + std::to_string(media.size()) + "\r\n\r\n" + media.substr(0, repeated ? 1 : 32768));
            } else {
                const auto first = static_cast<std::size_t>(std::stoull(request.substr(range + 13)));
                if (first >= media.size()) throw std::runtime_error("resume_range_outside_fixture");
                const bool changed = request.find("/library/parts/994/") != request.npos;
                const bool wrong_range = request.find("/library/parts/993/") != request.npos;
                const bool ignored_range = request.find("/library/parts/992/") != request.npos;
                const bool wrong_length = request.find("/library/parts/991/") != request.npos;
                send_text(client, std::string(ignored_range ? "HTTP/1.1 200 OK" : "HTTP/1.1 206 Partial Content") + "\r\nETag: \"" + std::string(changed ? "changed" : "fixture") + "\"\r\nContent-Length: " + std::to_string(media.size() - first + (wrong_length ? 1 : 0)) +
                    "\r\nContent-Range: bytes " + std::to_string(first + (wrong_range ? 1 : 0)) + "-" + std::to_string(media.size() - 1) + "/" + std::to_string(media.size()) + "\r\n\r\n" + media.substr(first, repeated ? 1 : media.size()));
            }
            return;
        }
        if (request.find("/library/parts/996/") != request.npos) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(media.size()) + "\r\n\r\n" + media.substr(0, 32768));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (!stop.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.load()) send_text(client, media.substr(32768));
            return;
        }
        for (const auto status : {401, 403, 404, 302, 500}) {
            if (request.find("/library/parts/" + std::to_string(status) + "/") != request.npos) {
                const auto response = "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Length: 0\r\n"
                                      "Location: http://127.0.0.1:1/foreign\r\nSet-Cookie: private=test\r\n\r\n";
                send_text(client, response); return;
            }
        }
        if (request.find("/library/parts/999/") != request.npos) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\ntruncated"); return;
        }
        if (request.find("/library/parts/988/") != request.npos) {
            send_text(client, "HTTP/1.1 200 OK\r\nContent-Length: 33554433\r\n\r\n"); return;
        }
        std::size_t first = 0, last = media.size() - 1;
        bool ranged = false;
        auto range = request.find("Range: bytes=");
        if (range != request.npos) {
            ranged = true;
            range += 13;
            const auto end = request.find("\r\n", range);
            const auto interval = request.substr(range, end - range);
            const auto dash = interval.find('-');
            if (dash == 0) first = media.size() - static_cast<std::size_t>(std::stoull(interval.substr(1)));
            else {
                first = static_cast<std::size_t>(std::stoull(interval.substr(0, dash)));
                if (dash + 1 < interval.size()) last = static_cast<std::size_t>(std::stoull(interval.substr(dash + 1)));
            }
            if (first >= media.size()) { send_text(client, "HTTP/1.1 416 Test\r\nContent-Length: 0\r\n\r\n"); return; }
        }
        const auto body = media.substr(first, last - first + 1);
        auto response = std::string(ranged ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n") +
                        "Content-Type: audio/mpeg\r\nAccept-Ranges: bytes\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        if (ranged) response += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(media.size()) + "\r\n";
        response += "\r\n";
        if (request.substr(0, 4) != "HEAD") response += body;
        send_text(client, response);
    }

    void run() noexcept {
        while (!stop.load()) {
            const auto client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            u_long blocking = 0; ioctlsocket(client, FIONBIO, &blocking);
            try { serve(client); } catch (...) { ++token_errors; }
            closesocket(client);
        }
    }

    ~Fixture() {
        stop.store(true);
        if (worker.joinable()) worker.join();
        closesocket(listener); WSACleanup();
    }
};

struct InternetHandle {
    HINTERNET value;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
struct LocalSocket {
    SOCKET value = INVALID_SOCKET;
    ~LocalSocket() { if (value != INVALID_SOCKET) closesocket(value); }
};

// Ein Decoder darf den TCP-Empfang bei gefülltem Audiopuffer pausieren.
// Ein kleiner Empfangspuffer erzwingt Rückstau ohne langen Audiotest.
void slow_decoder_checks() {
    Fixture origin(4 * 1024 * 1024);
    const auto server = plex::parse_server_address("http://127.0.0.1:" + std::to_string(origin.port));
    auto proxy = std::make_unique<plex::PlaybackProxy>(plex::PlaybackSource{server, "synthetic-test-token", "/library/parts/1/file.mp3"});
    const auto url = proxy->url();
    const auto path = url.find('/', 7);
    const auto port = std::stoi(url.substr(url.find(':', 7) + 1, path - url.find(':', 7) - 1));
    const auto connect_decoder = [&](bool complete_headers = true, const std::string& range = std::string{}) {
        const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET) throw std::runtime_error("slow_client_socket");
        const int buffer = 4096;
        setsockopt(client, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        DWORD timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<u_short>(port));
        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
            closesocket(client); throw std::runtime_error("slow_client_connect");
        }
        send_text(client, complete_headers ? "GET " + url.substr(path) + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n" +
            (range.empty() ? std::string{} : "Range: " + range + "\r\n") + "\r\n" : "GET ");
        return client;
    };
    {
        LocalSocket client{connect_decoder()};
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto buffered = proxy->status();
        check(buffered.bytes_received == origin.media.size() && buffered.bytes_sent < origin.media.size(),
              "complete_upstream_is_buffered_while_decoder_does_not_read");
        check(buffered.active_downloads == 0 && buffered.reserved_media_bytes == origin.media.size(),
              "completed_download_releases_network_but_retains_bounded_media_buffer");
        const int buffer = 256 * 1024;
        setsockopt(client.value, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        std::string response;
        std::array<char, 65536> chunk{};
        for (;;) {
            const auto received = recv(client.value, chunk.data(), static_cast<int>(chunk.size()), 0);
            if (received <= 0) break;
            response.append(chunk.data(), static_cast<std::size_t>(received));
        }
        const auto header = response.find("\r\n\r\n");
        check(header != response.npos && response.substr(header + 4) == origin.media, "decoder_backpressure_does_not_truncate_media");
    }
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (proxy->status().reserved_media_bytes && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(proxy->status().reserved_media_bytes == 0, "completed_decoder_request_releases_buffer_reservation");
    }
    {
        const auto before = proxy->status().bytes_received;
        LocalSocket client{connect_decoder(true, "bytes=131072-")};
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const auto buffered = proxy->status();
        check(buffered.bytes_received - before == origin.media.size() - 131072 && buffered.active_downloads == 0,
              "seeking_response_downloads_independently_of_paused_decoder");
        const int buffer = 256 * 1024;
        setsockopt(client.value, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
        std::string response;
        std::array<char, 65536> chunk{};
        for (;;) {
            const auto received = recv(client.value, chunk.data(), static_cast<int>(chunk.size()), 0);
            if (received <= 0) break;
            response.append(chunk.data(), static_cast<std::size_t>(received));
        }
        const auto header = response.find("\r\n\r\n");
        check(response.rfind("HTTP/1.1 206 ", 0) == 0 && header != response.npos && response.substr(header + 4) == origin.media.substr(131072),
              "buffered_seeking_response_preserves_status_offset_and_every_byte");
    }
    {
        const auto hits = origin.hits.load();
        LocalSocket client{connect_decoder(false)};
        DWORD timeout = 5000;
        setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        const auto started = std::chrono::steady_clock::now();
        char marker = 0;
        const auto received = recv(client.value, &marker, 1, 0);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        check(received == 0 && elapsed >= std::chrono::milliseconds(2500) && elapsed < std::chrono::milliseconds(4500) &&
              origin.hits.load() == hits, "incomplete_headers_still_expire_without_upstream");
    }
    {
        const auto transferred = proxy->status().bytes_sent;
        LocalSocket client{connect_decoder()};
        char marker = 0;
        check(recv(client.value, &marker, 1, 0) == 1 && marker == 'H', "backpressured_decoder_response_started");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(proxy->status().bytes_sent < transferred + origin.media.size(), "decoder_is_backpressured_before_shutdown");
        // Eine fehlende Abbruchbehandlung muss als Fehler enden, nicht den
        // Testprozess dauerhaft festhalten. Der Wächter löst nur den Testclient.
        std::promise<void> finished;
        auto completion = finished.get_future();
        std::thread watchdog([&] {
            if (completion.wait_for(std::chrono::milliseconds(1500)) == std::future_status::timeout)
                shutdown(client.value, SD_BOTH);
        });
        const auto started = std::chrono::steady_clock::now();
        proxy.reset();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        finished.set_value(); watchdog.join();
        check(elapsed < std::chrono::milliseconds(1500), "backpressured_decoder_does_not_block_shutdown");
    }
}

// Eine geschlossene Decoderverbindung muss einen noch laufenden Plex-Abruf
// abbrechen, bevor dessen Empfangsfrist abläuft. Plugin-Ende prüft denselben Fall.
void buffer_disconnect_checks() {
    for (const bool decoder_close : {true, false}) {
        Fixture origin;
        const auto server = plex::parse_server_address("http://127.0.0.1:" + std::to_string(origin.port));
        auto proxy = std::make_unique<plex::PlaybackProxy>(plex::PlaybackSource{server, "synthetic-test-token", "/library/parts/989/file.mp3"});
        const auto url = proxy->url();
        const auto path = url.find('/', 7);
        const auto port = std::stoi(url.substr(url.find(':', 7) + 1, path - url.find(':', 7) - 1));
        LocalSocket client{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        check(client.value != INVALID_SOCKET, "buffer_cancel_client_socket");
        DWORD timeout = 3000;
        setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<u_short>(port));
        check(connect(client.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "buffer_cancel_client_connect");
        send_text(client.value, "GET " + url.substr(path) + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n\r\n");
        char marker = 0;
        check(recv(client.value, &marker, 1, 0) == 1 && marker == 'H', "buffer_cancel_response_started");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while ((!proxy->status().active_downloads || proxy->status().bytes_received != 65536) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(origin.slow_started.load() && proxy->status().active_downloads == 1 && proxy->status().bytes_received == 65536,
              "buffer_cancel_upstream_is_stalled_with_partial_data");
        const auto started = std::chrono::steady_clock::now();
        if (decoder_close) {
            shutdown(client.value, SD_BOTH); closesocket(client.value); client.value = INVALID_SOCKET;
            while (proxy->status().reserved_media_bytes && std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto status = proxy->status();
            check(status.active_downloads == 0 && status.reserved_media_bytes == 0 && status.local_disconnects == 1 && status.last_failure.count == 0,
                  "decoder_disconnect_cancels_download_and_releases_buffer_without_source_error");
        } else {
            proxy.reset();
            check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500),
                  "plugin_shutdown_cancels_download_while_buffer_waits_for_startup_bytes");
        }
    }
}

// Kleine Dateibudgets provozieren dieselben Grenzfälle ohne große Testdateien.
void stream_buffer_checks() {
    const auto root = std::filesystem::temp_directory_path() / (L"WinampPlexBufferTests-" + std::to_wstring(GetCurrentProcessId()));
    check(!std::filesystem::exists(root), "stream_buffer_test_directory_is_new");
    {
        plex::LocalMediaStore store(root, {16, 24});
        auto file = store.buffer(16);
        check(store.bytes_used() == 16, "buffer_reserves_known_length_before_transfer");
        file->append("abcdefgh", 8);
        std::array<char, 32> bytes{};
        check(file->read(2, bytes.data(), 4) == 4 && std::string(bytes.data(), 4) == "cdef", "buffer_reads_exact_published_offsets");
        bool rejected = false;
        try { file->read(9, bytes.data(), 1); } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_INVALID_DATA; }
        check(rejected, "buffer_rejects_unwritten_offsets");
        rejected = false;
        try { file->append("012345678", 9); } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_FILE_TOO_LARGE; }
        check(rejected && store.bytes_used() == 16, "buffer_cannot_exceed_per_file_reservation");
        rejected = false;
        try { store.buffer(9); } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_DISK_FULL; }
        check(rejected && store.bytes_used() == 16, "concurrent_buffers_share_total_quota");
        rejected = false;
        try { store.buffer(17); } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_FILE_TOO_LARGE; }
        check(rejected, "oversized_buffer_rejected_before_download");
        auto second = store.buffer();
        second->append("12345678", 8);
        rejected = false;
        try { second->append("9", 1); } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_DISK_FULL; }
        check(rejected && store.bytes_used() == 24, "unknown_length_buffer_grows_only_within_shared_quota");
        const auto directory = store.directory();
        PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr;
        check(GetNamedSecurityInfoW(const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
              nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS, "buffer_private_acl_readable");
        SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
        const bool private_acl = GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED) && acl && acl->AceCount == 2;
        LocalFree(descriptor);
        check(private_acl, "buffer_directory_is_private_to_user_and_system");
        file.reset(); second.reset();
        check(store.bytes_used() == 0, "released_buffers_return_all_reserved_bytes");
        unsigned files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.path().extension() == L".buffer") ++files;
        check(files == 0, "released_buffers_delete_partial_files");
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(stop != nullptr, "buffer_stop_event");
        {
            plex::StreamBuffer body(stop);
            body.start(store.buffer(8), [](plex::StreamBuffer& destination) {
                destination.append("abcdefgh", 8);
                throw plex::HttpError(ERROR_CONNECTION_ABORTED);
            });
            std::string result;
            rejected = false;
            try {
                for (;;) {
                    const auto count = body.read(bytes.data(), static_cast<DWORD>(bytes.size()));
                    if (!count) continue;
                    if (!*count) break;
                    result.append(bytes.data(), *count);
                }
            } catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_CONNECTION_ABORTED; }
            check(rejected && result == "abcdefgh", "buffered_bytes_drain_before_explicit_terminal_error");
        }
        check(store.bytes_used() == 0, "failed_producer_releases_file_and_budget");
        const auto started = std::chrono::steady_clock::now();
        {
            plex::StreamBuffer body(stop);
            body.start(store.buffer(8), [](plex::StreamBuffer& destination) { WaitForSingleObject(destination.cancellation(), INFINITE); });
        }
        check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500), "buffer_destructor_cancels_and_joins_producer");
        {
            plex::StreamBuffer body(stop);
            body.start(store.buffer(8), [stop](plex::StreamBuffer& destination) {
                HANDLE events[] = {stop, destination.cancellation()}; WaitForMultipleObjects(2, events, FALSE, INFINITE);
            });
            SetEvent(stop); rejected = false;
            try { body.read(bytes.data(), static_cast<DWORD>(bytes.size())); }
            catch (const plex::HttpError& failure) { rejected = failure.code == ERROR_CANCELLED; }
            check(rejected, "buffer_wait_is_cancelled_without_upstream_bytes");
        }
        CloseHandle(stop);
    }
    check(std::filesystem::is_empty(root), "buffer_session_files_removed_on_store_destruction");
    check(std::filesystem::remove(root), "buffer_test_directory_removed");
}
struct Response { DWORD status = 0; std::string body; std::wstring raw_headers; };

Response fetch(const std::string& url, const wchar_t* method = L"GET", const std::wstring& headers = {}) {
    const auto authority_end = url.find('/', 7);
    const auto origin = plex::parse_server_address(url.substr(0, authority_end));
    const std::wstring host(origin.host.begin(), origin.host.end());
    const auto path_text = url.substr(authority_end);
    const std::wstring path(path_text.begin(), path_text.end());
    InternetHandle session{WinHttpOpen(L"Plex synthetic tests", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0)};
    WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 10000);
    InternetHandle connection{WinHttpConnect(session.value, host.c_str(), origin.port, 0)};
    InternetHandle request{WinHttpOpenRequest(connection.value, method, path.c_str(), nullptr, nullptr, nullptr, 0)};
    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()), nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("fixture_client_connect");
    Response response;
    DWORD size = sizeof(response.status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &response.status, &size, nullptr);
    size = 0;
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_RAW_HEADERS_CRLF, nullptr, nullptr, &size, nullptr);
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_RAW_HEADERS_CRLF, nullptr, buffer.data(), &size, nullptr);
    response.raw_headers = buffer.data();
    if (wcscmp(method, L"HEAD") == 0) return response;
    char bytes[16384];
    DWORD read = 0;
    while (WinHttpReadData(request.value, bytes, sizeof(bytes), &read) && read) response.body.append(bytes, read);
    return response;
}

// Ein eigener Prozess prüft die Clientidentität unabhängig vom Proxyprozess.
bool child_status(const std::string& url, unsigned expected) {
    std::array<wchar_t, 32768> executable{};
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("client_executable");
    auto command = L"\"" + std::wstring(executable.data()) + L"\" --client-status \"" +
        std::wstring(url.begin(), url.end()) + L"\" " + std::to_wstring(expected);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) throw std::runtime_error("client_start");
    const auto waited = WaitForSingleObject(process.hProcess, 10000);
    DWORD code = 1;
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 1000);
    } else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (waited != WAIT_OBJECT_0) throw std::runtime_error("client_timeout");
    return code == 0;
}
}

int main(int count, char** arguments) {
    if (count != 1) {
        if (count != 4 || std::string(arguments[1]) != "--client-status" ||
            std::string(arguments[2]).rfind("http://127.0.0.1:", 0) != 0 ||
            (std::string(arguments[3]) != "200" && std::string(arguments[3]) != "410")) return 2;
        try { return fetch(arguments[2]).status == std::stoul(arguments[3]) ? 0 : 1; }
        catch (...) { return 1; }
    }
    try {
        struct Apartment {
            Apartment() { check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "artwork_com"); }
            ~Apartment() { CoUninitialize(); }
        } apartment;
        stream_buffer_checks();
        buffer_disconnect_checks();
        slow_decoder_checks();
        Fixture fixture;
        const auto server = plex::parse_server_address("http://127.0.0.1:" + std::to_string(fixture.port));
        {
            plex::PlaybackProxy proxy({server, "synthetic-test-token", "/library/parts/1/file.mp3"});
            const auto next = proxy.add({server, "synthetic-test-token", "/library/parts/2/file.m4a"});
            proxy.prefetch(proxy.url(), next);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!proxy.status().prefetched_bytes && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(proxy.status().prefetched_bytes == fixture.media.size(), "next_track_prefetched_entirely");
            const auto hits = fixture.hits.load();
            check(fetch(next).body == fixture.media, "cached_next_track_exact_bytes");
            check(fetch(next, L"HEAD").status == 200, "cached_next_track_head");
            check(fetch(next, L"GET", L"Range: bytes=5-11\r\n").body == fixture.media.substr(5, 7), "cached_next_track_seeking");
            check(fetch(next, L"GET", L"Range: bytes=-17\r\n").body == fixture.media.substr(fixture.media.size() - 17), "cached_next_track_suffix");
            check(fetch(next, L"GET", L"Range: bytes=100000-\r\n").status == 416, "cached_next_track_range_outside");
            check(fetch(next, L"GET", L"Origin: https://example.invalid\r\n").status == 403, "cache_keeps_origin_checks");
            check(fixture.hits.load() == hits && proxy.status().prefetch_hits == 4, "cached_next_track_avoids_upstream_requests");
            proxy.prefetch(next, "https://radio.example/stream");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(fetch(next).body == fixture.media && fixture.hits.load() == hits, "playing_cache_retained_mixed_radio_ignored");
            proxy.revoke(next);
            check(fetch(next).status == 403, "cache_keeps_revocation_checks");
            proxy.prefetch({}, {});
            const auto cleared = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (proxy.status().reserved_media_bytes && std::chrono::steady_clock::now() < cleared) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(proxy.status().prefetched_bytes == 0 && proxy.status().reserved_media_bytes == 0, "prefetch_releases_private_files_and_budget");
            const auto invalid = proxy.add({server, "synthetic-test-token", "/library/parts/999/file.mp3"});
            proxy.prefetch({}, invalid);
            const auto failed = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!proxy.status().prefetch_error && std::chrono::steady_clock::now() < failed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(proxy.status().prefetch_error && !proxy.status().prefetched_bytes, "partial_prefetch_not_published");
            check(fetch(proxy.url()).body == fixture.media, "failed_speculation_preserves_actual_playback");
            const auto oversized = proxy.add({server, "synthetic-test-token", "/library/parts/988/file.mp3"});
            proxy.prefetch({}, oversized);
            const auto large_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (proxy.status().prefetch_error != ERROR_FILE_TOO_LARGE && std::chrono::steady_clock::now() < large_deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(proxy.status().prefetch_error == ERROR_FILE_TOO_LARGE && proxy.status().reserved_media_bytes == 0, "prefetch_size_limit_does_not_reserve_large_file");
        }
        {
            Fixture stalled;
            auto proxy = std::make_unique<plex::PlaybackProxy>(plex::PlaybackSource{plex::parse_server_address("http://127.0.0.1:" + std::to_string(stalled.port)), "synthetic-test-token", "/library/parts/997/file.mp3"});
            proxy->prefetch({}, proxy->url());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stalled.slow_started.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(stalled.slow_started.load(), "prefetch_cancel_fixture_started");
            const auto started = std::chrono::steady_clock::now(); proxy.reset();
            check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "stalled_prefetch_shutdown_is_cancellable");
        }
        {
            Fixture offline;
            plex::PlaybackProxy proxy({plex::parse_server_address("http://127.0.0.1:" + std::to_string(offline.port)), "synthetic-test-token", "/library/parts/1/file.mp3"});
            proxy.prefetch({}, proxy.url());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!proxy.status().prefetched_bytes && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(proxy.status().prefetched_bytes == offline.media.size(), "outage_fixture_prefetch_ready");
            offline.stop.store(true); offline.worker.join();
            closesocket(offline.listener); offline.listener = INVALID_SOCKET;
            check(fetch(proxy.url()).body == offline.media, "complete_prefetch_plays_with_upstream_listener_closed");
            check(fetch(proxy.url(), L"GET", L"Range: bytes=7-\r\n").body == offline.media.substr(7), "complete_prefetch_seeks_during_upstream_outage");
        }
        {
            const auto root = std::filesystem::temp_directory_path() / (L"WinampPlexBufferLimit-" + std::to_wstring(GetCurrentProcessId()));
            auto store = std::make_shared<plex::LocalMediaStore>(root, plex::LocalMediaLimits{16, 16});
            {
                plex::PlaybackProxy limited({server, "synthetic-test-token", "/library/parts/1/file.mp3"},
                    std::chrono::hours(2), plex::ProxyClientPolicy::capability_only, store);
                const auto response = fetch(limited.url());
                check(response.status == 502 && response.body.empty() && limited.status().transport_error == ERROR_FILE_TOO_LARGE && store->bytes_used() == 0,
                      "buffer_quota_failure_is_explicit_without_unbuffered_playback");
            }
            store.reset();
            check(!std::filesystem::exists(root) || std::filesystem::remove(root), "buffer_limit_test_directory_removed");
        }
        plex::PlexConnectionSettings image_settings;
        image_settings.server_origin = "http://127.0.0.1:" + std::to_string(fixture.port);
        image_settings.server_token = "synthetic-test-token"; image_settings.client_identifier = "synthetic-client";
        const auto image_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(image_stop != nullptr, "artwork_stop_event");
        for (const auto method : {plex::HttpMethod::post, plex::HttpMethod::put}) {
            plex::HttpStream request(server, "synthetic-test-token", "/feedback-method-probe", method, "synthetic-client", image_stop);
            check(request.status() == 200, "feedback_http_method_and_identity_headers");
            char unused = 0;
            check(request.read(&unused, 1) == 0, "feedback_empty_response_supported");
        }
        try {
            const auto image = plex::fetch_artwork(image_settings, "/library/metadata/1/thumb/123", image_stop);
            check(image && image->width == 48 && image->height == 24, "artwork_authenticated_image_transport");
            check(!plex::fetch_artwork(image_settings, "/library/metadata/404/thumb", image_stop), "artwork_404_is_missing");
            for (const auto status : {401, 403, 302, 500}) {
                bool rejected = false;
                try { plex::fetch_artwork(image_settings, "/library/metadata/" + std::to_string(status) + "/thumb", image_stop); }
                catch (const plex::HttpError& error) { rejected = error.code == 0x20000000UL + status; }
                check(rejected, "artwork_errors_and_redirects_not_hidden");
            }
            bool rejected = false;
            try { plex::fetch_artwork(image_settings, "/library/metadata/2/thumb", image_stop); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "artwork_non_image_rejected");
        } catch (...) { CloseHandle(image_stop); throw; }
        CloseHandle(image_stop);
        {
            const auto root = std::filesystem::temp_directory_path() / (L"plex-media-tests-" + std::to_wstring(GetCurrentProcessId()));
            const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            check(stop != nullptr, "media_stop_event");
            plex::LocalMediaStore store(root, {1000000, 1000000});
            auto file = store.stage({server, "synthetic-test-token", "/library/parts/1/file.flac"}, stop);
            check(plex::requires_local_media("/library/parts/1/file.aac"), "adts_aac_requires_local_decoder_access");
            const auto path = file->path();
            check(std::filesystem::exists(path) && file->size() == fixture.media.size(), "local_media_prepared");
            PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr;
            check(GetNamedSecurityInfoW(const_cast<wchar_t*>(store.directory().c_str()), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS, "private_media_acl_read");
            SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
            const bool private_acl = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                (control & SE_DACL_PROTECTED) && acl && acl->AceCount == 2;
            LocalFree(descriptor);
            check(private_acl, "private_media_acl_has_only_explicit_user_and_system");
            {
                std::ifstream input(path, std::ios::binary);
                const std::string bytes((std::istreambuf_iterator<char>(input)), {});
                check(bytes == fixture.media, "local_media_complete_and_readable");
            }
            const auto write = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            check(write == INVALID_HANDLE_VALUE, "prepared_media_cannot_be_modified");
            if (write != INVALID_HANDLE_VALUE) CloseHandle(write);
            file.reset();
            check(!std::filesystem::exists(path) && store.bytes_used() == 0, "released_local_media_deleted");
            auto locked_file = store.stage({server, "synthetic-test-token", "/library/parts/1/file.ogg"}, stop);
            const auto locked_path = locked_file->path();
            const auto decoder = CreateFileW(locked_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            check(decoder != INVALID_HANDLE_VALUE, "native_decoder_can_open_prepared_file");
            locked_file.reset();
            check(std::filesystem::exists(locked_path) && store.bytes_used() == fixture.media.size(), "active_decoder_file_remains_accounted");
            CloseHandle(decoder);
            auto reclaimed = store.stage({server, "synthetic-test-token", "/library/parts/2/file.ogg"}, stop);
            check(!std::filesystem::exists(locked_path) && store.bytes_used() == fixture.media.size(), "closed_decoder_file_reclaimed");
            reclaimed.reset();
            bool invalid_aac = false;
            try { store.stage({server, "synthetic-test-token", "/library/parts/3/file.aac"}, stop); }
            catch (const plex::HttpError&) { invalid_aac = true; }
            check(invalid_aac && store.bytes_used() == 0, "invalid_aac_releases_input_and_output_reservation");
            for (const auto status : {401, 403, 404, 302, 500, 999}) {
                bool rejected = false;
                try { store.stage({server, "synthetic-test-token", "/library/parts/" + std::to_string(status) + "/file.ogg"}, stop); }
                catch (const plex::HttpError&) { rejected = true; }
                check(rejected && store.bytes_used() == 0, "local_media_failure_has_no_partial_file");
            }
            SetEvent(stop);
            bool cancelled = false;
            try { store.stage({server, "synthetic-test-token", "/library/parts/1/file.ogg"}, stop); }
            catch (const plex::HttpError& error) { cancelled = error.code == ERROR_CANCELLED; }
            check(cancelled && store.bytes_used() == 0, "local_media_cancelled");
            ResetEvent(stop);
            plex::LocalMediaStore small_store(root, {99999, 99999});
            bool limited = false;
            try { small_store.stage({server, "synthetic-test-token", "/library/parts/1/file.ogg"}, stop); }
            catch (const plex::HttpError& error) { limited = error.code == ERROR_FILE_TOO_LARGE; }
            check(limited && small_store.bytes_used() == 0, "local_media_size_limit");
            plex::LocalMediaStore quota(root, {100000, 100000});
            auto retained = quota.stage({server, "synthetic-test-token", "/library/parts/1/file.ogg"}, stop);
            limited = false;
            try { quota.buffer(1); }
            catch (const plex::HttpError& error) { limited = error.code == ERROR_DISK_FULL; }
            check(limited && quota.bytes_used() == fixture.media.size(), "prepared_media_and_stream_buffers_share_quota");
            limited = false;
            try { quota.stage({server, "synthetic-test-token", "/library/parts/2/file.ogg"}, stop); }
            catch (const plex::HttpError& error) { limited = error.code == ERROR_DISK_FULL; }
            check(limited && std::filesystem::exists(retained->path()), "local_media_quota_and_active_session_preserved");
            const auto abandoned = root / (L"session-" + std::wstring(48, L'c'));
            std::filesystem::create_directory(abandoned);
            {
                std::ofstream owner(abandoned / L"owner.lock", std::ios::binary);
                constexpr char marker[] = "WinampPlexLocalMedia1\r\n";
                owner.write(marker, sizeof(marker));
                std::ofstream media(abandoned / (std::wstring(48, L'a') + L".flac")); media << "partial";
                std::ofstream buffer(abandoned / (std::wstring(48, L'b') + L".buffer")); buffer << "partial";
            }
            const auto foreign = root / (L"session-" + std::wstring(48, L'd'));
            std::filesystem::create_directory(foreign);
            { std::ofstream owner(foreign / L"owner.lock"); owner << "foreign"; }
            {
                plex::LocalMediaStore cleanup(root);
                auto prepared = cleanup.stage({server, "synthetic-test-token", "/library/parts/1/file.flac"}, stop);
                check(!std::filesystem::exists(abandoned), "abandoned_marked_session_removed");
                check(std::filesystem::exists(foreign / L"owner.lock"), "unrecognized_session_preserved");
                check(std::filesystem::exists(retained->path()), "active_parallel_session_preserved");
            }
            std::filesystem::remove(foreign / L"owner.lock"); std::filesystem::remove(foreign);
            CloseHandle(stop);
        }
        {
            Fixture stalled;
            const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            const auto root = std::filesystem::temp_directory_path() / (L"plex-cancel-tests-" + std::to_wstring(GetCurrentProcessId()));
            plex::LocalMediaStore store(root);
            const auto origin = plex::parse_server_address("http://127.0.0.1:" + std::to_string(stalled.port));
            auto pending = std::async(std::launch::async, [&] {
                try { store.stage({origin, "synthetic-test-token", "/library/parts/997/file.flac"}, stop); }
                catch (const plex::HttpError& error) { return error.code == ERROR_CANCELLED; }
                return false;
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            bool partial_file = false;
            while (!partial_file && std::chrono::steady_clock::now() < deadline) {
                const auto directory = store.directory();
                if (!directory.empty()) for (const auto& entry : std::filesystem::directory_iterator(directory))
                    if (entry.path().extension() == L".flac") partial_file = true;
                if (!partial_file) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(stalled.slow_started.load() && partial_file, "local_media_partial_transfer_started");
            SetEvent(stop);
            check(pending.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready, "local_media_cancel_does_not_wait_for_server");
            check(pending.get() && store.bytes_used() == 0, "local_media_cancel_removes_partial_file");
            CloseHandle(stop);
        }
        auto proxy = std::make_unique<plex::PlaybackProxy>(plex::PlaybackSource{server, "synthetic-test-token", "/library/parts/1/file.mp3"});
        const auto url = proxy->url();
        check(url.find("synthetic-test-token") == url.npos && url.find("/library/parts/") == url.npos, "opaque_url");
        const auto all = fetch(url);
        check(all.status == 200 && all.body == fixture.media, "complete_media_bytes");
        const auto head = fetch(url, L"HEAD");
        check(head.status == 200 && head.body.empty() && head.raw_headers.find(L"Content-Length: 100000") != head.raw_headers.npos, "head_length");
        const auto range = fetch(url, L"GET", L"Range: bytes=101-500\r\n");
        check(range.status == 206 && range.body == fixture.media.substr(101, 400), "bounded_byte_range");
        check(range.raw_headers.find(L"Content-Range: bytes 101-500/100000") != range.raw_headers.npos, "content_range");
        check(fetch(url, L"GET", L"Range: bytes=99900-\r\n").body == fixture.media.substr(99900), "open_range");
        check(fetch(url, L"GET", L"Range: bytes=-128\r\n").body == fixture.media.substr(99872), "suffix_range");
        check(fetch(url, L"GET", L"Range: bytes=999999-\r\n").status == 416, "unsatisfiable_range");
        const auto before = fixture.hits.load();
        check(fetch(url + "?X-Plex-Token=should-not-forward").status == 403, "query_rejected");
        check(proxy->status().local_rejection && std::string(proxy->status().local_rejection) == "request_target", "fixed_rejection_diagnostic");
        check(fetch(url, L"POST").status == 403, "post_rejected");
        check(fetch(url, L"GET", L"Origin: https://evil.example\r\n").status == 403, "browser_origin_rejected");
        check(fetch(url, L"GET", L"Range: bytes=0-1,4-5\r\n").status == 403, "multipart_range_rejected");
        check(fetch(url, L"GET", L"Host: evil.example\r\n").status == 403, "dns_rebinding_rejected");
        check(fixture.hits.load() == before, "rejections_never_contact_upstream");
        check(fetch(url, L"GET", L"Host: 127.0.0.1\r\n").body == fixture.media, "winamp_host_transfers_media");
        check(!proxy->status().local_rejection, "valid_request_clears_rejection_diagnostic");
        const auto second_url = proxy->add({server, "synthetic-test-token", "/library/parts/2/file.flac"});
        check(fetch(second_url).body == fixture.media && fetch(url).body == fixture.media, "independent_grants_share_listener");
        proxy->revoke(second_url);
        check(fetch(second_url).status == 403 && fetch(url).status == 200, "single_grant_revocation");
        const auto origin_end = url.find('/', 7);
        check(fetch(url.substr(0, origin_end) + "/stream/invalid/audio.mp3").status == 403, "invalid_capability");
        proxy.reset();
        bool revoked = false;
        try { fetch(url); } catch (...) { revoked = true; }
        check(revoked, "grant_revoked_after_shutdown");
        {
            plex::PlaybackProxy expiring({server, "synthetic-test-token", "/library/parts/1/file.mp3"}, std::chrono::milliseconds(500));
            check(fetch(expiring.url()).body == fixture.media, "unexpired_grant_streams");
            const auto hits = fixture.hits.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(550));
            check(fetch(expiring.url()).status == 410, "expired_grant_rejected");
            check(expiring.status().local_rejection && std::string(expiring.status().local_rejection) == "request_expired" &&
                  !expiring.status().upstream_status && !expiring.status().transport_error, "expired_grant_reports_local_expiry");
            check(fixture.hits.load() == hits, "expired_grant_never_contacts_upstream");
            const auto renewed = expiring.add({server, "synthetic-test-token", "/library/parts/1/file.mp3"});
            check(fetch(renewed).body == fixture.media && !expiring.status().local_rejection, "new_grant_recovers_after_expiry");
            for (const auto lifetime : {std::chrono::milliseconds(0), std::chrono::milliseconds(std::chrono::hours(3))}) {
                bool rejected = false;
                try { plex::PlaybackProxy invalid({server, "synthetic-test-token", "/library/parts/1/file.mp3"}, lifetime); }
                catch (const std::invalid_argument&) { rejected = true; }
                check(rejected, "invalid_grant_lifetime_rejected");
            }
        }
        {
            plex::PlaybackProxy session({server, "synthetic-test-token", "/library/parts/1/file.mp3"},
                std::chrono::seconds(3), plex::ProxyClientPolicy::host_session);
            const auto pending = session.url();
            check(child_status(pending, 200), "foreign_client_can_use_unexpired_capability");
            std::this_thread::sleep_for(std::chrono::milliseconds(3050));
            check(child_status(pending, 410), "foreign_process_cannot_use_expired_host_grant");
            check(fetch(pending).body == fixture.media, "host_process_plays_waiting_track_after_expiry");
            check(fetch(pending, L"GET", L"Range: bytes=5-11\r\n").body == fixture.media.substr(5, 7), "expired_host_grant_seeking");
            check(fetch(pending, L"HEAD").status == 200, "expired_host_grant_head");
            check(child_status(pending, 410), "host_access_does_not_extend_exported_capability");
            const auto added = session.add({server, "synthetic-test-token", "/library/parts/1/file.mp3"});
            check(fetch(pending).body == fixture.media && fetch(added).body == fixture.media, "new_tracks_preserve_old_host_queue");
            check(fetch(pending, L"GET", L"Origin: https://example.invalid\r\n").status == 403, "host_identity_does_not_bypass_origin_check");
            session.revoke(pending);
            check(fetch(pending).status == 403, "host_grant_revocation_still_applies");
            bool invalid = false;
            try { plex::PlaybackProxy rejected({server, "synthetic-test-token", "/library/parts/1/file.mp3"}, std::chrono::seconds(1), static_cast<plex::ProxyClientPolicy>(99)); }
            catch (const std::invalid_argument&) { invalid = true; }
            check(invalid, "invalid_client_policy_rejected");
        }
        for (const auto status : {401, 403, 404, 302, 500}) {
            plex::PlaybackProxy failure({server, "synthetic-test-token", "/library/parts/" + std::to_string(status) + "/file.mp3"});
            const auto response = fetch(failure.url());
            check(response.status == static_cast<DWORD>(status == 302 || status == 500 ? 502 : status), "classified_upstream_status");
            check(response.body.empty() && response.raw_headers.find(L"Set-Cookie") == response.raw_headers.npos &&
                  response.raw_headers.find(L"Location:") == response.raw_headers.npos, "private_headers_not_forwarded");
            check(failure.status().last_failure.count == 1 && failure.status().last_failure.upstream_status == static_cast<unsigned long>(status),
                  "upstream_rejection_remains_diagnosable");
        }
        {
            plex::PlaybackProxy truncated({server, "synthetic-test-token", "/library/parts/999/file.mp3"});
            check(fetch(truncated.url()).body == "truncated", "truncated_fixture_received");
            check(truncated.status().transport_error != 0, "truncated_response_reported");
            const auto failure = truncated.status().last_failure;
            check(failure.count == 1 && failure.transport_error != 0 && failure.upstream_status == 200 &&
                  failure.expected_bytes == 999999 && failure.transferred_bytes == 9, "truncated_response_byte_diagnosis");
            const auto healthy = truncated.add({server, "synthetic-test-token", "/library/parts/1/file.mp3"});
            check(fetch(healthy).body == fixture.media && !truncated.status().transport_error, "next_track_recovers_after_transport_failure");
            const auto retained = truncated.status().last_failure;
            check(retained.count == failure.count && retained.transport_error == failure.transport_error &&
                  retained.expected_bytes == failure.expected_bytes && retained.transferred_bytes == failure.transferred_bytes,
                  "next_track_preserves_last_failure_diagnosis");
        }
        {
            plex::PlaybackProxy interrupted({server, "synthetic-test-token", "/library/parts/996/file.mp3"});
            const auto response = fetch(interrupted.url());
            check(response.body == fixture.media, "temporary_upstream_pause_keeps_complete_media");
        }
        {
            plex::PlaybackProxy proxy({server, "synthetic-test-token", "/library/parts/995/file.mp3"});
            check(fetch(proxy.url()).body == fixture.media, "interrupted_file_resumes_without_missing_or_duplicate_bytes");
            check(proxy.status().last_failure.count == 1, "recovered_interruption_remains_visible");
        }
        {
            plex::PlaybackProxy proxy({server, "synthetic-test-token", "/library/parts/994/file.mp3"});
            bool rejected = false;
            try { rejected = fetch(proxy.url()).body != fixture.media; } catch (const plex::HttpError&) { rejected = true; }
            check(rejected, "changed_file_is_not_appended_after_interruption");
            check(proxy.status().transport_error == ERROR_INVALID_DATA, "changed_file_reports_explicit_error");
        }
        for (const auto fixture_id : {993, 992, 991, 990}) {
            const auto hits = fixture.hits.load();
            plex::PlaybackProxy proxy({server, "synthetic-test-token", "/library/parts/" + std::to_string(fixture_id) + "/file.mp3"});
            bool rejected = false;
            try { rejected = fetch(proxy.url()).body != fixture.media; } catch (const plex::HttpError&) { rejected = true; }
            check(rejected, "invalid_or_excessive_resumption_is_rejected");
            check(proxy.status().transport_error == (fixture_id == 990 ? ERROR_HANDLE_EOF : ERROR_INVALID_DATA), "resumption_error_remains_explicit");
            if (fixture_id == 990) check(fixture.hits.load() - hits == 4, "resumption_budget_is_three_followup_requests");
        }
        const auto handles_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int iteration = 0; iteration < 10; ++iteration) {
            plex::PlaybackProxy repeated({server, "synthetic-test-token", "/library/parts/1/file.mp3"});
            check(fetch(repeated.url()).status == 200, "repeated_proxy_lifecycle");
        }
        check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == handles_before, "no_user_handles_leaked");
        check(fixture.token_errors.load() == 0, "header_authentication_intact");
        proxy = std::make_unique<plex::PlaybackProxy>(plex::PlaybackSource{server, "synthetic-test-token", "/library/parts/998/file.mp3"});
        const auto pending_url = proxy->url();
        auto pending = std::async(std::launch::async, [pending_url] { try { fetch(pending_url); } catch (...) {} });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!fixture.slow_started.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(fixture.slow_started.load(), "pending_request_started");
        const auto start = std::chrono::steady_clock::now();
        proxy.reset();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(elapsed < 1500, "async_request_cancelled_on_shutdown");
        pending.get();
        std::cout << checks << " transport checks passed; cancellation " << elapsed << " ms\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
