#include "artwork_provider.h"
#include <filesystem>
#include "wasabi_sdk.h"
#include <cstring>
#include <mutex>
#include <atomic>
#include <stdexcept>

namespace plex::plugin {
namespace {
std::mutex artwork_mutex;
std::unordered_map<std::wstring, ArtworkBytes> artwork;
std::unordered_map<const void*, unsigned> references;
std::size_t artwork_size = 0;
api_service* service_api = nullptr;
waServiceFactory* memory_factory = nullptr;
api_memmgr* memory_api = nullptr;
bool registered = false;
std::atomic<unsigned> ownership_queries{0}, data_queries{0}, successful_queries{0};

// Der Host erhält ausschließlich bereits geladene Bilder. Kein Callback führt HTTP aus.
class Provider final : public svc_albumArtProvider {
public:
    bool owns(const wchar_t* filename) {
        ++ownership_queries;
        std::lock_guard<std::mutex> lock(artwork_mutex);
        return filename && artwork.find(filename) != artwork.end();
    }
    // Winamp 5.9.2 berücksichtigt für HTTP-Titel nur die dateibezogene EMBEDDED-Kategorie.
    // Das Bild bleibt separat zugeordnet; weder Medienbytes noch Plex-Tags werden verändert.
    int kind() { return ALBUMARTPROVIDER_TYPE_EMBEDDED; }
    int get(const wchar_t* filename, const wchar_t* type, void** bits, size_t* length, wchar_t** mime) {
        ++data_queries;
        if (!bits || !length || !mime) return ALBUMARTPROVIDER_FAILURE;
        *bits = nullptr; *length = 0; *mime = nullptr;
        if (!filename || !type || _wcsicmp(type, L"cover")) return ALBUMARTPROVIDER_FAILURE;
        try {
            std::lock_guard<std::mutex> lock(artwork_mutex);
            const auto found = artwork.find(filename);
            if (found == artwork.end() || !memory_api) return ALBUMARTPROVIDER_FAILURE;
            auto* data = memory_api->sysMalloc(found->second->size());
            auto* format = static_cast<wchar_t*>(memory_api->sysMalloc(sizeof(L"png")));
            if (!data || !format) {
                if (data) memory_api->sysFree(data);
                if (format) memory_api->sysFree(format);
                return ALBUMARTPROVIDER_FAILURE;
            }
            std::memcpy(data, found->second->data(), found->second->size());
            // Der SDK-Providervertrag verwendet die Dateitypkennung, keinen HTTP-MIME-Typ.
            std::memcpy(format, L"png", sizeof(L"png"));
            *bits = data; *length = found->second->size(); *mime = format;
            ++successful_queries;
            return ALBUMARTPROVIDER_SUCCESS;
        } catch (...) { return ALBUMARTPROVIDER_FAILURE; }
    }
    int _dispatch(int message, void* result, void** parameters, int) override {
        try {
            switch (message) {
                case SVC_ALBUMARTPROVIDER_ISMINE: cb(&Provider::owns, result, parameters); break;
                case SVC_ALBUMARTPROVIDER_PROVIDERTYPE: cb(&Provider::kind, result, parameters); break;
                case SVC_ALBUMARTPROVIDER_GETALBUMARTDATA: cb(&Provider::get, result, parameters); break;
                case SVC_ALBUMARTPROVIDER_SETALBUMARTDATA:
                case SVC_ALBUMARTPROVIDER_DELETEALBUMART: *static_cast<int*>(result) = ALBUMARTPROVIDER_READONLY; break;
                default: return 0;
            }
            return 1;
        } catch (...) { return 0; }
    }
} provider;

class Factory final : public waServiceFactory {
public:
    FOURCC type() { return svc_albumArtProvider::getServiceType(); }
    const char* name() { return "Winamp Plex Library artwork"; }
    GUID guid() { return {0xf5f5bfd2, 0x7ab3, 0x4d71, {0xa5, 0x37, 0x87, 0x53, 0x8e, 0x61, 0x28, 0x14}}; }
    void* get(int) { return &provider; }
    int release(void*) { return 1; }
    int nonlocking() { return 1; }
    int notify(int, intptr_t, intptr_t) { return 1; }
    int _dispatch(int message, void* result, void** parameters, int) override {
        try {
            switch (message) {
                case WASERVICEFACTORY_GETSERVICETYPE: cb(&Factory::type, result, parameters); break;
                case WASERVICEFACTORY_GETSERVICENAME: cb(&Factory::name, result, parameters); break;
                case WASERVICEFACTORY_GETGUID: cb(&Factory::guid, result, parameters); break;
                case WASERVICEFACTORY_GETINTERFACE: cb(&Factory::get, result, parameters); break;
                case WASERVICEFACTORY_RELEASEINTERFACE: cb(&Factory::release, result, parameters); break;
                case WASERVICEFACTORY_SUPPORTNONLOCKINGGETINTERFACE: cb(&Factory::nonlocking, result, parameters); break;
                case WASERVICEFACTORY_SERVICENOTIFY: cb(&Factory::notify, result, parameters); break;
                default: return 0;
            }
            return 1;
        } catch (...) { return 0; }
    }
} factory;
}

void initialize_artwork_provider(api_service* services) {
    ownership_queries = 0; data_queries = 0; successful_queries = 0;
    if (!services || services == reinterpret_cast<api_service*>(1)) throw std::runtime_error("artwork_service_missing");
    service_api = services;
    memory_factory = services->service_getServiceByGuid(memMgrApiServiceGuid);
    if (!memory_factory) throw std::runtime_error("artwork_memory_service_missing");
    memory_api = static_cast<api_memmgr*>(memory_factory->getInterface());
    if (!memory_api) throw std::runtime_error("artwork_memory_interface_missing");
    services->service_register(&factory);
    registered = true;
    if (services->service_getServiceByGuid(factory.guid()) != &factory) throw std::runtime_error("artwork_registration_failed");
}
void clear_playback_artwork() noexcept {
    std::lock_guard<std::mutex> lock(artwork_mutex); artwork.clear(); references.clear(); artwork_size = 0;
}
void shutdown_artwork_provider() noexcept {
    if (service_api && registered) service_api->service_deregister(&factory);
    api_memmgr* released = nullptr;
    { std::lock_guard<std::mutex> lock(artwork_mutex);
      artwork.clear(); references.clear(); artwork_size = 0; released = memory_api; memory_api = nullptr; }
    if (memory_factory && released) memory_factory->releaseInterface(released);
    registered = false;
    memory_factory = nullptr; service_api = nullptr;
}
void bind_playback_artwork(const std::wstring& url, ArtworkBytes bytes) {
    if (!bytes) return;
    if (bytes->empty() || bytes->size() > 300000 ||
        (url.compare(0, 17, L"http://127.0.0.1:") != 0 && !std::filesystem::path(url).is_absolute()))
        throw std::invalid_argument("playback_artwork");
    std::lock_guard<std::mutex> lock(artwork_mutex);
    if (artwork.count(url) || artwork.size() >= 10000) throw std::invalid_argument("playback_artwork_limit");
    const bool shared = references.count(bytes.get()) != 0;
    if (!shared && artwork_size + bytes->size() > 64 * 1024 * 1024) throw std::invalid_argument("playback_artwork_limit");
    const auto inserted = artwork.emplace(url, bytes);
    try { ++references[bytes.get()]; }
    catch (...) { artwork.erase(inserted.first); throw; }
    if (!shared) artwork_size += bytes->size();
}
ArtworkBytes playback_artwork(const std::wstring& filename) {
    std::lock_guard<std::mutex> lock(artwork_mutex);
    const auto found = artwork.find(filename);
    return found == artwork.end() ? nullptr : found->second;
}
void unbind_playback_artwork(const std::wstring& url) noexcept {
    std::lock_guard<std::mutex> lock(artwork_mutex);
    const auto found = artwork.find(url);
    if (found == artwork.end()) return;
    const auto reference = references.find(found->second.get());
    if (reference != references.end() && --reference->second == 0) {
        artwork_size -= found->second->size(); references.erase(reference);
    }
    artwork.erase(found);
}
unsigned artwork_provider_diagnostic(unsigned counter) noexcept {
    if (counter == 0) return ownership_queries.load();
    if (counter == 1) return data_queries.load();
    if (counter == 2) return successful_queries.load();
    if (counter == 3) { std::lock_guard<std::mutex> lock(artwork_mutex); return static_cast<unsigned>(artwork.size()); }
    if (counter == 4) return registered ? 1U : 0U;
    return 0;
}
}
