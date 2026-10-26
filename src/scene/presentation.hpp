#pragma once
#include "audio/mixer.hpp"
#include "scene/scene.hpp"
#include "scene/snow.hpp"
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
    [[nodiscard]] const std::vector<Layer>& current() const { return current_; }
    [[nodiscard]] float progress() const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] int panel_mode() const { return panel_; }
    [[nodiscard]] bool panel_visible() const { return panel_ != 0; }
    [[nodiscard]] std::vector<Layer> effects() const { return snow_ ? snow_->layers() : std::vector<Layer>{}; }
    AudioMixer& mixer() { return mixer_; }
    void realtime_audio(bool enabled) { realtime_ = enabled; }
private:
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
    std::vector<Layer> previous_, current_;
    std::uint64_t revision_{};
    int speed_{9999}, panel_{3};
    double elapsed_{};
    bool fading_{};
    struct Scroll { std::size_t layer; float x, y, target_x, target_y; double elapsed{}, speed{}; };
    std::optional<Scroll> scroll_;
    bool wait_scroll_{};
    std::optional<std::size_t> background_;
};
}
