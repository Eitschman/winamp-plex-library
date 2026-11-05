#include "plex/aac_remux.h"
#include "plex/http_stream.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <wmcodecdsp.h>
#include <array>
#include <cstring>
#include <fstream>

namespace plex {
namespace {
using Microsoft::WRL::ComPtr;
struct RemuxFailure : HttpError {
    RemuxFailure(HRESULT result, const char* location) : HttpError(static_cast<DWORD>(result)), stage(location) {}
    const char* what() const noexcept override { return stage; }
    const char* stage;
};
void require(HRESULT result, const char* stage = "media_foundation") { if (FAILED(result)) throw RemuxFailure(result, stage); }
constexpr DWORD audio_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr DWORD all_streams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
void cancelled(HANDLE stop) {
    const auto result = WaitForSingleObject(stop, 0);
    if (result == WAIT_OBJECT_0) throw HttpError(ERROR_CANCELLED);
    if (result != WAIT_TIMEOUT) throw HttpError(ERROR_INVALID_HANDLE);
}
struct Module {
    explicit Module(const wchar_t* name) : value(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!value) throw HttpError(GetLastError());
    }
    ~Module() { FreeLibrary(value); }
    HMODULE value;
};
template<class Function> Function entry(const Module& module, const char* name) {
    const auto address = GetProcAddress(module.value, name);
    if (!address) throw HttpError(ERROR_PROC_NOT_FOUND);
    return reinterpret_cast<Function>(address);
}
struct Platform {
    // Optionale Windows-Medienkomponenten erst für AAC laden, ohne DLL-Suche im Arbeitsverzeichnis.
    Module core{L"mfplat.dll"}, readwrite{L"mfreadwrite.dll"};
    decltype(&MFStartup) startup = entry<decltype(&MFStartup)>(core, "MFStartup");
    decltype(&MFShutdown) shutdown = entry<decltype(&MFShutdown)>(core, "MFShutdown");
    decltype(&MFCreateAttributes) create_attributes = entry<decltype(&MFCreateAttributes)>(core, "MFCreateAttributes");
    decltype(&MFCreateMediaType) create_media_type = entry<decltype(&MFCreateMediaType)>(core, "MFCreateMediaType");
    decltype(&MFCreateFile) create_file = entry<decltype(&MFCreateFile)>(core, "MFCreateFile");
    decltype(&MFCreateMemoryBuffer) create_buffer = entry<decltype(&MFCreateMemoryBuffer)>(core, "MFCreateMemoryBuffer");
    decltype(&MFCreateSample) create_sample = entry<decltype(&MFCreateSample)>(core, "MFCreateSample");
    decltype(&MFCreateSourceReaderFromURL) create_reader = entry<decltype(&MFCreateSourceReaderFromURL)>(readwrite, "MFCreateSourceReaderFromURL");
    decltype(&MFCreateSinkWriterFromURL) create_writer = entry<decltype(&MFCreateSinkWriterFromURL)>(readwrite, "MFCreateSinkWriterFromURL");
    Platform() { require(startup(MF_VERSION, MFSTARTUP_LITE)); }
    ~Platform() { shutdown(); }
};
struct Apartment {
    Apartment() { require(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "com_initialize"); }
    ~Apartment() { CoUninitialize(); }
};

// Metadaten werden nicht in Audio umgedeutet. Die Bytebilanz erkennt auch vom
// Windows-Parser übersprungene oder abgeschnittene ADTS-Frames.
unsigned long long audio_bytes(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 10> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) throw HttpError(ERROR_BAD_FORMAT);
    unsigned long long metadata = 0;
    if (!std::memcmp(header.data(), "ID3", 3)) {
        if (header[3] < 2 || header[3] > 4) throw HttpError(ERROR_BAD_FORMAT);
        unsigned tag = 0;
        for (std::size_t index = 6; index < 10; ++index) {
            if (header[index] & 128) throw HttpError(ERROR_BAD_FORMAT);
            tag = (tag << 7) | header[index];
        }
        metadata = 10ULL + tag + (header[3] == 4 && (header[5] & 16) ? 10 : 0);
        if (metadata > size || metadata > 16 * 1024 * 1024) throw HttpError(ERROR_BAD_FORMAT);
    }
    if (size >= 128) {
        file.seekg(static_cast<std::streamoff>(size - 128));
        std::array<char, 3> tail{};
        if (!file.read(tail.data(), tail.size())) throw HttpError(ERROR_READ_FAULT);
        if (!std::memcmp(tail.data(), "TAG", 3)) metadata += 128;
    }
    if (metadata >= size) throw HttpError(ERROR_BAD_FORMAT);
    return size - metadata;
}
}
AacRemuxInfo remux_aac(const std::filesystem::path& input, const std::filesystem::path& output,
                       HANDLE stop, unsigned long long maximum_bytes, bool* output_created) {
    if (output_created) *output_created = false;
    cancelled(stop);
    const auto expected_audio_bytes = audio_bytes(input);
    Apartment apartment;
    Platform platform;
    ComPtr<IMFAttributes> attributes;
    require(platform.create_attributes(&attributes, 1));
    require(attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE));
    ComPtr<IMFSourceReader> reader;
    require(platform.create_reader(input.c_str(), attributes.Get(), &reader), "create_reader");
    require(reader->SetStreamSelection(all_streams, FALSE));
    require(reader->SetStreamSelection(audio_stream, TRUE));
    ComPtr<IMFMediaType> type;
    require(reader->GetNativeMediaType(audio_stream, 0, &type), "native_type");
    GUID major{}, subtype{};
    require(type->GetGUID(MF_MT_MAJOR_TYPE, &major)); require(type->GetGUID(MF_MT_SUBTYPE, &subtype));
    if (major != MFMediaType_Audio || subtype != MFAudioFormat_AAC) throw HttpError(ERROR_BAD_FORMAT);
    require(reader->SetCurrentMediaType(audio_stream, nullptr, type.Get()), "set_compressed_type");
    AacRemuxInfo result;
    type->GetUINT32(MF_MT_AAC_PAYLOAD_TYPE, &result.payload_type);
    ComPtr<IMFSample> first_sample;
    DWORD first_flags = 0;
    require(reader->ReadSample(audio_stream, 0, nullptr, &first_flags, nullptr, &first_sample), "first_adts_sample");
    if (!first_sample || result.payload_type != 1) throw HttpError(ERROR_NOT_SUPPORTED);
    ComPtr<IMFMediaBuffer> first_buffer;
    require(first_sample->ConvertToContiguousBuffer(&first_buffer));
    BYTE* first_data = nullptr; DWORD first_size = 0;
    require(first_buffer->Lock(&first_data, nullptr, &first_size));
    std::array<BYTE, 7> initial{};
    if (first_size >= initial.size()) std::memcpy(initial.data(), first_data, initial.size());
    require(first_buffer->Unlock());
    const auto profile = static_cast<unsigned>(initial[2] >> 6) + 1;
    const auto frequency = static_cast<unsigned>((initial[2] >> 2) & 15);
    const auto channels = ((initial[2] & 1) << 2) | (initial[3] >> 6);
    constexpr std::array<unsigned, 13> rates{96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    if (first_size < initial.size() || initial[0] != 255 || (initial[1] & 0xF6) != 0xF0 || frequency >= rates.size() || !channels)
        throw HttpError(ERROR_BAD_FORMAT);
    ComPtr<IMFMediaType> output_type;
    require(platform.create_media_type(&output_type));
    require(type->CopyAllItems(output_type.Get()));
    require(output_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0));
    require(output_type->SetGUID(MF_MT_SUBTYPE, MEDIASUBTYPE_RAW_AAC1));
    std::array<BYTE, 2> extra{};
    const auto asc = static_cast<unsigned>((profile << 11) | (frequency << 7) | (channels << 3));
    extra[extra.size() - 2] = static_cast<BYTE>(asc >> 8); extra.back() = static_cast<BYTE>(asc);
    require(output_type->SetBlob(MF_MT_USER_DATA, extra.data(), static_cast<UINT32>(extra.size())));
    ComPtr<IMFByteStream> bytes;
    require(platform.create_file(MF_ACCESSMODE_WRITE, MF_OPENMODE_FAIL_IF_EXIST, MF_FILEFLAGS_NONE, output.c_str(), &bytes), "create_output_file");
    if (output_created) *output_created = true;
    try {
        ComPtr<IMFSinkWriter> writer;
        require(platform.create_writer(output.c_str(), bytes.Get(), attributes.Get(), &writer), "create_writer");
        DWORD stream = 0;
        require(writer->AddStream(output_type.Get(), &stream), "add_output_stream");
        require(writer->SetInputMediaType(stream, output_type.Get(), nullptr), "set_compressed_input");
        require(writer->BeginWriting(), "begin_writing");
        unsigned long long decoded_samples = 0;
        unsigned long long consumed_bytes = 0;
        for (;;) {
            cancelled(stop);
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (first_sample) { sample = std::move(first_sample); flags = first_flags; }
            else require(reader->ReadSample(audio_stream, 0, nullptr, &flags, nullptr, &sample), "read_compressed_sample");
            if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))
                throw HttpError(ERROR_BAD_FORMAT);
            if (sample) {
                ComPtr<IMFMediaBuffer> source;
                require(sample->ConvertToContiguousBuffer(&source));
                BYTE* data = nullptr; DWORD length = 0;
                require(source->Lock(&data, nullptr, &length));
                try {
                    consumed_bytes += length;
                    DWORD offset = 0;
                    while (offset < length) {
                        cancelled(stop);
                        if (length - offset < 7) throw HttpError(ERROR_BAD_FORMAT);
                        const auto* frame = data + offset;
                        const auto header = (frame[1] & 1) ? 7U : 9U;
                        const auto size = static_cast<DWORD>(((frame[3] & 3) << 11) | (frame[4] << 3) | (frame[5] >> 5));
                        if (frame[0] != 255 || (frame[1] & 0xF6) != 0xF0 || (frame[6] & 3) ||
                            (frame[2] & 0xFD) != (initial[2] & 0xFD) || (frame[3] & 0xC0) != (initial[3] & 0xC0) ||
                            size <= header || size > length - offset) throw HttpError(ERROR_BAD_FORMAT);
                        if (++result.samples > 2000000) throw HttpError(ERROR_FILE_TOO_LARGE);
                        ComPtr<IMFMediaBuffer> payload;
                        require(platform.create_buffer(size - header, &payload));
                        BYTE* destination = nullptr;
                        require(payload->Lock(&destination, nullptr, nullptr));
                        std::memcpy(destination, frame + header, size - header);
                        require(payload->Unlock()); require(payload->SetCurrentLength(size - header));
                        ComPtr<IMFSample> packet;
                        require(platform.create_sample(&packet)); require(packet->AddBuffer(payload.Get()));
                        const auto start = decoded_samples * 10000000ULL / rates[frequency];
                        decoded_samples += 1024;
                        const auto end = decoded_samples * 10000000ULL / rates[frequency];
                        require(packet->SetSampleTime(static_cast<LONGLONG>(start)));
                        require(packet->SetSampleDuration(static_cast<LONGLONG>(end - start)));
                        require(writer->WriteSample(stream, packet.Get()), "write_compressed_sample");
                        offset += size;
                    }
                } catch (...) { source->Unlock(); throw; }
                require(source->Unlock());
                require(bytes->GetLength(&result.bytes));
                if (result.bytes > maximum_bytes) throw HttpError(ERROR_FILE_TOO_LARGE);
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        if (!result.samples || consumed_bytes != expected_audio_bytes) throw HttpError(ERROR_BAD_FORMAT);
        require(writer->Finalize(), "finalize_container");
        writer.Reset(); bytes.Reset();
        result.bytes = std::filesystem::file_size(output);
        if (!result.bytes || result.bytes > maximum_bytes) throw HttpError(ERROR_FILE_TOO_LARGE);
        return result;
    } catch (...) {
        bytes.Reset(); DeleteFileW(output.c_str()); throw;
    }
}
}
