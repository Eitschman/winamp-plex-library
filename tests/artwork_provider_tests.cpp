#include "plugin/artwork_provider.h"
#include "plex/artwork.h"
#include "wasabi_mock.h"
#include <Agave/AlbumArt/svc_albumArtProvider.h>
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Action> void rejects(Action action, const char* name) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}
}
int main() {
    using namespace plex::plugin;
    MockServices services;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 2;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    try {
        rejects([] { initialize_artwork_provider(nullptr); }, "missing_service_rejected");
        services.memory_available = false;
        rejects([&] { initialize_artwork_provider(&services); }, "missing_memory_rejected");
        shutdown_artwork_provider(); services.memory_available = true;
        services.memory.enabled = false;
        rejects([&] { initialize_artwork_provider(&services); }, "missing_memory_interface_rejected");
        shutdown_artwork_provider(); services.memory.enabled = true;
        services.reject_registration = true;
        rejects([&] { initialize_artwork_provider(&services); }, "rejected_registration_detected");
        shutdown_artwork_provider(); services.reject_registration = false;
        initialize_artwork_provider(&services);
        auto* provider = static_cast<svc_albumArtProvider*>(services.registered->getInterface());
        check(provider->ProviderType() == ALBUMARTPROVIDER_TYPE_EMBEDDED, "stream_provider_type");
        check(!provider->IsMine(L"https://foreign.example/file.mp3"), "foreign_url_ignored");
        const std::wstring url = L"http://127.0.0.1:12345/stream/012345678901234567890123456789012345678901234567/audio.mp3";
        auto pixels = plex::ArtworkPixels{1, 1, {10, 20, 30, 255}};
        const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(plex::encode_artwork_png(pixels));
        bind_playback_artwork(url, bytes);
        check(provider->IsMine(url.c_str()), "owned_url_recognized");
        check(artwork_provider_diagnostic(3) == 1 && artwork_provider_diagnostic(4) == 1, "provider_diagnostics_registered_and_bound");
        rejects([&] { bind_playback_artwork(L"https://foreign.example/file.mp3", bytes); }, "foreign_cover_binding_rejected");
        rejects([&] { bind_playback_artwork(url, bytes); }, "duplicate_binding_rejected");
        void* bits = nullptr; size_t length = 0; wchar_t* mime = nullptr;
        check(provider->GetAlbumArtData(url.c_str(), L"cover", &bits, &length, &mime) == ALBUMARTPROVIDER_SUCCESS,
              "cached_cover_returned");
        check(length == bytes->size() && !std::memcmp(bits, bytes->data(), length) && !wcscmp(mime, L"png"), "cover_bytes_and_sdk_type_identifier");
        services.memory.memory.sysFree(bits); services.memory.memory.sysFree(mime);
        check(services.memory.memory.allocations == 0, "host_allocator_balanced");
        services.memory.memory.fail_after = 1;
        check(provider->GetAlbumArtData(url.c_str(), L"cover", &bits, &length, &mime) == ALBUMARTPROVIDER_FAILURE && !bits && !mime && length == 0,
              "partial_allocation_failure_cleans_up");
        check(services.memory.memory.allocations == 0, "failed_allocation_balanced");
        services.memory.memory.fail_after = -1;
        check(provider->GetAlbumArtData(url.c_str(), L"artist", &bits, &length, &mime) == ALBUMARTPROVIDER_FAILURE, "unsupported_art_type");
        check(provider->SetAlbumArtData(url.c_str(), L"cover", nullptr, 0, nullptr) == ALBUMARTPROVIDER_READONLY, "artwork_readonly");
        unbind_playback_artwork(url);
        check(!provider->IsMine(url.c_str()), "revoked_mapping_removed");
        const std::wstring local_file = L"C:\\private\\playback\\session-test\\012345678901234567890123456789012345678901234567.flac";
        bind_playback_artwork(local_file, bytes);
        check(provider->IsMine(local_file.c_str()), "prepared_file_cover_recognized");
        check(provider->GetAlbumArtData(local_file.c_str(), L"cover", &bits, &length, &mime) == ALBUMARTPROVIDER_SUCCESS,
              "prepared_file_cover_returned");
        services.memory.memory.sysFree(bits); services.memory.memory.sysFree(mime);
        unbind_playback_artwork(local_file);
        check(provider->GetAlbumArtData(url.c_str(), L"cover", &bits, &length, &mime) == ALBUMARTPROVIDER_FAILURE, "no_stale_cover");
        services.registered->releaseInterface(provider);
        shutdown_artwork_provider();
        check(!services.registered && services.memory.releases == 2, "service_lifecycle_balanced");
        std::cout << checks << " artwork provider checks passed\n"; return 0;
    } catch (const std::exception& error) {
        shutdown_artwork_provider(); std::cerr << error.what() << '\n'; return 1;
    }
}
