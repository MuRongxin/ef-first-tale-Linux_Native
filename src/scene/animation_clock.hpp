#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ef {
// Native absolute clock units; conversion from the host clock is intentionally separate.
// One update emits at most one timed state, discarding excess elapsed time.
class EyeClock {
    friend class ImageAnimation;
public:
    explicit EyeClock(int sequence=2);
    bool update(std::uint32_t tick, std::uint32_t random_value=0);
    [[nodiscard]] int frame() const { return frame_; }
    [[nodiscard]] std::uint32_t remaining() const { return remaining_; }
private:
    int sequence_, frame_{};
    std::size_t cursor_{};
    std::uint32_t last_{}, remaining_{};
    bool started_{};
};
// Native amplitude envelope input, not a guessed PCM/RMS conversion.
class LipState {
    friend class ImageAnimation;
public:
    bool update(std::uint32_t amplitude, std::uint32_t serial, bool active);
    [[nodiscard]] int frame() const { return frame_; }
private:
    int frame_{};
    std::uint32_t amplitude_{}, serial_{~std::uint32_t{0}};
};
struct AnimationStep {
    std::string frame; // * selects the unmodified base image.
    std::uint32_t duration{};
};
// Observed ANI grammar: @;name:duration;...;~. Other native opcodes stay unsupported.
std::vector<AnimationStep> parse_animation_loop(const std::string&);
class AnimationLoop {
    friend class ImageAnimation;
public:
    explicit AnimationLoop(std::vector<AnimationStep>);
    bool update(std::uint32_t tick);
    [[nodiscard]] const std::string& frame() const { return frame_; }
    [[nodiscard]] std::uint32_t remaining() const { return remaining_; }
private:
    std::vector<AnimationStep> steps_;
    std::string frame_{"*"};
    std::size_t cursor_{};
    std::uint32_t last_{}, remaining_{};
    bool started_{};
};
}
