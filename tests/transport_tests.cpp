#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include "plex/proxy.h"
#include "plex/security.h"
#include "plex/artwork.h"
#include "plex/http_stream.h"
#include "plex/local_media.h"
#include <fstream>
#include <aclapi.h>
#include <objbase.h>
#include <atomic>
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

    Fixture() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("fixture_winsock");
        media.resize(100000);
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
        if (request.find("/library/parts/998/") != request.npos) {
            slow_started.store(true);
            while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
struct Response { DWORD status = 0; std::string body; std::wstring raw_headers; };

Response fetch(const std::string& url, const wchar_t* method = L"GET", const std::wstring& headers = {}) {
    const auto authority_end = url.find('/', 7);
    const auto origin = plex::parse_server_address(url.substr(0, authority_end));
    const std::wstring host(origin.host.begin(), origin.host.end());
    const auto path_text = url.substr(authority_end);
    const std::wstring path(path_text.begin(), path_text.end());
    InternetHandle session{WinHttpOpen(L"Plex synthetic tests", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0)};
    WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000);
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
}

int main() {
    try {
        struct Apartment {
            Apartment() { check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "artwork_com"); }
            ~Apartment() { CoUninitialize(); }
        } apartment;
        Fixture fixture;
        const auto server = plex::parse_server_address("http://127.0.0.1:" + std::to_string(fixture.port));
        plex::PlexConnectionSettings image_settings;
        image_settings.server_origin = "http://127.0.0.1:" + std::to_string(fixture.port);
        image_settings.server_token = "synthetic-test-token"; image_settings.client_identifier = "synthetic-client";
        const auto image_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(image_stop != nullptr, "artwork_stop_event");
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
            check(fixture.hits.load() == hits, "expired_grant_never_contacts_upstream");
            for (const auto lifetime : {std::chrono::milliseconds(0), std::chrono::milliseconds(std::chrono::hours(3))}) {
                bool rejected = false;
                try { plex::PlaybackProxy invalid({server, "synthetic-test-token", "/library/parts/1/file.mp3"}, lifetime); }
                catch (const std::invalid_argument&) { rejected = true; }
                check(rejected, "invalid_grant_lifetime_rejected");
            }
        }
        for (const auto status : {401, 403, 404, 302, 500}) {
            plex::PlaybackProxy failure({server, "synthetic-test-token", "/library/parts/" + std::to_string(status) + "/file.mp3"});
            const auto response = fetch(failure.url());
            check(response.status == static_cast<DWORD>(status == 302 || status == 500 ? 502 : status), "classified_upstream_status");
            check(response.body.empty() && response.raw_headers.find(L"Set-Cookie") == response.raw_headers.npos &&
                  response.raw_headers.find(L"Location:") == response.raw_headers.npos, "private_headers_not_forwarded");
        }
        {
            plex::PlaybackProxy truncated({server, "synthetic-test-token", "/library/parts/999/file.mp3"});
            check(fetch(truncated.url()).body == "truncated", "truncated_fixture_received");
            check(truncated.status().transport_error != 0, "truncated_response_reported");
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
