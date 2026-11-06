#include "plex/local_media.h"
#include "plex/http_stream.h"
#include "plex/aac_remux.h"
#include <bcrypt.h>
#include <sddl.h>
#include <array>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <atomic>

namespace plex {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE release() noexcept { const auto result = value; value = INVALID_HANDLE_VALUE; return result; }
};
struct LocalMemory {
    HLOCAL value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};
constexpr char marker[] = "WinampPlexLocalMedia1\r\n";
void cancelled(HANDLE stop) {
    const auto result = WaitForSingleObject(stop, 0);
    if (result == WAIT_OBJECT_0) throw HttpError(ERROR_CANCELLED);
    if (result != WAIT_TIMEOUT) throw HttpError(ERROR_INVALID_HANDLE);
}
std::wstring random_name() {
    std::array<unsigned char, 24> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("local_media_random");
    std::wstring result;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (const auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
bool hex_name(std::wstring_view name) {
    if (name.size() != 48) return false;
    for (const auto character : name) if (!((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f'))) return false;
    return true;
}
bool owned_file(std::wstring_view name) {
    return name.size() > 48 && hex_name(name.substr(0, 48)) && (name.substr(48) == L".flac" || name.substr(48) == L".ogg" || name.substr(48) == L".aac" || name.substr(48) == L".m4a" || name.substr(48) == L".buffer");
}

// Nur der aktuelle Windows-Benutzer und SYSTEM erhalten Zugriff, auch wenn der
// übergeordnete Ordner weiter gefasste vererbte Berechtigungen besitzt.
void private_directory(const std::filesystem::path& path) {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) throw HttpError(GetLastError());
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    if (!size || size > 65536) throw HttpError(ERROR_INVALID_DATA);
    std::vector<unsigned char> information(size);
    if (!GetTokenInformation(token.value, TokenUser, information.data(), size, &size)) throw HttpError(GetLastError());
    wchar_t* sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(information.data())->User.Sid, &sid)) throw HttpError(GetLastError());
    LocalMemory sid_memory{sid};
    const auto description = std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;") + sid + L")";
    PSECURITY_DESCRIPTOR security = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(description.c_str(), SDDL_REVISION_1, &security, nullptr)) throw HttpError(GetLastError());
    LocalMemory security_memory{security};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
    if (!CreateDirectoryW(path.c_str(), &attributes)) throw HttpError(GetLastError());
}

