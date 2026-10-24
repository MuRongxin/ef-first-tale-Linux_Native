#include "data/image.hpp"
#include <png.h>
#include <webp/decode.h>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ef {
namespace {
std::size_t pixel_bytes(int width, int height) {
    constexpr std::uint64_t max_bytes = 512ULL * 1024 * 1024;
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 4 > max_bytes)
        throw std::runtime_error("Image dimensions exceed the 512 MiB decode budget");
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
}
}
Image decode_image(const std::filesystem::path& path) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open image");
        const auto length = input.tellg();
        if (length <= 0 || length > 512 * 1024 * 1024) throw std::runtime_error("Invalid image file size");
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(encoded.data()), length)) throw std::runtime_error("Cannot read image");
        Image result;
        if (encoded.size() >= 8 && png_sig_cmp(encoded.data(), 0, 8) == 0) {
            struct PngGuard {
                png_image value{};
                PngGuard() { value.version = PNG_IMAGE_VERSION; }
                ~PngGuard() { png_image_free(&value); }
            } png;
            if (!png_image_begin_read_from_memory(&png.value, encoded.data(), encoded.size()))
                throw std::runtime_error(png.value.message);
            if (png.value.width > static_cast<unsigned>(std::numeric_limits<int>::max()) ||
                png.value.height > static_cast<unsigned>(std::numeric_limits<int>::max()))
                throw std::runtime_error("PNG dimensions overflow");
            result.width = static_cast<int>(png.value.width);
            result.height = static_cast<int>(png.value.height);
            result.rgba.resize(pixel_bytes(result.width, result.height));
            png.value.format = PNG_FORMAT_RGBA;
            if (!png_image_finish_read(&png.value, nullptr, result.rgba.data(), 0, nullptr))
                throw std::runtime_error(png.value.message);
        } else {
            WebPBitstreamFeatures features{};
            if (WebPGetFeatures(encoded.data(), encoded.size(), &features) != VP8_STATUS_OK)
                throw std::runtime_error("Not a valid PNG or WebP image");
            if (features.has_animation) throw std::runtime_error("Animated WebP is not supported; import frame sequences");
            result.width = features.width;
            result.height = features.height;
            result.rgba.resize(pixel_bytes(result.width, result.height));
            if (!WebPDecodeRGBAInto(encoded.data(), encoded.size(), result.rgba.data(), result.rgba.size(), result.width * 4))
                throw std::runtime_error("WebP decode failed");
        }
        return result;
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}
}
