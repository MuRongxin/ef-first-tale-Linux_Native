#include "core/display.hpp"
#include <algorithm>
#include <cmath>

namespace ef {
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
