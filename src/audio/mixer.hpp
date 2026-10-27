#pragma once
#include "audio/clip.hpp"
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>

namespace ef {
enum class AudioChannel : std::size_t { bgm, voice, se, se2 };
class AudioMixer {
    friend struct CheckpointCodec;
public:
    explicit AudioMixer(int output_rate = 48000);
    // Independent silent/checkpoint playback state; immutable PCM is shared.
    AudioMixer(const AudioMixer& other);
    // Positive rates (linear gain/second) override durations and are resolved under the commit lock.
    void play(AudioChannel channel, std::shared_ptr<const AudioClip> clip, bool loop = false,
              float gain = 1, float pan = 0, double fade_in_seconds = 0, double fade_out_seconds = 0, double fade_out_rate = 0, std::string resource_id = {});
    void stop(AudioChannel channel, double fade_out_seconds = 0);
    // Script playback owns the current track; earlier outgoing fades keep their envelopes.
    void stop_current(AudioChannel channel, double fade_out_seconds = 0, double fade_out_rate = 0);
    [[nodiscard]] bool current_playing(AudioChannel channel) const;
    void set_gain(AudioChannel channel, float gain, double fade_seconds = 0, double rate = 0);
    void stop_all();
    [[nodiscard]] bool playing(AudioChannel channel) const;
    [[nodiscard]] float gain(AudioChannel channel) const;
    [[nodiscard]] int sample_rate() const { return output_rate_; }
    // Callback-safe: fixed work per frame, no file I/O, decoding or clip allocation/free.
    void mix(std::span<float> interleaved_stereo);
    // Analytical, silent clock for headless execution; does not open an audio device.
    void advance(double seconds);
    // Optional control-thread reclamation of clips retained after callback completion.
    void collect_finished();
private:
    struct Slot {
        std::shared_ptr<const AudioClip> clip;
        double position{};
        double gain{}, target{}, gain_step{};
        float pan{};
        std::uint64_t ramp_left{};
        bool active{}, loop{}, stop_after_ramp{};
        AudioChannel channel{};
        std::string resource_id;
    };
    [[nodiscard]] std::uint64_t duration_frames(double seconds) const;
    static std::size_t index(AudioChannel channel);
    static void ramp(Slot& slot, std::uint64_t frames);
    int output_rate_;
    mutable std::mutex mutex_;
    // Four current tracks plus fixed storage for outgoing fades. At most ten active voices.
    static constexpr std::size_t channel_count=4, voice_limit=10;
    std::array<Slot, channel_count+voice_limit> slots_;
};
}
