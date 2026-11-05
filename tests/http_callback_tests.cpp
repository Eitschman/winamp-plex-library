#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <atomic>
#include <iostream>

namespace {
WINHTTP_STATUS_CALLBACK original_callback = nullptr;
HANDLE callback_entered = nullptr;
HANDLE callback_release = nullptr;
HANDLE error_entered = nullptr;
HANDLE error_release = nullptr;
HANDLE request_closed = nullptr;
HINTERNET watched_request = nullptr;
std::atomic<bool> gate_timeout{false};

// Die ursprüngliche Abschlussmeldung wird zugestellt, der Callback bleibt
// danach gezielt aktiv. Eine DLL darf in diesem Zustand nicht entladen werden.
void CALLBACK gated_callback(HINTERNET handle, DWORD_PTR context, DWORD status, void* data, DWORD length) {
    original_callback(handle, context, status, data, length);
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && context) {
        SetEvent(error_entered);
        if (WaitForSingleObject(error_release, 10000) != WAIT_OBJECT_0) gate_timeout.store(true);
    }
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING && context) {
        SetEvent(callback_entered);
        if (WaitForSingleObject(callback_release, 10000) != WAIT_OBJECT_0) gate_timeout.store(true);
    }
}
WINHTTP_STATUS_CALLBACK gated_set_callback(HINTERNET handle, WINHTTP_STATUS_CALLBACK callback, DWORD flags, DWORD_PTR reserved) {
    original_callback = callback;
    watched_request = handle;
    return WinHttpSetStatusCallback(handle, gated_callback, flags, reserved);
}
BOOL gated_close(HINTERNET handle) {
    const auto outcome = WinHttpCloseHandle(handle);
    if (handle == watched_request) SetEvent(request_closed);
    return outcome;
}
}

// Nur diese Testübersetzung ersetzt die Callback-Registrierung. Der reguläre
// Transport bleibt unverändert; es gibt keinen Laufzeit-Testschalter im Plugin.
#define WinHttpSetStatusCallback gated_set_callback
#define WinHttpCloseHandle gated_close
#include "../src/plex/http_stream.cpp"
#undef WinHttpSetStatusCallback
#undef WinHttpCloseHandle

int main() {
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets)) return 2;
    const auto reserved = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int size = sizeof(address);
    if (reserved == INVALID_SOCKET || bind(reserved, reinterpret_cast<sockaddr*>(&address), size) ||
        getsockname(reserved, reinterpret_cast<sockaddr*>(&address), &size)) return 2;
    // Der gebundene Port hat absichtlich keinen Listener: kein fremder Dienst.
    callback_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    callback_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    error_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    error_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    request_closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const auto destroyed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!callback_entered || !callback_release || !error_entered || !error_release || !request_closed || !destroyed || !stop) return 2;
    std::atomic<bool> rejected{false};
    std::atomic<bool> unexpected_error{false};
    std::thread worker([&] {
        try {
            plex::HttpStream stream({"127.0.0.1", ntohs(address.sin_port), false}, "synthetic-test-token", "/library/parts/1/file.mp3", false, {}, stop);
        } catch (const plex::HttpError&) { rejected.store(true); }
          catch (...) { unexpected_error.store(true); }
        SetEvent(destroyed);
    });
    const bool error_active = WaitForSingleObject(error_entered, 10000) == WAIT_OBJECT_0;
    const bool closed = error_active && WaitForSingleObject(request_closed, 5000) == WAIT_OBJECT_0;
    SetEvent(error_release);
    const bool entered = WaitForSingleObject(callback_entered, 10000) == WAIT_OBJECT_0;
    const bool waits = entered && WaitForSingleObject(destroyed, 200) == WAIT_TIMEOUT;
    SetEvent(callback_release);
    worker.join();
    const bool completed = WaitForSingleObject(destroyed, 0) == WAIT_OBJECT_0;
    CloseHandle(stop); CloseHandle(destroyed); CloseHandle(callback_release); CloseHandle(callback_entered);
    CloseHandle(error_entered); CloseHandle(error_release);
    CloseHandle(request_closed);
    closesocket(reserved); WSACleanup();
    if (!closed || !error_active || !entered || !waits || !completed || !rejected.load() || unexpected_error.load() || gate_timeout.load()) {
        std::cerr << "FAILED: transport_waits_until_last_callback_returns\n";
        return 1;
    }
    std::cout << "transport_waits_until_last_callback_returns\n";
    return 0;
}
