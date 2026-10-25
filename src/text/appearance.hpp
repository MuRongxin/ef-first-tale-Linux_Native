#pragma once
#include <array>
#include <nlohmann/json_fwd.hpp>
namespace ef {
// Design-pixel values; they never participate in line breaking or cursor advance.
struct TextAppearance {
    float outline_width{},shadow_x{},shadow_y{},baseline_offset{};
    std::array<float,4> outline_color{0,0,0,1},shadow_color{0,0,0,0.65f};
    bool operator==(const TextAppearance&) const = default;
    void validate() const;
};
void to_json(nlohmann::json& json,const TextAppearance& value);
void from_json(const nlohmann::json& json,TextAppearance& value);
}
