#include "plex/proxy.h"
#include <windows.h>
#include <iostream>
#include <string>

// Ausschließlich synthetische Loopback-Fixtures, keine realen Zugangsdaten annehmen.
int main(int count, char** arguments) {
    if (count != 3) return 2;
    try {
        auto server = plex::parse_server_address(arguments[1]);
        if (server.host != "127.0.0.1" || server.secure) return 2;
        plex::PlaybackProxy proxy({server, "synthetic-test-token", arguments[2]});
        std::cout << proxy.url() << std::endl;
        std::string command;
        std::getline(std::cin, command);
        const auto status = proxy.status();
        std::cout << "status=" << status.upstream_status << " error=" << status.transport_error << " bytes=" << status.bytes_sent << '\n';
        return 0;
    } catch (...) { std::cerr << "proxy_probe_failed\n"; return 1; }
}
