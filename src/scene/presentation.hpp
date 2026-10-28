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
// One .movie request; the application owns the decoder while the script host waits.
struct MovieRequest {
    std::string asset, path;
    int id{}, width{800}, height{600}, x{}, y{};
    bool skippable{true};
};
// Development scene host. Unsupported effects remain strict script errors.
class Presentation {
    friend struct CheckpointCodec;
public:
    using AudioDecoder = std::function<std::shared_ptr<const AudioClip>(const std::filesystem::path&)>;
    explicit Presentation(const Content& content, AudioDecoder decoder=decode_audio) : content_(content),decoder_(std::move(decoder)) {}
    bool command(const Instruction& instruction);
    void message(const Message& message);
    void update(double seconds);
    void local_assignment(const std::string& name, std::int64_t value);
    [[nodiscard]] std::int64_t finder_counter() const;
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
    void set_logging(bool enabled) { logging_ = enabled; }
    // Movie decoding lives in the application (FFmpeg); presentation only records the
    // request and blocks the script until the host reports completion.
    void set_movie_supported(bool supported) { movie_supported_ = supported; }
    [[nodiscard]] bool movie_active() const { return movie_active_; }
    [[nodiscard]] const std::optional<MovieRequest>& movie_request() const { return movie_request_; }
    void finish_movie() { movie_active_ = false; movie_request_.reset(); }
private:
    void stop_walking();
    std::vector<Layer> frame_layers() const;
    Layer image(const std::string& name, bool r18) const;
    void voice(const std::string& name, bool r18);
    void play(AudioChannel channel, const std::string& kind, const std::string& name,
              bool r18, bool loop, float gain, double fade, double fade_out_rate=0);
    const Content& content_;
    AudioDecoder decoder_;
    AudioMixer mixer_;
    std::array<std::string,4> playing_ids_;
    std::array<bool,4> playing_loops_{};
    bool realtime_{};
    bool logging_{}; // Control-thread diagnostics only; never used by the audio callback.
    std::optional<Snow> snow_;
    std::optional<Walking> walking_;
    std::optional<Cutin> cutin_;
    std::optional<Shake> shake_;
    std::uint64_t shake_serial_{};
    std::vector<Layer> previous_, current_, transition_frame_;
    std::optional<Layer> pending_frame_, frame_;
    std::array<Layer,4> finder_batteries_;
    bool pending_finder_{}, finder_{};
    double finder_ticks_{}; // Native global 10ms clock, including time with the finder hidden.
    std::uint64_t revision_{};
    int speed_{9999}, panel_{3},active_speed_{9999},transition_type_{},active_type_{};
    std::string transition_mask_,active_mask_;
    std::array<std::array<int,3>,2> hsv_{};
    double elapsed_{};
    bool fading_{};
    bool movie_supported_{}, movie_active_{};
    std::optional<MovieRequest> movie_request_;
    struct Scroll { std::size_t layer; float x, y, target_x, target_y; double elapsed{}, speed{}; };
    std::optional<Scroll> scroll_;
    bool wait_scroll_{};
    std::optional<std::size_t> background_;
    std::array<std::string,3> screens_,overlays_; // pending frame/background/foreground materials
};
}