// Nach einem Prozessabbruch werden ausschließlich eigene, markierte und nicht
// mehr gesperrte Sitzungen bereinigt. Fremde Dateien und Reparse Points bleiben stehen.
void remove_inactive_sessions(const std::filesystem::path& root) {
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().wstring();
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (name.size() != 56 || name.substr(0, 8) != L"session-" || !hex_name(std::wstring_view(name).substr(8)) ||
            attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const auto owner = entry.path() / L"owner.lock";
        Handle lock{CreateFileW(owner.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (lock.value == INVALID_HANDLE_VALUE) continue;
        BY_HANDLE_FILE_INFORMATION info{};
        std::array<char, sizeof(marker)> content{}; DWORD read = 0;
        if (!GetFileInformationByHandle(lock.value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            info.nFileSizeHigh || info.nFileSizeLow != sizeof(marker) ||
            !ReadFile(lock.value, content.data(), static_cast<DWORD>(content.size()), &read, nullptr) ||
            read != sizeof(marker)) continue;
        // Der Marker wird bytegenau geprüft; ein Dateiname allein reicht nicht.
        if (std::string_view(content.data(), read) != std::string_view(marker, sizeof(marker))) continue;
        std::vector<std::filesystem::path> files;
        bool safe = true;
        for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
            if (file.path().filename() == L"owner.lock") continue;
            const auto flags = GetFileAttributesW(file.path().c_str());
            if (!owned_file(file.path().filename().wstring()) || flags == INVALID_FILE_ATTRIBUTES ||
                (flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { safe = false; break; }
            files.push_back(file.path());
        }
        if (!safe) continue;
        for (const auto& file : files) if (!DeleteFileW(file.c_str())) throw HttpError(GetLastError());
        CloseHandle(lock.release());
        if (!DeleteFileW(owner.c_str()) || !RemoveDirectoryW(entry.path().c_str())) throw HttpError(GetLastError());
    }
}
}

struct LocalMediaState {
    std::filesystem::path root, session;
    LocalMediaLimits limits;
    mutable std::mutex mutex;
    std::atomic<unsigned long long> used{0};
    Handle owner;
    std::vector<std::pair<std::filesystem::path, unsigned long long>> deferred;
    bool cleanup_pending = false;
    LocalMediaState(std::filesystem::path value, LocalMediaLimits bounds) : root(std::move(value)), limits(bounds) {}
    void ensure_directory() {
        if (!session.empty()) return;
        std::filesystem::create_directories(root);
        const auto flags = GetFileAttributesW(root.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT)) throw HttpError(ERROR_ACCESS_DENIED);
        remove_inactive_sessions(root);
        const auto candidate = root / (L"session-" + random_name());
        private_directory(candidate);
        Handle lock{CreateFileW((candidate / L"owner.lock").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr)};
        DWORD written = 0;
        if (lock.value == INVALID_HANDLE_VALUE || !WriteFile(lock.value, marker, sizeof(marker), &written, nullptr) || written != sizeof(marker)) {
            const auto error = GetLastError();
            if (lock.value != INVALID_HANDLE_VALUE) {
                CloseHandle(lock.release()); DeleteFileW((candidate / L"owner.lock").c_str());
            }
            RemoveDirectoryW(candidate.c_str());
            throw HttpError(error ? error : ERROR_WRITE_FAULT);
        }
        session = candidate; owner.value = lock.release();
    }
    void reclaim() {
        for (auto entry = deferred.begin(); entry != deferred.end();) {
            if (DeleteFileW(entry->first.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
                used -= entry->second; entry = deferred.erase(entry);
            } else ++entry;
        }
    }
    ~LocalMediaState() {
        reclaim();
        if (!session.empty()) {
            if (owner.value != INVALID_HANDLE_VALUE) CloseHandle(owner.release());
            if (deferred.empty() && !cleanup_pending) { DeleteFileW((session / L"owner.lock").c_str()); RemoveDirectoryW(session.c_str()); }
        }
    }
};

LocalMediaFile::LocalMediaFile(std::shared_ptr<LocalMediaState> state, std::filesystem::path path, unsigned long long size, HANDLE guard)
    : state_(std::move(state)), path_(std::move(path)), size_(size), guard_(guard) {}
LocalMediaFile::~LocalMediaFile() {
    CloseHandle(guard_);
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) state_->used -= size_;
    else {
        // Auch bei Speichermangel bleibt der Sitzungsmarker für die nächste Bereinigung erhalten.
        try { state_->deferred.emplace_back(std::move(path_), size_); }
        catch (...) { state_->cleanup_pending = true; }
    }
}

LocalMediaBuffer::LocalMediaBuffer(std::shared_ptr<LocalMediaState> state, std::filesystem::path path,
                                 HANDLE file, unsigned long long reserved, unsigned long long limit)
    : state_(std::move(state)), path_(std::move(path)), file_(file), reserved_(reserved), limit_(limit) {}
LocalMediaBuffer::~LocalMediaBuffer() {
    CloseHandle(file_);
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) state_->used -= reserved_;
    else {
        try { state_->deferred.emplace_back(std::move(path_), reserved_); }
        catch (...) { state_->cleanup_pending = true; }
    }
}
void LocalMediaBuffer::append(const char* data, unsigned long size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size > limit_ - written_) throw HttpError(ERROR_FILE_TOO_LARGE);
    if (written_ + size > reserved_) {
        std::lock_guard<std::mutex> budget(state_->mutex);
        const auto extra = written_ + size - reserved_;
        if (extra > state_->limits.total_bytes - state_->used) throw HttpError(ERROR_DISK_FULL);
        state_->used += extra; reserved_ += extra;
    }
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(written_);
    if (!SetFilePointerEx(file_, position, nullptr, FILE_BEGIN)) throw HttpError(GetLastError());
    DWORD actual = 0;
    if (!WriteFile(file_, data, size, &actual, nullptr) || actual != size) throw HttpError(ERROR_WRITE_FAULT);
    written_ += actual;
}
unsigned long LocalMediaBuffer::read(unsigned long long offset, char* data, unsigned long capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (offset > written_) throw HttpError(ERROR_INVALID_DATA);
    const auto count = static_cast<DWORD>((std::min)(written_ - offset, static_cast<unsigned long long>(capacity)));
    if (!count) return 0;
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file_, position, nullptr, FILE_BEGIN)) throw HttpError(GetLastError());
    DWORD actual = 0;
    if (!ReadFile(file_, data, count, &actual, nullptr) || actual != count) throw HttpError(ERROR_READ_FAULT);
    return actual;
}

std::shared_ptr<LocalMediaBuffer> LocalMediaStore::buffer(unsigned long long expected_bytes) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (expected_bytes > state_->limits.file_bytes) throw HttpError(ERROR_FILE_TOO_LARGE);
    state_->ensure_directory(); state_->reclaim();
    if (expected_bytes > state_->limits.total_bytes - state_->used) throw HttpError(ERROR_DISK_FULL);
    const auto path = state_->session / (random_name() + L".buffer");
    Handle file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_RANDOM_ACCESS, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) throw HttpError(GetLastError());
    // Erst nach erfolgreicher Ressourcenübernahme wird das gemeinsame Budget belastet.
    LocalMediaBuffer* result = nullptr;
    try { result = new LocalMediaBuffer(state_, path, file.value, expected_bytes, expected_bytes ? expected_bytes : state_->limits.file_bytes); }
    catch (...) {
        CloseHandle(file.release());
        if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) state_->cleanup_pending = true;
        throw;
    }
    file.release(); state_->used += expected_bytes;
    // Der shared_ptr kann selbst bei Speichermangel den Destruktor aufrufen.
    // Dessen Budgetfreigabe darf nicht unter dem bereits gehaltenen Mutex erfolgen.
    lock.unlock();
    return std::shared_ptr<LocalMediaBuffer>(result);
}
bool requires_local_media(const std::string& path) {
    const auto extension = media_extension(path);
    return extension == ".flac" || extension == ".ogg" || extension == ".aac";
}
LocalMediaStore::LocalMediaStore(std::filesystem::path root, LocalMediaLimits limits) {
    if (!root.is_absolute() || !limits.file_bytes || limits.total_bytes < limits.file_bytes) throw std::invalid_argument("local_media_settings");
    state_ = std::make_shared<LocalMediaState>(std::move(root), limits);
}
std::filesystem::path LocalMediaStore::directory() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->session; }
// UI-Diagnosen dürfen nicht auf laufende Dateibereinigung warten. Änderungen
// des Budgets bleiben durch den Zustandsmutex gemeinsam mit den Dateien geschützt.
unsigned long long LocalMediaStore::bytes_used() const { return state_->used.load(); }

