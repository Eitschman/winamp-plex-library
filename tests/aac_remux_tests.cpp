#include "plex/aac_remux.h"
#include "plex/http_stream.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
unsigned checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file.write(bytes.data(), bytes.size());
    check(static_cast<bool>(file), "write_synthetic_aac");
}
unsigned long long integer(const std::string& bytes, std::size_t offset, unsigned count) {
    unsigned long long result = 0;
    check(offset <= bytes.size() && count <= bytes.size() - offset, "bounded_mp4_box");
    for (unsigned index = 0; index < count; ++index) result = (result << 8) | static_cast<unsigned char>(bytes[offset + index]);
    return result;
}
std::size_t find_box(const std::string& bytes, std::size_t begin, std::size_t end, const char* name) {
    for (auto offset = begin; offset < end;) {
        auto size = integer(bytes, offset, 4); std::size_t header = 8;
        check(end - offset >= header, "mp4_box_header");
        if (size == 1) { size = integer(bytes, offset + 8, 8); header = 16; }
        else if (!size) size = end - offset;
        check(size >= header && size <= end - offset, "mp4_box_size");
        if (bytes.compare(offset + 4, 4, name) == 0) return offset;
        for (const auto* container : {"moov", "trak", "mdia", "minf", "stbl"}) {
            if (bytes.compare(offset + 4, 4, container) == 0) {
                const auto found = find_box(bytes, offset + header, offset + static_cast<std::size_t>(size), name);
                if (found != std::string::npos) return found;
            }
        }
        offset += static_cast<std::size_t>(size);
    }
    return std::string::npos;
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"plex-aac-tests-" + std::to_wstring(GetCurrentProcessId()));
    const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    try {
        check(stop != nullptr && std::filesystem::create_directory(root), "isolated_aac_test_directory");
        // Selbst erzeugter AAC-LC-Stilleframe mit 44,1 kHz und zwei Kanälen, kein fremdes Musikstück.
        constexpr std::array<unsigned char, 13> frame{0xff, 0xf1, 0x50, 0x80, 0x01, 0xbf, 0xfc, 0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c};
        std::string input, payload;
        for (unsigned index = 0; index < 64; ++index) {
            input.append(reinterpret_cast<const char*>(frame.data()), frame.size());
            payload.append(reinterpret_cast<const char*>(frame.data() + 7), frame.size() - 7);
        }
        const auto source = root / L"synthetic.aac", output = root / L"synthetic.m4a";
        write(source, input);
        bool created = false;
        const auto result = plex::remux_aac(source, output, stop, 1048576, &created);
        check(created && result.samples == 64 && result.bytes == std::filesystem::file_size(output), "aac_samples_and_size_preserved");
        const auto mp4 = read(output);
        // Der Container darf vor den Samples reservierte Bytes enthalten.
        // Maßgeblich sind die Sampletabellen und die dort referenzierten Audiobytes.
        const auto sizes = find_box(mp4, 0, mp4.size(), "stsz");
        const auto chunks = find_box(mp4, 0, mp4.size(), "stco");
        const auto mapping = find_box(mp4, 0, mp4.size(), "stsc");
        check(sizes != std::string::npos && chunks != std::string::npos && mapping != std::string::npos, "mp4_sample_tables");
        check(integer(mp4, chunks + 12, 4) == 1 && integer(mp4, mapping + 12, 4) == 1 &&
              integer(mp4, mapping + 16, 4) == 1 && integer(mp4, mapping + 20, 4) == 64 &&
              integer(mp4, mapping + 24, 4) == 1, "single_chunk_contains_all_fixture_samples");
        check(integer(mp4, sizes + 16, 4) == 64, "mp4_sample_count");
        const auto fixed_size = integer(mp4, sizes + 12, 4);
        auto sample_offset = static_cast<std::size_t>(integer(mp4, chunks + 16, 4));
        std::string actual_payload;
        for (unsigned index = 0; index < 64; ++index) {
            const auto size = fixed_size ? fixed_size : integer(mp4, sizes + 20 + index * 4, 4);
            check(size == frame.size() - 7 && sample_offset <= mp4.size() && size <= mp4.size() - sample_offset, "bounded_aac_sample");
            actual_payload.append(mp4, sample_offset, static_cast<std::size_t>(size));
            sample_offset += static_cast<std::size_t>(size);
        }
        check(actual_payload == payload, "aac_payload_bit_identical_without_encoding");
        bool rejected = false;
        try { plex::remux_aac(source, output, stop, 1048576, &created); }
        catch (const plex::HttpError&) { rejected = true; }
        check(rejected && !created && read(output) == mp4, "existing_output_never_overwritten");
        std::filesystem::remove(output);
        rejected = false;
        try { plex::remux_aac(source, output, stop, 1, &created); }
        catch (const plex::HttpError& error) { rejected = error.code == ERROR_FILE_TOO_LARGE; }
        check(rejected && !std::filesystem::exists(output), "oversized_remux_removed");
        SetEvent(stop); rejected = false;
        try { plex::remux_aac(source, output, stop, 1048576); }
        catch (const plex::HttpError& error) { rejected = error.code == ERROR_CANCELLED; }
        check(rejected && !std::filesystem::exists(output), "cancelled_remux_has_no_output");
        ResetEvent(stop);
        for (const auto& invalid : {std::string("invalid-aac-data"), input.substr(0, input.size() - 1), input + "unexpected-tail"}) {
            write(source, invalid); rejected = false;
            try { plex::remux_aac(source, output, stop, 1048576); }
            catch (const plex::HttpError&) { rejected = true; }
            check(rejected && !std::filesystem::exists(output), "malformed_or_truncated_aac_not_silently_accepted");
        }
        std::filesystem::remove(source); std::filesystem::remove(root);
        CloseHandle(stop);
        std::cout << checks << " AAC remux checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (stop) CloseHandle(stop);
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
