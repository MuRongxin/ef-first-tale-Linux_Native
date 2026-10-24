#pragma once
#include "data/content.hpp"
#include <array>
#include <string>
#include <vector>
namespace ef {
using Color = std::array<float, 4>;
enum class LayerKind { image, rectangle, text };
struct Layer {
    LayerKind kind{};
    std::string asset, text, slot;
    float x{}, y{}, width{}, height{}, font_size{24};
    Color color{1, 1, 1, 1};
};
struct Scene {
    std::string title;
    Color background{0.06f, 0.08f, 0.12f, 1};
    std::vector<Layer> layers; // painter's order; all coordinates in 800x600 space
    static Scene load(const Content& content, const std::string& path = {});
};
}
