#pragma once
#include <windows.h>
#include <processsnapshot.h>
#include <psapi.h>
#include <map>
#include <set>
#include <string>
#include <iostream>
#include <stdexcept>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace resource_checks {
using Handles = std::map<std::wstring, std::set<ULONG_PTR>>;

struct HeapBaseline {
#ifdef _DEBUG
    _CrtMemState initial{};
#endif
    void capture() {
#ifdef _DEBUG
        _CrtMemCheckpoint(&initial);
#endif
    }
    void describe(const char* stage) const {
#ifdef _DEBUG
        _CrtMemState current{};
        _CrtMemCheckpoint(&current);
        const auto blocks = [&](int type) { return static_cast<long long>(current.lCounts[type]) - static_cast<long long>(initial.lCounts[type]); };
        const auto bytes = [&](int type) { return static_cast<long long>(current.lSizes[type]) - static_cast<long long>(initial.lSizes[type]); };
        std::cerr << stage << ": crt_normal_blocks=" << blocks(_NORMAL_BLOCK)
                  << ", crt_normal_bytes=" << bytes(_NORMAL_BLOCK)
                  << ", crt_client_blocks=" << blocks(_CLIENT_BLOCK)
                  << ", crt_client_bytes=" << bytes(_CLIENT_BLOCK) << '\n';
#endif
    }
};

