#include <windows.h>
#include <gen_ml/ml.h>
#include <gen_ml/ml_ipc_0313.h>
#include <Winamp/wa_ipc.h>
#include "wasabi_mock.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <string>

namespace {
int additions = 0;
int removals = 0;
bool deny_navigation = false;
unsigned child_count = 0;
unsigned skin_dialogs = 0, skin_lists = 0;
MockServices services;
LRESULT CALLBACK host(HWND window, UINT message, WPARAM parameter, LPARAM operation) {
    if (message == WM_WA_IPC && operation == IPC_GET_API_SERVICE) return reinterpret_cast<LRESULT>(&services);
    if (message == WM_ML_IPC) {
        if (operation == ML_IPC_SKINWINDOW) {
            const auto* skin = reinterpret_cast<const MLSKINWINDOW*>(parameter);
            if (!skin || !IsWindow(skin->hwndToSkin) || !(skin->style & SWS_USESKINCOLORS) || !(skin->style & SWS_USESKINFONT)) return FALSE;
            if (skin->skinType == SKINNEDWND_TYPE_DIALOG) ++skin_dialogs;
            if (skin->skinType == SKINNEDWND_TYPE_LISTVIEW) ++skin_lists;
            return TRUE;
        }
        if (operation == ML_IPC_TREEITEM_ADDW) {
            auto* item = reinterpret_cast<MLTREEITEMW*>(parameter);
            if (item->size != sizeof(*item) || !item->title) return 0;
            if (!deny_navigation) {
                if (!item->parentId && wcscmp(item->title, L"Plex") == 0) { item->id = 41000; child_count = 0; }
                else if (item->parentId == 41000 && child_count < 4) item->id = 41001 + child_count++;
                else return 0;
                ++additions; return item->id;
            }
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
        const auto configuration = std::filesystem::temp_directory_path() / (L"PlexHostTest-" + std::to_wstring(GetCurrentProcessId()));
        check(!std::filesystem::exists(configuration), "isolated_configuration_path");
        check(SetEnvironmentVariableW(L"WINAMP_PLEX_CONFIG_DIRECTORY", configuration.c_str()) != FALSE, "isolate_configuration");
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
            check(child_count == 4, "native_child_navigation");
            check(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41999, reinterpret_cast<INT_PTR>(window), 0) == 0, "unrelated_node");
            const auto view = reinterpret_cast<HWND>(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41000, reinterpret_cast<INT_PTR>(window), 0));
            check(IsWindow(view) && GetParent(view) == window, "plugin_view");
            if (iteration % 2 == 0) DestroyWindow(view);
            plugin->quit();
            check(!services.registered && services.memory.memory.allocations == 0, "artwork_service_released");
            check(!IsWindow(view), "view_destroyed_on_quit");
            check(FreeLibrary(library) != 0, "unload_plugin");
        }
        check(additions == 500 && removals == 100, "balanced_navigation");
        check(skin_dialogs == 100 && skin_lists == 100, "native_skin_registration");
        check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == baseline, "balanced_user_handles");
        DestroyWindow(window);
        std::cout << "100 load/init/view/quit/unload cycles passed; failed init and foreign nodes rejected\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
