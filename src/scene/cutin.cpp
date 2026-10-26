#include "scene/cutin.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace ef {
Cutin::Cutin(Layer image,int hold_ticks,int acceleration)
    : image_(std::move(image)),hold_(hold_ticks),rightward_(acceleration>=0) {
    if (image_.width!=800 || image_.height!=600 || hold_ticks < -1000000 || hold_ticks>1000000 || acceleration < -10000 || acceleration>10000)
        throw std::runtime_error("Unsupported Cutin canvas or timing range");
    factor_=std::abs(static_cast<double>(acceleration)/100);
    if (factor_<=1) factor_=1.5;
    velocity_=rightward_ ? 2 : -2;
    x_=rightward_ ? -800 : 800;
}
void Cutin::step() {
    if (state_==1) {
        if (!exiting_) {
            if (hold_<0) return;
            hold_=std::max(0,hold_-4);
            if (hold_>0) return;
        }
        state_=2; x_=0; velocity_=rightward_ ? 2 : -2;
    }
    if (state_==0 || state_==2) {
        x_+=static_cast<int>(std::trunc(velocity_));
        velocity_*=factor_;
        if (state_==0 && (rightward_ ? x_>=0 : x_<=0)) { x_=0; state_=1; }
        else if (state_==2 && (rightward_ ? x_>=800 : x_<=-800)) { x_=rightward_ ? 800 : -800; state_=3; }
    }
}
void Cutin::update(double seconds) {
    if (!std::isfinite(seconds) || seconds<0 || seconds>1e9) throw std::runtime_error("Invalid Cutin delta time");
    elapsed_=std::min(1e9,elapsed_+seconds);
    const auto now=static_cast<std::uint64_t>(std::floor(elapsed_*100+1e-9));
    while (!finished() && next_tick_<=now) {
        if (state_==1 && !exiting_) {
            auto skip=(now-next_tick_)/4+1;
            if (hold_>=0) skip=std::min(skip,static_cast<std::uint64_t>(std::max(0,hold_-1)/4));
            if (skip) {
                next_tick_+=skip*4;
                if (hold_>=0) hold_-=static_cast<int>(skip*4);
                continue;
            }
        }
        step(); next_tick_+=4;
    }
}
Layer Cutin::layer() const {
    auto result=image_; result.x=static_cast<float>(x_); result.y=0; return result;
}
}
