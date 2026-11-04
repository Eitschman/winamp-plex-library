#include "plex/artwork.h"
#include "plex/http_stream.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace plex {
namespace {
constexpr std::size_t byte_limit = 8 * 1024 * 1024;
void require(HRESULT result) { if (FAILED(result)) throw std::invalid_argument("artwork_decode"); }
bool digits(std::string_view value) {
    return !value.empty() && value.size() <= 32 && value.find_first_not_of("0123456789") == value.npos;
}
}

void validate_artwork_path(const std::string& path) {
    constexpr std::string_view prefix = "/library/metadata/";
    if (path.size() > 160 || path.compare(0, prefix.size(), prefix) != 0) throw std::invalid_argument("artwork_path");
    const auto remaining = std::string_view(path).substr(prefix.size());
    const auto marker = remaining.find("/thumb");
    if (marker == remaining.npos || !digits(remaining.substr(0, marker))) throw std::invalid_argument("artwork_path");
    const auto version = remaining.substr(marker + 6);
    if (!version.empty() && (version.front() != '/' || !digits(version.substr(1)))) throw std::invalid_argument("artwork_path");
}

ArtworkPixels decode_artwork(const std::vector<std::uint8_t>& bytes, unsigned edge) {
    if (bytes.empty() || bytes.size() > byte_limit || edge < 16 || edge > 256) throw std::invalid_argument("artwork_limit");
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    require(factory->CreateStream(&stream));
    require(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    require(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    require(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0;
    require(frame->GetSize(&width, &height));
    if (!width || !height || width > 16384 || height > 16384 ||
        static_cast<unsigned long long>(width) * height > 32000000) throw std::invalid_argument("artwork_dimensions");
    const auto scale = static_cast<double>(edge) / (std::max)(width, height);
    ArtworkPixels result;
    result.width = (std::max)(1U, static_cast<unsigned>(width * scale));
    result.height = (std::max)(1U, static_cast<unsigned>(height * scale));
    ComPtr<IWICBitmapScaler> scaler;
    require(factory->CreateBitmapScaler(&scaler));
    require(scaler->Initialize(frame.Get(), result.width, result.height, WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> converter;
    require(factory->CreateFormatConverter(&converter));
    require(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    result.bgra.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    require(converter->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.bgra.size()), result.bgra.data()));
    return result;
}

std::vector<std::uint8_t> encode_artwork_bmp(const ArtworkPixels& pixels) {
    if (!pixels.width || !pixels.height || pixels.width > 256 || pixels.height > 256 ||
        pixels.bgra.size() != static_cast<std::size_t>(pixels.width) * pixels.height * 4)
        throw std::invalid_argument("artwork_pixels");
    BITMAPFILEHEADER file{};
    BITMAPV5HEADER header{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(header);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.bgra.size());
    header.bV5Size = sizeof(header); header.bV5Width = pixels.width;
    header.bV5Height = -static_cast<LONG>(pixels.height);
    header.bV5Planes = 1; header.bV5BitCount = 32; header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    std::vector<std::uint8_t> result(file.bfSize);
    std::memcpy(result.data(), &file, sizeof(file));
    std::memcpy(result.data() + sizeof(file), &header, sizeof(header));
    std::memcpy(result.data() + file.bfOffBits, pixels.bgra.data(), pixels.bgra.size());
    // WIC liefert vormultiplizierte Pixel; das BMP speichert unabhängige Farbkanäle.
    for (std::size_t index = file.bfOffBits; index < result.size(); index += 4) {
        const auto alpha = result[index + 3];
        for (std::size_t channel = 0; channel < 3; ++channel)
            result[index + channel] = alpha ? static_cast<std::uint8_t>((std::min)(255U, (result[index + channel] * 255U + alpha / 2) / alpha)) : 0;
    }
    return result;
}

std::optional<ArtworkPixels> fetch_artwork(const PlexConnectionSettings& settings, const std::string& thumb, HANDLE stop, unsigned edge) {
    if (thumb.empty()) return std::nullopt;
    validate_artwork_path(thumb);
    HttpStream stream(parse_server_address(settings.server_origin), settings.server_token, thumb,
                      HttpMethod::get, settings.client_identifier, stop, HttpRepresentation::image);
    const auto status = stream.status();
    if (status == 404) return std::nullopt;
    if (status != 200) throw HttpError(0x20000000UL + status);
    const auto type = stream.header(L"Content-Type");
    if (type.compare(0, 6, "image/") != 0) throw std::invalid_argument("artwork_content_type");
    std::array<char, 32768> buffer{};
    std::vector<std::uint8_t> bytes;
    for (;;) {
        const auto count = stream.read(buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!count) break;
        if (bytes.size() + count > byte_limit) throw std::invalid_argument("artwork_limit");
        bytes.insert(bytes.end(), buffer.data(), buffer.data() + count);
    }
    return decode_artwork(bytes, edge);
}
}
