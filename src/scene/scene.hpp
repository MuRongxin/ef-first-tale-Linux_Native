#pragma once
#include "data/content.hpp"
#include "text/appearance.hpp"
#include <array>
#include <string>
#include <optional>
#include <limits>
#include <vector>
namespace ef {
using Color = std::array<float, 4>;
enum class LayerKind { image, rectangle, text };
struct TextMarker { std::string asset; int frame_size{},frame{}; };
struct Layer {
    LayerKind kind{};
    std::string asset, text, slot;
    float x{}, y{}, width{}, height{}, font_size{24};
    Color color{1, 1, 1, 1};
    TextAppearance appearance;
    std::optional<std::array<float,4>> source; // image atlas region in design pixels
    float line_height{}, letter_spacing{}, upward_room{};
    bool kinsoku{}, flow_body{};
    std::optional<std::size_t> flow_anchor; // UTF-8 byte offset of a speaker in a full-screen page
    std::optional<TextMarker> advance_marker;
    std::size_t visible_characters{std::numeric_limits<std::size_t>::max()};
};
struct Scene {
    std::string title;
    Color background{0.06f, 0.08f, 0.12f, 1};
    std::vector<Layer> layers; // painter's order; all coordinates in 800x600 space
    static Scene load(const Content& content, const std::string& path = {});
};
}
