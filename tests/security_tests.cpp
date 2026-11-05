#include "plex/security.h"
#include "plex/playlist.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const char* name) {
    ++checks;
    if (!condition) throw std::runtime_error(name);
}
void rejects(const std::function<void()>& action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, name);
}
}

int main() {
    try {
        const auto secure = plex::parse_server_address("https://music.example:32400/");
        check(secure.secure && secure.port == 32400 && secure.host == "music.example", "https_origin");
        check(plex::parse_server_address("https://MUSIC.example").host == "music.example", "case_normalization");
        check(plex::parse_server_address("http://127.0.0.1:23456").port == 23456, "loopback_http");
        for (const auto* address : {"http://music.example:32400", "ftp://music.example", "https://user:password@music.example",
             "https://music.example?X-Plex-Token=secret", "https://music.example/#fragment", "https://music.example/path",
             "https://music.example:0", "https://music.example:65536", "https://music.example:abc", "https://music.example:443:9",
             "https://music.example\\evil", "https://music.example\r\nInjected: yes", "https://", "https://.example", "https://music..example"})
            rejects([&] { plex::parse_server_address(address); }, "unsafe_origin");
        plex::validate_token("synthetic-token.ABC_123-xyz"); ++checks;
        for (const auto* token : {"", "fake\r\nInjected: yes", "fake token", "fake\ttoken"})
            rejects([&] { plex::validate_token(token); }, "unsafe_token");
        check(plex::media_extension("/library/parts/123/file.FLAC") == ".flac", "file_extension");
        check(plex::media_extension("/library/parts/123/1730000000/file.flac") == ".flac", "versioned_part_key");
        for (const auto* path : {"https://other.example/library/parts/1/file.mp3", "//other.example/file.mp3",
             "/library/parts/../file.mp3", "/library/parts/1/../file.mp3", "/library/parts/1/file.mp3?X-Plex-Token=fake",
             "/library/parts/1/%2e%2e.mp3", "/library/parts/1/file.mp3\r\nHeader: fake", "/library/parts/1/file.exe"})
            rejects([&] { plex::media_extension(path); }, "unsafe_part_path");
        for (const auto* range : {"bytes=0-", "bytes=100-200", "bytes=-128", "bytes=0-0", "bytes=9223372036854775807-"})
            check(plex::validate_range(range) == range, "valid_range");
        for (const auto* range : {"bytes=-", "bytes=9-1", "bytes=0-1,4-5", "bytes=0-\r\nFake: yes", "items=0-9",
                                  "bytes=-0", "bytes=0--1", "bytes=999999999999999999999-"})
            rejects([&] { plex::validate_range(range); }, "unsafe_range");
        check(plex::encode_query_component("Björk & AC/DC") == "Bj%C3%B6rk%20%26%20AC%2FDC", "utf8_query");
        const std::string target = "/stream/synthetic-key/audio.mp3";
        const std::string authority = "127.0.0.1:12345";
        const auto request = [&](std::string verb, std::string headers) {
            return plex::parse_local_request(verb + " " + target + " HTTP/1.1\r\n" + headers + "\r\n", authority, target);
        };
        check(!request("GET", "Host: " + authority + "\r\nRange: bytes=1-2\r\n").head, "get_request");
        check(request("HEAD", "Host: " + authority + "\r\n").head, "head_request");
        check(!request("GET", "Host: 127.0.0.1\r\nUser-Agent: Winamp/5.9\r\n").head, "winamp_host_without_port");
        rejects([&] { request("GET", "Host: 127.0.0.1:9999\r\n"); }, "wrong_explicit_port");
        rejects([&] { request("GET", "Host: localhost\r\n"); }, "different_host_without_port");
        rejects([&] { request("GET", "Host: 127.0.0.1\r\nHost: " + authority + "\r\n"); }, "duplicate_legacy_host");
        for (const auto* headers : {"Host: attacker.example\r\n", "Host: 127.0.0.1:12345\r\nHost: 127.0.0.1:12345\r\n",
             "Host: 127.0.0.1:12345\r\nOrigin: https://evil.example\r\n",
             "Host: 127.0.0.1:12345\r\nContent-Length: 5\r\n",
             "Host: 127.0.0.1:12345\r\nTransfer-Encoding: chunked\r\n",
             "Host: 127.0.0.1:12345\r\nRange: bytes=0-\r\nRange: bytes=1-\r\n",
             "Host: 127.0.0.1:12345\r\nSec-Fetch-Site: cross-site\r\n", "User-Agent: test\r\n"})
            rejects([&] { request("GET", headers); }, "unsafe_local_headers");
        rejects([&] { request("POST", "Host: " + authority + "\r\n"); }, "unsupported_method");
        rejects([&] { plex::parse_local_request("GET /wrong HTTP/1.1\r\nHost: " + authority + "\r\n\r\n", authority, target); }, "wrong_capability");
        rejects([&] { plex::parse_local_request(std::string(17000, 'a'), authority, target); }, "oversized_headers");
        const auto local = "http://127.0.0.1:12345/stream/" + std::string(48, 'a') + "/audio.mp3";
        check(plex::playlist_display_title("The Puppeteer", "Elvellon") == "Elvellon - The Puppeteer", "playlist_artist_and_title");
        check(plex::playlist_display_title("Jóga", "Björk") == "Björk - Jóga", "playlist_artist_unicode");
        check(plex::playlist_display_title("Ohne Künstler", "") == "Ohne Künstler", "playlist_optional_artist");
        const auto playlist = plex::make_playlist({{local, "Björk\r\n#EXTINF:0,Injected", 306}});
        check(playlist.find("#EXTINF:306,Björk  #EXTINF:0,Injected\r\n") != playlist.npos, "playlist_unicode_and_line_safety");
        rejects([&] { plex::make_playlist({{"https://other.example/media?X-Plex-Token=fake", "Title", 5}}); }, "playlist_external_url_rejected");
        rejects([&] { plex::make_playlist({{local + "?X-Plex-Token=fake", "Title", 5}}); }, "playlist_query_rejected");
        rejects([&] { plex::make_playlist({}); }, "empty_playlist_rejected");
        const std::string directory = "C:\\private\\playback\\session-test";
        const auto file = directory + "\\" + std::string(48, 'a') + ".flac";
        check(plex::make_playlist({{file, "Lokaler Titel", 120, true}}, directory).find(file) != std::string::npos, "owned_local_media_accepted");
        const auto aac = directory + "\\" + std::string(48, 'b') + ".m4a";
        check(plex::make_playlist({{aac, "AAC-Test", 45, true}}, directory).find(aac) != std::string::npos, "owned_aac_file_accepted");
        for (const auto& path : {std::string("C:\\foreign\\file.flac"), directory + "\\..\\foreign.flac", file + "\r\nInjected", directory + "\\short.ogg", directory + "\\" + std::string(48, 'a') + ".exe"})
            rejects([&] { plex::make_playlist({{path, "Title", 5, true}}, directory); }, "foreign_local_media_rejected");
        rejects([&] { plex::make_playlist({{file, "Title", 5, true}}); }, "local_directory_required");
        rejects([&] { plex::make_playlist({{file, "Title", 5}}); }, "local_file_requires_explicit_type");
        std::cout << checks << " security checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
