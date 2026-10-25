#include "scene/snow.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ef {
Snow::Snow(std::array<Layer,3> sprites, bool horizontal, std::uint32_t seed)
    : sprites_(std::move(sprites)), seed_(seed), horizontal_(horizontal) {
    for (auto& particle : particles_) spawn(particle,false);
}
std::uint32_t Snow::random() {
    seed_ = seed_*214013U+2531011U;
    return (seed_ >> 16)&32767;
}
void Snow::spawn(Particle& p, bool entering) {
    p.x = horizontal_ && entering ? 0 : static_cast<double>(random()%900);
    p.y = !horizontal_ && entering ? 0 : static_cast<double>(random()%660);
    const double slow = static_cast<double>(random()%8192)/65536;
    const bool positive = random()>16383;
    const auto fast_raw = random()*2+16384;
    const double fast = static_cast<double>(fast_raw)/65536;
    p.vx = horizontal_ ? fast : positive ? slow : -slow;
    p.vy = horizontal_ ? positive ? slow : -slow : fast;
    p.sprite = fast_raw < 20480 ? 0 : fast_raw < 40960 ? 1 : 2;
    p.active = true;
}
void Snow::fade_out() { if (!fading_) { fading_ = true; fade_ticks_ = 0; } }
void Snow::update(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9) throw std::runtime_error("Invalid snow delta time");
    if (finished_) return;
    time_ += seconds;
    const auto now = static_cast<std::uint64_t>(std::floor(time_*100));
    if (now == ticks_) return;
    const auto delta = now-ticks_;
    ticks_ = now;
    fade_ticks_ += static_cast<double>(delta);
    const int step = static_cast<int>(std::min(256.0,std::floor(fade_ticks_/16)));
    opacity_ = std::clamp(opacity_+(fading_ ? -step : step),0,256);
    if (fading_ && opacity_ == 0) { finished_ = true; return; }
    for (auto& p : particles_) {
        if (!p.active) { spawn(p,true); continue; }
        p.x += p.vx*static_cast<double>(delta);
        p.y += p.vy*static_cast<double>(delta);
        if ((horizontal_ ? p.x-50 >= 800 : p.y-30 >= 600)) p.active = false;
    }
}
std::vector<Layer> Snow::layers() const {
    std::vector<Layer> result;
    if (finished_ || opacity_ == 0) return result;
    result.reserve(particles_.size());
    for (const auto& p : particles_) {
        if (!p.active) continue;
        auto layer = sprites_.at(static_cast<std::size_t>(p.sprite));
        layer.x = static_cast<float>(std::trunc(p.x))-50;
        layer.y = static_cast<float>(std::trunc(p.y))-30;
        layer.color[3] *= opacity();
        result.push_back(std::move(layer));
    }
    return result;
}
}
