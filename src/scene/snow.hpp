#pragma once
#include "scene/scene.hpp"
#include <array>
#include <cstdint>

namespace ef {
// Reconstructed 50-particle Snow/SnowH effect. Random seed is a port choice.
class Snow {
public:
    Snow(std::array<Layer,3> sprites, bool horizontal, std::uint32_t seed = 1);
    void update(double seconds);
    void fade_out();
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] float opacity() const { return static_cast<float>(opacity_)/256; }
    [[nodiscard]] std::vector<Layer> layers() const;
private:
    struct Particle {
        double x{}, y{}, vx{}, vy{};
        int sprite{};
        bool active{true};
    };
    std::uint32_t random();
    void spawn(Particle& particle, bool entering);
    std::array<Layer,3> sprites_;
    std::array<Particle,50> particles_;
    std::uint32_t seed_;
    bool horizontal_{}, fading_{}, finished_{};
    int opacity_{};
    double time_{}, fade_ticks_{};
    std::uint64_t ticks_{};
};
}
