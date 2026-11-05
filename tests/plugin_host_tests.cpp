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
unsigned unskinned_lists = 0;
MockServices services;
unsigned settings_case = 0;
bool settings_valid = false;
bool german_host = true;
bool deny_language = false;
bool diagnostics_requested = false, diagnostics_checked = false, diagnostics_valid = false;
BOOL CALLBACK inspect_settings(HWND dialog, LPARAM) {
    const auto frame = dialog;
    dialog = GetDlgItem(dialog, 1081);
    if (const auto diagnostic = GetDlgItem(dialog, 1084)) {
        wchar_t information[2049]{}; GetWindowTextW(diagnostic, information, 2049);
        diagnostics_valid = wcsstr(information, L"Windows DPAPI") && wcsstr(information, L"HTTP:") &&
                            !wcsstr(information, L"https://") && !wcsstr(information, L"X-Plex-Token");
        diagnostics_checked = true;
        PostMessageW(frame, WM_COMMAND, IDCANCEL, 0); return FALSE;
    }
    if (!GetDlgItem(dialog, 1078)) return TRUE;
    wchar_t status[160]{};
    GetDlgItemTextW(dialog, 1004, status, 160);
    const auto address = GetDlgItem(dialog, 1074);
    settings_valid = IsWindowEnabled(address) && !(GetWindowLongPtrW(address, GWL_STYLE) & ES_READONLY) &&
                     (((settings_case == 0 || settings_case == 3) && diagnostics_requested) || !(GetWindowLongPtrW(address, GWL_STYLE) & WS_VISIBLE)) &&
                     (wcscmp(status, L"Nicht bei Plex angemeldet.") == 0 || wcscmp(status, L"Not signed in to Plex.") == 0);
    if (!(GetWindowLongPtrW(address, GWL_STYLE) & WS_VISIBLE)) SendMessageW(dialog, WM_COMMAND, 1078, 0);
    settings_valid = settings_valid && (GetWindowLongPtrW(address, GWL_STYLE) & WS_VISIBLE);
    if ((settings_case == 0 || settings_case == 3) && !diagnostics_checked) {
        if (!diagnostics_requested) { diagnostics_requested = true; PostMessageW(dialog, WM_COMMAND, 1073, 0); }
        return FALSE;
    }
    if (settings_case == 1) {
        SetWindowTextW(address, L"https://synthetic.example:32400");
        SendDlgItemMessageW(dialog, 1071, CB_SETCURSEL, 1, 0);
    } else if (settings_case == 2) {
        wchar_t value[160]{}; GetWindowTextW(address, value, 160);
        settings_valid = settings_valid && wcscmp(value, L"https://synthetic.example:32400") == 0 &&
                         SendDlgItemMessageW(dialog, 1071, CB_GETCURSEL, 0, 0) == 1;
    }
    PostMessageW(dialog, WM_COMMAND, settings_case == 1 ? IDOK : IDCANCEL, 0);
    return FALSE;
}
VOID CALLBACK inspect_settings_timer(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), inspect_settings, 0);
}
LRESULT CALLBACK host(HWND window, UINT message, WPARAM parameter, LPARAM operation) {
    if (message == WM_WA_IPC && operation == IPC_GETLANGUAGEPACKINSTANCE && parameter == MAKEWPARAM(5, LANG_LANG_CODE))
        return deny_language ? 0 : reinterpret_cast<LRESULT>(german_host ? L"de" : L"en");
    if (message == WM_WA_IPC && operation == IPC_GET_API_SERVICE) return reinterpret_cast<LRESULT>(&services);
    if (message == WM_ML_IPC) {
        if (operation == ML_IPC_UNSKINWINDOW) {
            const auto control = reinterpret_cast<HWND>(parameter);
            if (IsWindow(control) && GetDlgCtrlID(control) == 1050) ++unskinned_lists;
            return IsWindow(control);
        }
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
        // Windows erzeugt beim ersten Eingabefokus pro Thread eigene IME-Fenster.
        // Diese Betriebssystemressourcen gehören nicht zum Plugin-Lebenszyklus.
        const auto input = CreateWindowW(L"EDIT", L"", WS_CHILD, 0, 0, 100, 20, window, nullptr, window_class.hInstance, nullptr);
        check(input != nullptr, "initialize_input_context");
        SetFocus(input); SetFocus(window); DestroyWindow(input);
        const auto baseline = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        const auto gdi_baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        DWORD warm_gdi = 0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            german_host = iteration % 2 == 0;
            const auto library = LoadLibraryW(arguments[1]);
            check(library != nullptr, "load_plugin");
            const auto get = reinterpret_cast<winampMediaLibraryPlugin* (__cdecl*)()>(GetProcAddress(library, "winampGetMediaLibraryPlugin"));
            check(get != nullptr, "undecorated_export");
            const auto plugin = get();
            check(plugin && plugin->version == MLHDR_VER && plugin->description, "sdk_descriptor");
            plugin->hwndWinampParent = window;
            plugin->hwndLibraryParent = window;
            plugin->hDllInstance = library;
            deny_language = true;
            check(plugin->init() != 0, "missing_host_language_rejected");
            deny_language = false;
            deny_navigation = true;
            check(plugin->init() != 0, "failed_navigation_init");
            deny_navigation = false;
            check(plugin->init() == 0, "plugin_init");
            check(child_count == 4, "native_child_navigation");
            check(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41999, reinterpret_cast<INT_PTR>(window), 0) == 0, "unrelated_node");
            const auto view = reinterpret_cast<HWND>(plugin->MessageProc(ML_MSG_TREE_ONCREATEVIEW, 41000, reinterpret_cast<INT_PTR>(window), 0));
            check(IsWindow(view) && GetParent(view) == window, "plugin_view");
            check(!(GetWindowLongPtrW(GetDlgItem(view, 1001), GWL_STYLE) & WS_VISIBLE) &&
                  !(GetWindowLongPtrW(GetDlgItem(view, 1053), GWL_STYLE) & WS_VISIBLE), "root_without_redundant_context_or_back");
            check(!(GetWindowLongPtrW(GetDlgItem(view, 1060), GWL_STYLE) & WS_VISIBLE) &&
                  (GetWindowLongPtrW(GetDlgItem(view, 1064), GWL_STYLE) & WS_VISIBLE), "unconfigured_library_without_empty_dropdown");
            check(GetDlgItem(view, 1002) == nullptr && IsWindow(GetDlgItem(view, 1050)), "catalog_without_manual_token_test_control");
            wchar_t caption[80]{}; GetDlgItemTextW(view, 1007, caption, 80);
            check(wcscmp(caption, german_host ? L"Einstellungen..." : L"Settings...") == 0 && !GetDlgItem(view, 1005), "language_inherited_from_winamp_without_selector");
            if (iteration == 0) {
                for (settings_case = 0; settings_case < 3; ++settings_case) {
                    settings_valid = false;
                    const auto timer = SetTimer(window, 1, 25, inspect_settings_timer);
                    check(timer != 0, "settings_inspection_timer");
                    plugin->MessageProc(ML_MSG_CONFIG, reinterpret_cast<INT_PTR>(window), 0, 0);
                    KillTimer(window, timer);
                    check(settings_valid, "settings_without_automatic_auth_and_advanced_preferences");
                    if (settings_case == 0) check(diagnostics_checked && diagnostics_valid, "skinned_diagnostics_without_credentials");
                    if (settings_case == 0) check(!std::filesystem::exists(configuration), "settings_cancel_without_write");
                }
            } else {
                settings_case = 3;
                diagnostics_requested = diagnostics_checked = diagnostics_valid = false;
                settings_valid = false;
                const auto timer = SetTimer(window, 1, 25, inspect_settings_timer);
                check(timer != 0, "repeat_settings_timer");
                plugin->MessageProc(ML_MSG_CONFIG, reinterpret_cast<INT_PTR>(window), 0, 0);
                KillTimer(window, timer);
                check(settings_valid && diagnostics_checked && diagnostics_valid, "repeat_settings_and_diagnostics");
            }
            if (iteration % 2 == 0) DestroyWindow(view);
            plugin->quit();
            check(!services.registered && services.memory.memory.allocations == 0, "artwork_service_released");
            check(!IsWindow(view), "view_destroyed_on_quit");
            check(FreeLibrary(library) != 0, "unload_plugin");
            // Die erste vollständige Runde initialisiert gemeinsame Windows-Control-
            // Ressourcen. Weitere 99 Lade- und Dialogrunden dürfen nichts akkumulieren.
            if (iteration == 0) warm_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            else check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == warm_gdi, "no_gdi_growth_across_settings_cycles");
            if (iteration == 0 || iteration == 49 || iteration == 99)
                std::cerr << "GDI objects after cycle " << iteration + 1 << ": " << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << ", baseline=" << gdi_baseline << '\n';
        }
        check(additions == 500 && removals == 100, "balanced_navigation");
        check(skin_dialogs == 302 && skin_lists == 100, "native_skin_registration");
        check(unskinned_lists == 100, "list_skin_released_before_window_destruction");
        const auto handles = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        if (handles > baseline) {
            std::cerr << "USER objects: before=" << baseline << ", after=" << handles << '\n';
            EnumThreadWindows(GetCurrentThreadId(), [](HWND handle, LPARAM) -> BOOL {
                wchar_t type[100]{}; GetClassNameW(handle, type, 100);
                std::wcerr << L"Remaining window: " << type << L'\n'; return TRUE;
            }, 0);
        }
        // Der Fokuswechsel darf temporäre Windows-Ressourcen aus der Ausgangsmessung
        // freigeben. Ausschließlich ein Anstieg ist hier ein Leckhinweis.
        check(handles <= baseline, "balanced_user_handles");
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == warm_gdi, "balanced_gdi_handles");
        DestroyWindow(window);
        check(std::filesystem::remove(configuration / L"connection.dpapi"), "settings_test_file_cleanup");
        check(std::filesystem::remove(configuration), "settings_test_directory_cleanup");
        std::cout << "100 load/init/view/quit/unload cycles passed; failed init and foreign nodes rejected\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
