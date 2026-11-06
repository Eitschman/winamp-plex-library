#include "resource_snapshot.h"
#include "ui/view_reaper.h"
#include <objbase.h>
#include <thread>
#include <atomic>
#include <vector>
#include <wincrypt.h>
#include <commctrl.h>
#include <msctf.h>
#include <windows.system.h>
#include <dispatcherqueue.h>
#include <wrl/client.h>
#include "resource.h"

namespace {
unsigned dialog_interval = 1;
bool focus_edit = false;
INT_PTR CALLBACK child(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
INT_PTR CALLBACK frame(HWND dialog, UINT message, WPARAM, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetWindowPos(dialog, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        const auto panel = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PLEX_LOGIN), dialog, child, 0);
        if (!panel || !SetTimer(dialog, 1, dialog_interval, nullptr)) { EndDialog(dialog, -1); return TRUE; }
        ShowWindow(panel, SW_SHOW);
        if (focus_edit) { SetFocus(GetDlgItem(panel, IDC_LOGIN_ADDRESS)); return FALSE; }
        return TRUE;
    }
    if (message == WM_TIMER) { KillTimer(dialog, 1); EndDialog(dialog, IDOK); return TRUE; }
    return FALSE;
}
}

// Derselbe Ressourcenvergleich ohne Winamp und ohne Plugin-DLL trennt die
// Thread-/COM-Laufzeit von der eigentlichen Plugin-Lebensdauer.
int wmain(int count, wchar_t** arguments) {
    if (count != 2) return 2;
    const std::wstring mode = arguments[1];
    if (mode == L"--verify") {
        try { resource_checks::verify(); return 0; }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (mode != L"--threads" && mode != L"--com" && mode != L"--overlapping-com" && mode != L"--com-race" && mode != L"--dispatcher" && mode != L"--text-input" && mode != L"--reaper" && mode != L"--dpapi" && mode != L"--dialogs" && mode != L"--dialogs-slow") return 2;
    try {
        resource_checks::verify();
        focus_edit = mode == L"--dialogs-slow";
        auto exercise = [&] {
            if (mode == L"--dispatcher") {
                Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController> controller;
                const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
                if (FAILED(CreateDispatcherQueueController(options, &controller))) throw std::runtime_error("dispatcher_create");
                Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> completion;
                if (FAILED(controller->ShutdownQueueAsync(&completion))) throw std::runtime_error("dispatcher_shutdown");
                Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> information;
                if (FAILED(completion.As(&information))) throw std::runtime_error("dispatcher_completion_information");
                const auto deadline = GetTickCount64() + 5000;
                for (;;) {
                    ABI::Windows::Foundation::AsyncStatus status;
                    if (FAILED(information->get_Status(&status))) throw std::runtime_error("dispatcher_completion_status");
                    if (status != ABI::Windows::Foundation::AsyncStatus::Started) break;
                    if (GetTickCount64() >= deadline) throw std::runtime_error("dispatcher_shutdown_timeout");
                    resource_checks::pump(10);
                }
                if (FAILED(completion->GetResults())) throw std::runtime_error("dispatcher_shutdown_result");
                return;
            }
            if (mode == L"--text-input") {
                const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(initialized)) throw std::runtime_error("text_input_com_initialization");
                ITfThreadMgr* manager = nullptr;
                auto result = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
                if (SUCCEEDED(result)) {
                    TfClientId client = 0;
                    result = manager->Activate(&client);
                    if (SUCCEEDED(result)) result = manager->Deactivate();
                    manager->Release();
                }
                CoUninitialize();
                if (FAILED(result)) throw std::runtime_error("text_input_activation");
                return;
            }
            if (mode == L"--com-race") {
                std::atomic<bool> failed{false};
                std::vector<std::thread> workers;
                for (unsigned index = 0; index < 8; ++index) workers.emplace_back([&] {
                    for (unsigned repeat = 0; repeat < 20; ++repeat) {
                        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                        if (FAILED(initialized)) failed = true;
                        else CoUninitialize();
                    }
                });
                for (auto& worker : workers) worker.join();
                if (failed) throw std::runtime_error("racing_com_initialization");
                return;
            }
            if (mode == L"--dialogs" || mode == L"--dialogs-slow") {
                if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PLEX_SETTINGS_FRAME), nullptr, frame, 0) != IDOK)
                    throw std::runtime_error("native_dialog_fixture");
                return;
            }
            if (mode == L"--dpapi") {
                BYTE synthetic[]{1, 2, 3, 4};
                DATA_BLOB plain{sizeof(synthetic), synthetic}, encrypted{}, decrypted{};
                if (!CryptProtectData(&plain, L"Synthetic resource probe", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
                    throw std::runtime_error("probe_dpapi_protect");
                const auto success = CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted);
                LocalFree(encrypted.pbData);
                if (!success) throw std::runtime_error("probe_dpapi_unprotect");
                const bool valid = decrypted.cbData == sizeof(synthetic) && memcmp(decrypted.pbData, synthetic, sizeof(synthetic)) == 0;
                SecureZeroMemory(decrypted.pbData, decrypted.cbData); LocalFree(decrypted.pbData);
                if (!valid) throw std::runtime_error("probe_dpapi_roundtrip");
                return;
            }
            if (mode == L"--reaper") { plex::plugin::ViewReaper cleanup; return; }
            if (mode == L"--overlapping-com") {
                std::atomic<unsigned> entered{0};
                std::atomic<bool> released{false}, failed{false};
                std::vector<std::thread> workers;
                for (unsigned index = 0; index < 3; ++index) workers.emplace_back([&] {
                    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    if (FAILED(initialized)) failed = true;
                    ++entered;
                    while (!released) std::this_thread::yield();
                    if (SUCCEEDED(initialized)) CoUninitialize();
                });
                while (entered != 3) std::this_thread::yield();
                released = true;
                for (auto& worker : workers) worker.join();
                if (failed) throw std::runtime_error("overlapping_com_initialization");
                return;
            }
            HRESULT initialized = S_OK;
            std::thread worker([&] {
                if (mode == L"--com") {
                    initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    if (SUCCEEDED(initialized)) CoUninitialize();
                }
            });
            worker.join();
            if (FAILED(initialized)) throw std::runtime_error("probe_com_initialization");
        };
        if (mode == L"--com-race") {
            HRESULT initialized = E_FAIL;
            std::thread warmup([&] {
                initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (SUCCEEDED(initialized)) CoUninitialize();
            });
            warmup.join();
            if (FAILED(initialized)) throw std::runtime_error("com_race_warmup");
        } else if (mode != L"--dispatcher") exercise();
        if (mode == L"--dialogs-slow") dialog_interval = 250;
        const auto baseline = resource_checks::snapshot();
        DWORD initial = 0;
        if (!GetProcessHandleCount(GetCurrentProcess(), &initial)) throw std::runtime_error("probe_handle_count");
        resource_checks::counters("after_warmup");
        bool growth = false;
        for (unsigned iteration = 0; iteration < 500; ++iteration) {
            exercise();
            DWORD current = 0;
            if (!GetProcessHandleCount(GetCurrentProcess(), &current)) throw std::runtime_error("probe_handle_count");
            if (current > initial && !growth) {
                std::cerr << "first_growth_at_iteration=" << iteration + 1 << '\n';
                resource_checks::modules("first_growth");
                resource_checks::describe(baseline, "first_growth");
                growth = true;
            }
            if (iteration % 100 == 99) resource_checks::counters("after_100_more_cycles");
        }
        resource_checks::describe(baseline, "after_500_cycles");
        Sleep(1000);
        resource_checks::counters("after_1_second_idle");
        resource_checks::describe(baseline, "after_idle");
        if (mode == L"--com-race") {
            for (unsigned interval = 0; interval < 6; ++interval) {
                resource_checks::pump(5000);
                resource_checks::counters("after_5_more_seconds_pumped");
            }
            resource_checks::describe(baseline, "after_30_more_seconds_pumped");
        }
        std::cout << "observed_handle_growth=" << growth << '\n';
        return 0; // Globale Handlezahlen dienen ausschließlich der Diagnose.
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
