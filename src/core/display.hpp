#pragma once
#include <optional>

namespace ef {
inline constexpr int design_width = 800;
inline constexpr int design_height = 600;
enum class AssetTier { x4 = 4, x8 = 8 };
// Pixel density and monitor resolution deliberately do not enter this policy.
constexpr AssetTier asset_tier(bool maximized, bool fullscreen) {
    return maximized || fullscreen ? AssetTier::x8 : AssetTier::x4;
}
struct Point { float x{}, y{}; };
struct Viewport {
    int x{}, y{}, width{}, height{}; // top-left origin, physical pixels
    [[nodiscard]] float scale() const;
    [[nodiscard]] std::optional<Point> to_design(float pixel_x, float pixel_y) const;
};
Viewport fit_viewport(int pixel_width, int pixel_height);
class FixedStep {
public:
    static constexpr double interval = 1.0 / 60.0;
    int advance(double elapsed);
private:
    double remainder_{};
};
}
