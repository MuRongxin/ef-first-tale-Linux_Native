#include "data/image.hpp"
#include <png.h>
#include <webp/decode.h>
#include <array>
#include <algorithm>
#include <cmath>
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
Image decode_image_scaled(const std::filesystem::path& path, int max_width, int max_height, int expected_width, int expected_height) {
    if (max_width<=0 || max_height<=0) throw std::runtime_error("Invalid decode bounds");
    auto check_size=[&](int w,int h) {
        if ((expected_width && expected_width!=w) || (expected_height && expected_height!=h))
            throw std::runtime_error("Image dimensions differ from content declaration");
    };
    auto dimensions=[&](int w,int h) {
        const double scale=std::min({1.0,static_cast<double>(max_width)/w,static_cast<double>(max_height)/h});
        return std::pair{std::max(1,static_cast<int>(w*scale)),std::max(1,static_cast<int>(h*scale))};
    };
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
            check_size(result.width,result.height);
            result.rgba.resize(pixel_bytes(result.width, result.height));
            png.value.format = PNG_FORMAT_RGBA;
            if (!png_image_finish_read(&png.value, nullptr, result.rgba.data(), 0, nullptr))
                throw std::runtime_error(png.value.message);
        } else {
            WebPBitstreamFeatures features{};
            if (WebPGetFeatures(encoded.data(), encoded.size(), &features) != VP8_STATUS_OK)
                throw std::runtime_error("Not a valid PNG or WebP image");
            if (features.has_animation) throw std::runtime_error("Animated WebP is not supported; import frame sequences");
            check_size(features.width,features.height);
            const auto [w,h]=dimensions(features.width,features.height);
            result.width = w;
            result.height = h;
            result.rgba.resize(pixel_bytes(result.width, result.height));
            WebPDecoderConfig config{};
            if (!WebPInitDecoderConfig(&config)) throw std::runtime_error("WebP decoder init failed");
            config.output.colorspace=MODE_RGBA;config.output.is_external_memory=1;
            config.output.u.RGBA.rgba=result.rgba.data();config.output.u.RGBA.stride=result.width*4;
            config.output.u.RGBA.size=result.rgba.size();
            config.options.use_scaling=(result.width!=features.width || result.height!=features.height);config.options.scaled_width=result.width;config.options.scaled_height=result.height;
            const auto status=WebPDecode(encoded.data(),encoded.size(),&config);
            WebPFreeDecBuffer(&config.output);
            if (status!=VP8_STATUS_OK) throw std::runtime_error("WebP decode failed");
        }
        const auto [w,h]=dimensions(result.width,result.height);
        if (w!=result.width || h!=result.height) {
            Image small{w,h,std::vector<std::uint8_t>(pixel_bytes(w,h))};
            // Area average for PNG fallbacks; WebP performs the reduction inside its decoder.
            for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
                const int x0=static_cast<int>(static_cast<std::int64_t>(x)*result.width/w),x1=std::max(x0+1,static_cast<int>(static_cast<std::int64_t>(x+1)*result.width/w));
                const int y0=static_cast<int>(static_cast<std::int64_t>(y)*result.height/h),y1=std::max(y0+1,static_cast<int>(static_cast<std::int64_t>(y+1)*result.height/h));
                std::array<std::uint64_t,4> sum{};
                for (int sy=y0;sy<y1;++sy) for (int sx=x0;sx<x1;++sx)
                    for (int c=0;c<4;++c) sum[static_cast<std::size_t>(c)]+=result.rgba[(static_cast<std::size_t>(sy)*result.width+sx)*4+c];
                const auto n=static_cast<unsigned>((x1-x0)*(y1-y0));
                for (int c=0;c<4;++c) small.rgba[(static_cast<std::size_t>(y)*w+x)*4+c]=static_cast<std::uint8_t>(sum[static_cast<std::size_t>(c)]/n);
            }
            return small;
        }
        return result;
    } catch (const std::exception& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}
Image decode_image(const std::filesystem::path& path) {
    return decode_image_scaled(path,65536,65536);
}
}
