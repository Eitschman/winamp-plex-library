#include <windows.h>
#include <gen_ml/ml.h>
#include <iostream>
#include <stdexcept>

namespace {
int additions = 0;
int removals = 0;
bool deny_navigation = false;
LRESULT CALLBACK host(HWND window, UINT message, WPARAM parameter, LPARAM operation) {
    if (message == WM_ML_IPC) {
        if (operation == ML_IPC_TREEITEM_ADDW) {
            auto* item = reinterpret_cast<MLTREEITEMW*>(parameter);
            if (item->size != sizeof(*item) || item->parentId || wcscmp(item->title, L"Plex") != 0) return 0;
            if (!deny_navigation) { item->id = 41000; ++additions; return item->id; }
            return 0;
        }
        if (operation == ML_IPC_TREEITEM_GETHANDLE && parameter == 41000) return 42000;
        if (operation == ML_IPC_TREEITEM_DELETE && parameter == 42000) { ++removals; return 1; }
    }
    return DefWindowProcW(window, message, parameter, operation);
}
void check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
}

int wmain(int count, wchar_t** arguments) {
    if (count != 2) return 2;
    try {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = host;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = L"PlexPluginTestHost";
        check(RegisterClassW(&window_class) != 0, "register_host");
        const auto window = CreateWindowW(window_class.lpszClassName, L"SDK ABI test host", WS_OVERLAPPEDWINDOW,
                                          0, 0, 800, 600, nullptr, nullptr, window_class.hInstance, nullptr);
        check(window != nullptr, "create_host");
        const auto baseline = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int iteration = 0; iteration < 100; ++iteration) {
            const auto library = LoadLibraryW(arguments[1]);
            check(library != nullptr, "load_plugin");
            const auto get = reinterpret_cast<winampMediaLibraryPlugin* (__cdecl*)()>(GetProcAddress(library, "winampGetMediaLibraryPlugin"));
            check(get != nullptr, "undecorated_export");
            const auto plugin = get();
            check(plugin && plugin->version == MLHDR_VER && plugin->description, "sdk_descriptor");
            plugin->hwndWinampParent = window;
            plugin->hwndLibraryParent = window;
            plugin->hDllInstance = library;
            deny_navigation = true;
            check(plugin->init() != 0, "failed_navigation_init");
            deny_navigation = false;
            check(plugin->init() == 0, "plugin_init");
            check(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41001, reinterpret_cast<INT_PTR>(window), 0) == 0, "unrelated_node");
            const auto view = reinterpret_cast<HWND>(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41000, reinterpret_cast<INT_PTR>(window), 0));
            check(IsWindow(view) && GetParent(view) == window, "plugin_view");
            if (iteration % 2 == 0) DestroyWindow(view);
            plugin->quit();
            check(!IsWindow(view), "view_destroyed_on_quit");
            check(FreeLibrary(library) != 0, "unload_plugin");
        }
        check(additions == 100 && removals == 100, "balanced_navigation");
        check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == baseline, "balanced_user_handles");
        DestroyWindow(window);
        std::cout << "100 load/init/view/quit/unload cycles passed; failed init and foreign nodes rejected\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
