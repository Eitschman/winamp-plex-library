#pragma once

#include "plex/security.h"
#include <windows.h>
#include <memory>
#include <string>

namespace plex {
enum class HttpMethod { get, post };
enum class HttpRepresentation { json, image };
struct HttpError : std::exception {
    explicit HttpError(unsigned long value) noexcept : code(value) {}
    const char* what() const noexcept override { return "http_transport_error"; }
    unsigned long code;
};

// Asynchrone WinHTTP-Aufrufe werden ausschließlich auf Proxy-Workern erwartet.
class HttpStream {
public:
    HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
               bool head, const std::string& range, HANDLE stop);
    // API-Pfade stammen aus dem Client; der Aufruf bleibt an die konfigurierte Origin gebunden.
    HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
               HttpMethod method, const std::string& client_identifier, HANDLE stop,
               HttpRepresentation representation = HttpRepresentation::json);
    ~HttpStream();
    HttpStream(const HttpStream&) = delete;
    HttpStream& operator=(const HttpStream&) = delete;
    unsigned long status() const;
    std::string header(const wchar_t* name) const;
    unsigned long read(char* buffer, unsigned long size);
private:
    HttpStream(const ServerAddress& server, const std::string& token, const std::string& path,
               const wchar_t* method, const std::string& range, const std::string& client_identifier, HANDLE stop,
               const wchar_t* accept);
    struct State;
    std::unique_ptr<State> state_;
};
}