// Ausschließlich Typen und numerische Handlewerte erfassen. Objektnamen können
// private Dateipfade enthalten und gehören nicht in veröffentlichbare Testlogs.
inline Handles snapshot(bool describe_threads = false) {
    HPSS captured = nullptr;
    HPSSWALK marker = nullptr;
    if (PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_BASIC_INFORMATION | PSS_CAPTURE_HANDLE_NAME_INFORMATION |
                           PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION, 0, &captured) != ERROR_SUCCESS)
        throw std::runtime_error("capture_handle_snapshot");
    try {
        if (PssWalkMarkerCreate(nullptr, &marker) != ERROR_SUCCESS) throw std::runtime_error("create_handle_marker");
        Handles result;
        PSS_HANDLE_ENTRY entry{};
        DWORD status = ERROR_SUCCESS;
        while ((status = PssWalkSnapshot(captured, PSS_WALK_HANDLES, marker, &entry, sizeof(entry))) == ERROR_SUCCESS) {
            auto type = entry.TypeName ? std::wstring(entry.TypeName, entry.TypeNameLength / sizeof(wchar_t)) : L"unknown";
            while (!type.empty() && type.back() == L'\0') type.pop_back();
            result[type].insert(reinterpret_cast<ULONG_PTR>(entry.Handle));
            if (describe_threads && entry.ObjectType == PSS_OBJECT_TYPE_THREAD &&
                (entry.Flags & PSS_HANDLE_HAVE_TYPE_SPECIFIC_INFORMATION)) {
                const auto& thread = entry.TypeSpecificInformation.Thread;
                HMODULE module = nullptr;
                wchar_t name[MAX_PATH]{};
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCWSTR>(thread.Win32StartAddress), &module))
                    GetModuleFileNameW(module, name, MAX_PATH);
                std::wstring filename(name);
                const auto separator = filename.find_last_of(L"/\\");
                if (separator != filename.npos) filename.erase(0, separator + 1);
                std::wcerr << L"Thread id=" << thread.ThreadId << L", owner=" << thread.ProcessId
                           << L", exit=" << thread.ExitStatus << L", start_module=" << filename << L'\n';
            }
        }
        if (status != ERROR_NO_MORE_ITEMS) throw std::runtime_error("walk_handle_snapshot");
        PssWalkMarkerFree(marker); marker = nullptr;
        PssFreeSnapshot(GetCurrentProcess(), captured); captured = nullptr;
        return result;
    } catch (...) {
        if (marker) PssWalkMarkerFree(marker);
        if (captured) PssFreeSnapshot(GetCurrentProcess(), captured);
        throw;
    }
}
inline void describe(const Handles& baseline, const char* stage) {
    const auto current = snapshot(true);
    std::cerr << "Handle types at " << stage << ":\n";
    std::set<std::wstring> types;
    for (const auto& type : baseline) types.insert(type.first);
    for (const auto& type : current) types.insert(type.first);
    for (const auto& type : types) {
        const auto found = baseline.find(type);
        const auto before = found == baseline.end() ? 0 : found->second.size();
        const auto present = current.find(type);
        const auto after = present == current.end() ? 0 : present->second.size();
        std::wcerr << L"  " << type << L": " << before << L" -> " << after << L'\n';
    }
}
// Ein nativer UI-Host verarbeitet zwischen Aktionen Windows-Nachrichten.
// Dieser Vergleich lässt die Plugin-DLL bereits entladen und pumpt nur den Host.
inline void pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) throw std::runtime_error("unexpected_test_quit");
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
inline void counters(const char* stage) {
    DWORD handles = 0;
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (!GetProcessHandleCount(GetCurrentProcess(), &handles) ||
        !GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        throw std::runtime_error("measure_process_resources");
    std::cerr << stage << ": handles=" << handles << ", private_bytes=" << memory.PrivateUsage << '\n';
}
inline void modules(const char* stage) {
    HMODULE loaded[512]{};
    DWORD required = 0;
    if (!EnumProcessModules(GetCurrentProcess(), loaded, sizeof(loaded), &required) || required > sizeof(loaded))
        throw std::runtime_error("resource_module_inventory");
    std::cerr << "Loaded modules at " << stage << ":\n";
    for (unsigned index = 0; index < required / sizeof(HMODULE); ++index) {
        wchar_t name[MAX_PATH]{};
        if (!GetModuleBaseNameW(GetCurrentProcess(), loaded[index], name, MAX_PATH))
            throw std::runtime_error("resource_module_name");
        std::wcerr << L"  " << name << L'\n';
    }
}
// Die Messung muss einen absichtlich offenen Event-Handle erkennen und dessen
// Freigabe bestätigen, bevor sie einen Plugin-Befund erklären darf.
inline void verify() {
#ifdef _DEBUG
    _CrtMemState before{}, allocated{}, freed{};
    _CrtMemCheckpoint(&before);
    void* allocation = _malloc_dbg(4096, _NORMAL_BLOCK, __FILE__, __LINE__);
    if (!allocation) throw std::runtime_error("heap_fixture_allocation");
    _CrtMemCheckpoint(&allocated);
    _free_dbg(allocation, _NORMAL_BLOCK);
    _CrtMemCheckpoint(&freed);
    if (allocated.lSizes[_NORMAL_BLOCK] != before.lSizes[_NORMAL_BLOCK] + 4096 ||
        freed.lSizes[_NORMAL_BLOCK] != before.lSizes[_NORMAL_BLOCK] ||
        freed.lCounts[_NORMAL_BLOCK] != before.lCounts[_NORMAL_BLOCK])
        throw std::runtime_error("heap_diagnostic_countercheck");
    std::cerr << "crt_heap_open_and_freed_allocation_countercheck=passed\n";
#endif
    snapshot();
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) throw std::runtime_error("diagnostic_event_fixture");
    const auto with_event = snapshot();
    const auto value = reinterpret_cast<ULONG_PTR>(event);
    const bool detected = with_event.count(L"Event") && with_event.at(L"Event").count(value);
    CloseHandle(event);
    const auto released = snapshot();
    if (!detected || (released.count(L"Event") && released.at(L"Event").count(value))) {
        for (const auto& type : with_event) std::wcerr << L"Fixture type " << type.first << L": " << type.second.size() << L'\n';
        std::cerr << "fixture_detected=" << detected << ", value_reused=" << (released.count(L"Event") && released.at(L"Event").count(value)) << '\n';
        throw std::runtime_error("handle_diagnostic_countercheck");
    }
    std::cerr << "handle_snapshot_open_and_closed_event_countercheck=passed\n";
}
}
