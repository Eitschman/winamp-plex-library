#include "plex/artwork.h"
#include "ui/artwork_loader.h"
#include <objbase.h>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Action> void rejects(Action action, const char* name) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, name);
}
}

int main() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 2;
    try {
        for (const auto* path : {"/library/metadata/42/thumb", "/library/metadata/42/thumb/123"}) {
            plex::validate_artwork_path(path); check(true, "relative_artwork_path");
        }
        for (const auto* path : {"https://other.example/image.jpg", "//other.example/image", "/library/metadata/42/thumb?X-Plex-Token=fake",
             "/library/metadata/../thumb/123", "/library/metadata/42/thumb/../../file", "/library/metadata/42/thumb/1\r\nInjected: yes"})
            rejects([&] { plex::validate_artwork_path(path); }, "unsafe_artwork_path_rejected");
        // Zweifarbiges synthetisches BMP, zwei Pixel breit und ein Pixel hoch.
        std::vector<std::uint8_t> bitmap(62, 0);
        bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 62; bitmap[10] = 54;
        bitmap[14] = 40; bitmap[18] = 2; bitmap[22] = 1; bitmap[26] = 1; bitmap[28] = 24;
        bitmap[56] = 255; bitmap[58] = 255;
        const auto decoded = plex::decode_artwork(bitmap);
        check(decoded.width == 48 && decoded.height == 24 && decoded.bgra.size() == 48 * 24 * 4, "aspect_ratio_and_bounded_output");
        check(decoded.bgra[2] == 255 && decoded.bgra[3] == 255, "decoded_red_pixel_and_alpha");
        const auto encoded = plex::encode_artwork_bmp(decoded);
        const auto roundtrip = plex::decode_artwork(encoded);
        check(roundtrip.width == decoded.width && roundtrip.bgra == decoded.bgra, "provider_bitmap_roundtrip");
        auto invalid_pixels = decoded; invalid_pixels.bgra.pop_back();
        rejects([&] { plex::encode_artwork_bmp(invalid_pixels); }, "provider_bitmap_invalid_pixels_rejected");
        rejects([] { plex::decode_artwork({}); }, "empty_image_rejected");
        rejects([] { plex::decode_artwork({1, 2, 3}); }, "malformed_image_rejected");
        rejects([&] { plex::decode_artwork(bitmap, 1024); }, "oversized_output_rejected");
        auto enormous = bitmap; enormous[18] = 0xFF; enormous[19] = 0x7F;
        rejects([&] { plex::decode_artwork(enormous); }, "oversized_source_rejected");
        check(!plex::fetch_artwork({}, "", nullptr), "missing_artwork_without_request");
        const auto window = CreateWindowExW(0, L"STATIC", L"Artwork test", 0, 0, 0, 100, 100, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(window != nullptr, "artwork_message_target");
        {
            plex::plugin::ArtworkLoader loader(window);
            auto wait = [&] {
                const auto deadline = GetTickCount64() + 2000;
                do {
                    auto results = loader.take();
                    if (!results.empty()) return results.front();
                    MSG message{}; while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
                    Sleep(5);
                } while (GetTickCount64() < deadline);
                throw std::runtime_error("artwork_worker_timeout");
            };
            loader.reset(1);
            loader.submit(0, {"42", "Album without cover", "Artist", "", 2026, 8}, {}, "test");
            const auto missing = wait();
            check(missing.generation == 1 && !missing.error && !missing.pixels && missing.track_count == 8U, "missing_cover_retains_album_count");
            loader.reset(2);
            loader.submit(0, {"42", "Unsafe cover", "Artist", "https://foreign.example/image.jpg", 2026, 8}, {}, "test");
            const auto rejected = wait();
            check(rejected.generation == 2 && rejected.error == ERROR_INVALID_DATA && !rejected.pixels, "worker_does_not_hide_invalid_cover");
            loader.reset(3);
            loader.submit(1, {"43", "Album without cover", "Artist", "", 2026, 3}, {}, "test");
            const auto restored = wait();
            check(restored.generation == 3 && restored.row == 1 && !restored.error && restored.track_count == 3U, "worker_recovers_for_new_selection");
        }
        DestroyWindow(window);
        CoUninitialize();
        std::cout << checks << " artwork checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        CoUninitialize(); std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
