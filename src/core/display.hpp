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
struct WindowSize { int width{}, height{}; };
struct MovieTextureSize { int width{}, height{}; };
// Initial 4:3 window tiers: the largest one that fits in 90% of the usable display.
[[nodiscard]] WindowSize adaptive_window_size(int usable_width, int usable_height);
// Startup precedence: explicit CLI size > remembered size when it still fits > adaptive.
[[nodiscard]] WindowSize resolve_window_size(int explicit_width, int explicit_height,
                                             int remembered_width, int remembered_height,
                                             int usable_width, int usable_height);
// Movie texture resolution follows the physical screen size of the logical movie rectangle.
// `max_side` caps either dimension (0 disables the cap); the design rectangle keeps its aspect.
[[nodiscard]] MovieTextureSize movie_texture_size(int design_width, int design_height,
                                                  int view_width, int view_height, int max_side);
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
