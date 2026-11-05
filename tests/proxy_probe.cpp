#include "plex/proxy.h"
#include "plex/local_media.h"
#include "plex/settings.h"
#include <windows.h>
#include <iostream>
#include <string>

// Ausschließlich synthetische Loopback-Fixtures, keine realen Zugangsdaten annehmen.
int main(int count, char** arguments) {
    const bool prepare = count == 4 && std::string(arguments[1]) == "--prepare";
    if (count != 3 && !prepare) return 2;
    try {
        auto server = plex::parse_server_address(arguments[prepare ? 2 : 1]);
        if (server.host != "127.0.0.1" || server.secure) return 2;
        if (prepare) {
            struct Stop { HANDLE value = CreateEventW(nullptr, TRUE, FALSE, nullptr); ~Stop() { if (value) CloseHandle(value); } } stop;
            if (!stop.value) return 1;
            plex::LocalMediaStore store(plex::settings_path().parent_path() / L"playback");
            const auto file = store.stage({server, "synthetic-test-token", arguments[3]}, stop.value);
            std::cout << file->path().u8string() << std::endl;
            std::string command; std::getline(std::cin, command);
            std::cout << "prepared_bytes=" << file->size() << '\n';
            return 0;
        }
        plex::PlaybackProxy proxy({server, "synthetic-test-token", arguments[2]});
        std::cout << proxy.url() << std::endl;
        std::string command;
        std::getline(std::cin, command);
        const auto status = proxy.status();
        std::cout << "status=" << status.upstream_status << " error=" << status.transport_error << " bytes=" << status.bytes_sent << '\n';
        return 0;
    } catch (...) { std::cerr << "proxy_probe_failed\n"; return 1; }
}
