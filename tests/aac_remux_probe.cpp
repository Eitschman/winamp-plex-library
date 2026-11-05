#include "plex/aac_remux.h"
#include "plex/http_stream.h"
#include <objbase.h>
#include <iostream>

// Nur lokale synthetische Mediendateien, kein Zugriff auf die Plex-Konfiguration.
int wmain(int count, wchar_t** arguments) {
    if (count != 3) return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int exit_code = 0;
    try {
        const auto result = plex::remux_aac(arguments[1], arguments[2], stop, 512ULL * 1024 * 1024);
        std::cout << "samples=" << result.samples << " payload=" << result.payload_type << " bytes=" << result.bytes << '\n';
    } catch (const plex::HttpError& error) { std::cerr << "remux_error=" << error.code << " stage=" << error.what() << '\n'; exit_code = 1; }
      catch (...) { std::cerr << "remux_failed\n"; exit_code = 1; }
    if (stop) CloseHandle(stop);
    CoUninitialize();
    return exit_code;
}
