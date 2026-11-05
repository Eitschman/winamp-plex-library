#include "plex/http_stream.h"
#include <winhttp.h>
#include <atomic>
#include <vector>

namespace plex {
namespace {
std::wstring widen(const std::string& value) {
    std::wstring result;
    for (const unsigned char character : value) result += static_cast<wchar_t>(character);
    return result;
}
void require(BOOL success) { if (!success) throw HttpError(GetLastError()); }
}

struct HttpStream::State {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stop = nullptr;
    std::atomic<DWORD> error{0};
    std::atomic<DWORD> bytes{0};
    bool context_installed = false;

    // Der Kontext lebt bis zur letzten HANDLE_CLOSING-Benachrichtigung.
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void* data, DWORD length) noexcept {
        auto* state = reinterpret_cast<State*>(context);
        if (!state) return;
        switch (status) {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            SetEvent(state->completed); break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            state->bytes.store(length);
            SetEvent(state->completed); break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            state->error.store(static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError);
            SetEvent(state->completed); break;
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
            SetEvent(state->closed); break;
        default: break;
        }
    }

    void prepare() {
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) throw HttpError(ERROR_CANCELLED);
        ResetEvent(completed); error.store(0); bytes.store(0);
    }

    void wait(BOOL started) {
        if (!started && GetLastError() != ERROR_IO_PENDING) throw HttpError(GetLastError());
        HANDLE events[] = {stop, completed};
        const auto outcome = WaitForMultipleObjects(2, events, FALSE, 15000);
        if (outcome == WAIT_OBJECT_0) throw HttpError(ERROR_CANCELLED);
        if (outcome != WAIT_OBJECT_0 + 1) throw HttpError(outcome == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
        if (error.load()) throw HttpError(error.load());
    }

    ~State() {
        if (request) {
            WinHttpCloseHandle(request);
            if (context_installed) WaitForSingleObject(closed, INFINITE);
        }
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        if (completed) CloseHandle(completed);
        if (closed) CloseHandle(closed);
    }
};

HttpStream::HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
                       bool head, const std::string& range, HANDLE stop)
    : HttpStream(server, token, (validate_media_path(path), path), head ? L"HEAD" : L"GET", range, "winamp-plex-library-poc", stop, L"*/*") {}

HttpStream::HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
                       HttpMethod method, const std::string& client_identifier, HANDLE stop, HttpRepresentation representation)
    : HttpStream(server, token, path, method == HttpMethod::post ? L"POST" : method == HttpMethod::put ? L"PUT" : L"GET", {}, client_identifier, stop,
                 representation == HttpRepresentation::image ? L"image/*" : L"application/json") {}

HttpStream::HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
                       const wchar_t* method, const std::string& range, const std::string& client_identifier, HANDLE stop, const wchar_t* accept)
    : state_(std::make_unique<State>()) {
    if (!token.empty()) validate_token(token);
    validate_token(client_identifier);
    if (path.empty() || path.front() != '/' || path.substr(0, 2) == "//" || path.size() > 8192 ||
        path.find_first_of("\r\n\\# ") != path.npos) throw std::invalid_argument("api_path");
    if (!range.empty()) validate_range(range);
    auto& state = *state_;
    state.stop = stop;
    if (!state.completed || !state.closed || !stop) throw HttpError(ERROR_NOT_ENOUGH_MEMORY);
    state.session = WinHttpOpen(L"Winamp Plex Library/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    require(state.session != nullptr);
    require(WinHttpSetTimeouts(state.session, 5000, 5000, 5000, 5000));
    const auto host = widen(server.host);
    state.connection = WinHttpConnect(state.session, host.c_str(), server.port, 0);
    require(state.connection != nullptr);
    const auto object = widen(path);
    state.request = WinHttpOpenRequest(state.connection, method, object.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, server.secure ? WINHTTP_FLAG_SECURE : 0);
    require(state.request != nullptr);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    require(WinHttpSetOption(state.request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)));
    policy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    require(WinHttpSetOption(state.request, WINHTTP_OPTION_AUTOLOGON_POLICY, &policy, sizeof(policy)));
    if (WinHttpSetStatusCallback(state.request, State::callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        throw HttpError(GetLastError());
    auto context = reinterpret_cast<DWORD_PTR>(&state);
    require(WinHttpSetOption(state.request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)));
    state.context_installed = true;
    auto headers = L"X-Plex-Product: Winamp Plex Library\r\nX-Plex-Version: 0.1.0\r\nX-Plex-Platform: Windows\r\n"
                   L"X-Plex-Client-Identifier: " + widen(client_identifier) + L"\r\nAccept: " + accept + L"\r\nAccept-Encoding: identity\r\n";
    if (!token.empty()) headers += L"X-Plex-Token: " + widen(token) + L"\r\n";
    if (!range.empty()) headers += L"Range: " + widen(range) + L"\r\n";
    state.prepare();
    const auto started = WinHttpSendRequest(state.request, headers.c_str(), static_cast<DWORD>(headers.size()),
                                            WINHTTP_NO_REQUEST_DATA, 0, 0, context);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    state.wait(started);
    state.prepare();
    state.wait(WinHttpReceiveResponse(state.request, nullptr));
}

HttpStream::~HttpStream() = default;

unsigned long HttpStream::status() const {
    DWORD result = 0;
    DWORD size = sizeof(result);
    require(WinHttpQueryHeaders(state_->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &result, &size, WINHTTP_NO_HEADER_INDEX));
    return result;
}

std::string HttpStream::header(const wchar_t* name) const {
    DWORD size = 0;
    WinHttpQueryHeaders(state_->request, WINHTTP_QUERY_CUSTOM, name, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 8192) throw HttpError(ERROR_INVALID_DATA);
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1);
    require(WinHttpQueryHeaders(state_->request, WINHTTP_QUERY_CUSTOM, name, value.data(), &size, WINHTTP_NO_HEADER_INDEX));
    std::string result;
    for (const wchar_t character : std::wstring(value.data())) {
        if (character < 32 || character > 126) throw HttpError(ERROR_INVALID_DATA);
        result += static_cast<char>(character);
    }
    return result;
}

unsigned long HttpStream::read(char* buffer, unsigned long size) {
    state_->prepare();
    state_->wait(WinHttpReadData(state_->request, buffer, size, nullptr));
    return state_->bytes.load();
}
}
