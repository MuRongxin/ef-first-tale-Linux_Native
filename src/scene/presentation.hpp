#pragma once
#include "audio/mixer.hpp"
#include "scene/scene.hpp"
#include "scene/snow.hpp"
#include "scene/shake.hpp"
#include "scene/cutin.hpp"
#include "scene/walking.hpp"
#include "script/script.hpp"
#include <optional>

namespace ef {
// Development scene host. Unsupported effects remain strict script errors.
class Presentation {
public:
    explicit Presentation(const Content& content) : content_(content) {}
    bool command(const Instruction& instruction);
    void message(const Message& message);
    void update(double seconds);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] const std::vector<Layer>& previous() const { return previous_; }
    [[nodiscard]] std::vector<Layer> current() const;
    [[nodiscard]] float progress() const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] int panel_mode() const { return panel_; }
    [[nodiscard]] bool panel_visible() const { return panel_ != 0; }
    [[nodiscard]] std::vector<Layer> effects() const { if (cutin_) return {cutin_->layer()}; return snow_ ? snow_->layers() : std::vector<Layer>{}; }
    bool shaking() const { return shake_ && !shake_->finished(); }
    ShakeFrame shake_frame() const { return shake_ ? shake_->frame() : ShakeFrame{}; }
    std::uint64_t shake_serial() const { return shake_serial_; }
    int transition_type() const { return active_type_; }
    const std::string& transition_mask() const { return active_mask_; }
    AudioMixer& mixer() { return mixer_; }
    void realtime_audio(bool enabled) { realtime_ = enabled; }
private:
    void stop_walking();
    Layer image(const std::string& name, bool r18) const;
    void voice(const std::string& name, bool r18);
    void play(AudioChannel channel, const std::string& kind, const std::string& name,
              bool r18, bool loop, float gain, double fade, double fade_out_rate=0);
    const Content& content_;
    AudioMixer mixer_;
    std::array<std::string,4> playing_ids_;
    std::array<bool,4> playing_loops_{};
    bool realtime_{};
    std::optional<Snow> snow_;
    std::optional<Walking> walking_;
    std::optional<Cutin> cutin_;
    std::optional<Shake> shake_;
    std::uint64_t shake_serial_{};
    std::vector<Layer> previous_, current_;
    std::uint64_t revision_{};
    int speed_{9999}, panel_{3},active_speed_{9999},transition_type_{},active_type_{};
    std::string transition_mask_,active_mask_;
    std::array<std::array<int,3>,2> hsv_{};
    double elapsed_{};
    bool fading_{};
    struct Scroll { std::size_t layer; float x, y, target_x, target_y; double elapsed{}, speed{}; };
    std::optional<Scroll> scroll_;
    bool wait_scroll_{};
    std::optional<std::size_t> background_;
    std::array<std::string,3> screens_,overlays_; // pending frame/background/foreground materials
};
}
