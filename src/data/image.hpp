#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace ef {
struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> rgba; // straight alpha, row zero at top
};
Image decode_image(const std::filesystem::path& path);
}
