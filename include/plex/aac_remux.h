#pragma once
#include <windows.h>
#include <filesystem>

namespace plex {
struct AacRemuxInfo { unsigned long long bytes = 0; unsigned samples = 0; unsigned payload_type = 0; };
// Ausschließlich komprimierte AAC-Samples umverpacken, ohne Decoder oder Encoder.
AacRemuxInfo remux_aac(const std::filesystem::path& input, const std::filesystem::path& output,
                       HANDLE stop, unsigned long long maximum_bytes, bool* output_created = nullptr);
}
