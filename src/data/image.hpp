#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>
namespace ef {
struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> rgba; // straight alpha, row zero at top
};
Image decode_image_scaled(const std::filesystem::path& path, int max_width, int max_height, int expected_width = 0, int expected_height = 0);
Image decode_image(const std::filesystem::path& path);
// Sequential RGBA rows. PNG uses one scanline (no complete panorama allocation).
// Callback may throw to cancel. Other formats retain the normal decode budget.
void stream_image_rows(const std::filesystem::path&, int width, int height,
                       const std::function<void(int, std::span<const std::uint8_t>)>&);
}
