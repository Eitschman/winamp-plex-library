#include <windows.h>
#include <gen_ml/ml.h>
#include <iostream>
#include <stdexcept>

// Das technische Testmodul darf außerhalb der expliziten Testkopie nicht starten.
int wmain(int count, wchar_t** arguments) {
    if (count != 2) return 2;
    try {
        const auto library = LoadLibraryW(arguments[1]);
        if (!library) throw std::runtime_error("load_probe");
        const auto entry = reinterpret_cast<winampMediaLibraryPlugin* (__cdecl*)()>(GetProcAddress(library, "winampGetMediaLibraryPlugin"));
        if (!entry) throw std::runtime_error("probe_export");
        const auto plugin = entry(); plugin->hDllInstance = library;
        for (const auto name : {L"PlexReplayUnrelatedHost", L"WinampPlexTest"}) {
            WNDCLASSW description{}; description.lpszClassName = name;
            description.lpfnWndProc = DefWindowProcW; description.hInstance = GetModuleHandleW(nullptr);
            if (!RegisterClassW(&description)) throw std::runtime_error("host_class");
            const auto window = CreateWindowW(name, L"Isolation countercheck", 0, 0, 0, 1, 1, nullptr, nullptr, description.hInstance, nullptr);
            if (!window) throw std::runtime_error("host_window");
            plugin->hwndWinampParent = plugin->hwndLibraryParent = window;
            if (plugin->init() == 0) throw std::runtime_error("foreign_host_accepted");
            plugin->quit(); DestroyWindow(window); UnregisterClassW(name, description.hInstance);
        }
        if (!FreeLibrary(library)) throw std::runtime_error("unload_probe");
        std::cout << "Unrelated host rejected; matching class outside isolated executable path rejected\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