std::shared_ptr<const LocalMediaFile> LocalMediaStore::download(const PlaybackSource& source, HANDLE stop) {
    cancelled(stop);
    if (!requires_local_media(source.media_path)) throw std::invalid_argument("local_media_format");
    HttpStream stream(source.server, source.token, source.media_path, false, {}, stop);
    if (stream.status() != 200) throw HttpError(0x20000000UL + stream.status());
    const auto encoding = stream.header(L"Content-Encoding");
    const auto length = stream.header(L"Content-Length");
    if ((!encoding.empty() && encoding != "identity") || length.empty() || length.size() > 19 || length.find_first_not_of("0123456789") != length.npos)
        throw HttpError(ERROR_INVALID_DATA);
    const auto size = std::stoull(length);
    if (!size || size > state_->limits.file_bytes) throw HttpError(ERROR_FILE_TOO_LARGE);
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->ensure_directory(); state_->reclaim();
        if (size > state_->limits.total_bytes - state_->used) throw HttpError(ERROR_DISK_FULL);
        const auto extension = media_extension(source.media_path);
        path = state_->session / (random_name() + std::wstring(extension.begin(), extension.end()));
        state_->used += size;
    }
    bool created = false;
    bool owned = false;
    try {
        Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) throw HttpError(GetLastError());
        created = true;
        std::array<char, 65536> buffer{};
        unsigned long long transferred = 0;
        for (;;) {
            cancelled(stop);
            const auto count = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!count) break;
            if (count > size - transferred) throw HttpError(ERROR_INVALID_DATA);
            DWORD written = 0;
            if (!WriteFile(file.value, buffer.data(), count, &written, nullptr) || written != count) throw HttpError(ERROR_WRITE_FAULT);
            transferred += count;
        }
        if (transferred != size) throw HttpError(ERROR_HANDLE_EOF);
        cancelled(stop);
        if (!CloseHandle(file.release())) throw HttpError(GetLastError());
        Handle guard{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (guard.value == INVALID_HANDLE_VALUE) throw HttpError(GetLastError());
        auto* prepared = new LocalMediaFile(state_, path, size, guard.value);
        guard.release();
        owned = true;
        auto result = std::shared_ptr<const LocalMediaFile>(prepared);
        return result;
    } catch (...) {
        if (owned) throw;
        const bool removed = !created || DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (removed) state_->used -= size;
        else {
            try { state_->deferred.emplace_back(std::move(path), size); }
            catch (...) { state_->cleanup_pending = true; }
        }
        throw;
    }
}

std::shared_ptr<const LocalMediaFile> LocalMediaStore::stage(const PlaybackSource& source, HANDLE stop) {
    auto input = download(source, stop);
    if (media_extension(source.media_path) != ".aac") return input;
    // Der reservierte Platz umfasst während der Umverpackung Quelle und Ziel.
    const auto budget = (std::min)(state_->limits.file_bytes, input->size() * 4 + 65536);
    std::filesystem::path output;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (budget > state_->limits.total_bytes - state_->used) throw HttpError(ERROR_DISK_FULL);
        output = state_->session / (random_name() + L".m4a");
        state_->used += budget;
    }
    bool created = false, owned = false;
    unsigned long long reserved = budget;
    try {
        const auto result = remux_aac(input->path(), output, stop, budget, &created);
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->used -= budget - result.bytes; reserved = result.bytes;
        }
        input.reset();
        Handle guard{CreateFileW(output.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (guard.value == INVALID_HANDLE_VALUE) throw HttpError(GetLastError());
        auto* prepared = new LocalMediaFile(state_, output, reserved, guard.value);
        guard.release(); owned = true;
        return std::shared_ptr<const LocalMediaFile>(prepared);
    } catch (...) {
        if (owned) throw;
        const bool removed = !created || DeleteFileW(output.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (removed) state_->used -= reserved;
        else {
            try { state_->deferred.emplace_back(std::move(output), reserved); }
            catch (...) { state_->cleanup_pending = true; }
        }
        throw;
    }
}
}
