#include "scene/walking.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
namespace ef {
Walking::Walking(int period,int speed) {
    if (period!=60 || speed!=-2) throw std::runtime_error("Only the corpus WScroll2 period 60 and speed -2 are implemented");
}
void Walking::update(double seconds) {
    if (!std::isfinite(seconds) || seconds<0 || seconds>1e9) throw std::runtime_error("Invalid walking delta time");
    elapsed_=std::min(1e9,elapsed_+seconds);
}
std::vector<Layer> Walking::layers(const std::vector<Layer>& base) const {
    const auto samples=static_cast<std::int64_t>(std::floor(elapsed_*10+1e-8));
    bool checked_foreground=false;
    for (const auto& layer : base) {
        const bool fg=layer.part==StagePart::foreground && !checked_foreground;
        checked_foreground|=fg;
        if ((layer.part==StagePart::background || fg) && (layer.width<800 || layer.height<600 || layer.source))
            throw std::runtime_error("Unsupported walking source canvas");
    }
    if (!samples) return base;
    const auto near=-2*samples;
    // The native remainder advances only when abs(remainder)>5, not >=5.
    const auto far=near/5+(near%5==0 ? 1 : 0);
    std::vector<Layer> result;
    bool foreground=false;
    for (auto layer : base) {
        if (layer.part==StagePart::background || (layer.part==StagePart::foreground && !foreground)) {
            const bool fg=layer.part==StagePart::foreground;
            foreground|=fg;
            if (layer.width<800 || layer.source) throw std::runtime_error("Unsupported walking source canvas");
            const double position=fg ? -static_cast<double>(layer.x)+static_cast<double>(near) : -static_cast<double>(layer.x)+static_cast<double>(far);
            double crop=std::fmod(position,static_cast<double>(layer.width));
            if (crop<0) crop+=layer.width;
            layer.x=-static_cast<float>(crop); layer.y=0;
            result.push_back(layer);
            if (layer.x+layer.width<800) { layer.x+=layer.width; result.push_back(std::move(layer)); }
        } else {
            if (layer.part==StagePart::frame) {
                const float extra=std::max(0.0f,layer.height-600);
                const double angle=-2*std::numbers::pi*static_cast<double>(samples%60)/60;
                layer.x=0;
                layer.y=-(extra-static_cast<float>(std::abs(std::trunc(extra*std::sin(angle)))));
            }
            result.push_back(std::move(layer));
        }
    }
    return result;
}
}
