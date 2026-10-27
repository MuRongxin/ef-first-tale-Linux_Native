#include "core/display.hpp"
#include <algorithm>
#include <cmath>

namespace ef {
WindowSize adaptive_window_size(int usable_width, int usable_height) {
    if (usable_width <= 0 || usable_height <= 0) return {800, 600};
    const int available_width = usable_width * 9 / 10;
    const int available_height = usable_height * 9 / 10;
    static constexpr WindowSize tiers[]{
        {800,600},{1200,900},{1600,1200},{2000,1500},{2400,1800},{3200,2400}};
    constexpr std::size_t tier_count = sizeof(tiers) / sizeof(tiers[0]);
    for (std::size_t n = tier_count; n-- > 0;)
        if (tiers[n].width <= available_width && tiers[n].height <= available_height) return tiers[n];
    // Very small displays: keep 4:3 and the application minimum (320x240).
    const int unit = std::max(80, std::min(usable_width / 4, usable_height / 3));
    return {unit * 4, unit * 3};
}
MovieTextureSize movie_texture_size(int design_width, int design_height,
                                    int view_width, int view_height, int max_side) {
    if (design_width <= 0 || design_height <= 0 || view_width <= 0 || view_height <= 0) return {};
    const double scale = std::min(static_cast<double>(view_width) / 800.0,
                                  static_cast<double>(view_height) / 600.0);
    long long width = std::max(1LL, std::llround(design_width * scale));
    long long height = std::max(1LL, std::llround(design_height * scale));
    if (max_side > 0 && (width > max_side || height > max_side)) {
        const double factor = static_cast<double>(max_side) / static_cast<double>(std::max(width, height));
        width = std::max(1LL, std::llround(width * factor));
        height = std::max(1LL, std::llround(height * factor));
    }
    return {static_cast<int>(width), static_cast<int>(height)};
}
WindowSize resolve_window_size(int explicit_width, int explicit_height,
                               int remembered_width, int remembered_height,
                               int usable_width, int usable_height) {
    if (explicit_width > 0 && explicit_height > 0) return {explicit_width, explicit_height};
    const bool remembered = remembered_width >= 320 && remembered_height >= 240 &&
        remembered_width <= usable_width && remembered_height <= usable_height;
    if (remembered) return {remembered_width, remembered_height};
    return adaptive_window_size(usable_width, usable_height);
}
float Viewport::scale() const { return static_cast<float>(width) / design_width; }
std::optional<Point> Viewport::to_design(float px, float py) const {
    const float local_x = px - static_cast<float>(x);
    const float local_y = py - static_cast<float>(y);
    if (width <= 0 || height <= 0 || local_x < 0 || local_y < 0 ||
        local_x >= static_cast<float>(width) || local_y >= static_cast<float>(height)) return {};
    return Point{local_x * design_width / static_cast<float>(width),
                 local_y * design_height / static_cast<float>(height)};
}
Viewport fit_viewport(int w, int h) {
    if (w <= 0 || h <= 0) return {};
    // Multiples of 4:3 keep x/y scale equal, including odd framebuffer sizes.
    const int unit = std::min(w / 4, h / 3);
    return {(w - unit * 4) / 2, (h - unit * 3) / 2, unit * 4, unit * 3};
}
int FixedStep::advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed <= 0) return 0;
    remainder_ += std::min(elapsed, 0.25); // cap catch-up after a blocked load/drag
    const int steps = static_cast<int>((remainder_ + 1e-12) / interval);
    remainder_ = std::max(0.0, remainder_ - steps * interval);
    return steps;
}
}
