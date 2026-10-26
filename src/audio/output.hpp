#pragma once
#include "audio/mixer.hpp"
#include <array>
#include <atomic>

struct SDL_AudioStream;
namespace ef {
// Mixer must outlive this object. Disabled mode never initializes SDL audio.
class AudioOutput {
public:
    explicit AudioOutput(AudioMixer& mixer, bool enabled = true);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    [[nodiscard]] bool active() const { return stream_ != nullptr; }
    [[nodiscard]] bool failed() const { return failed_.load(); }
    // Movie audio is streamed from the control thread and mixed by the same callback.
    // The ring holds at most two seconds of 48 kHz stereo float samples.
    void movie_begin();
    void movie_end();
    [[nodiscard]] std::size_t movie_push(const float* interleaved, std::size_t frames);
    [[nodiscard]] std::size_t movie_queued_frames() const;
    [[nodiscard]] std::uint64_t movie_played_frames() const;
private:
    static void callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void mix_movie(float* output, std::size_t frames);
    AudioMixer& mixer_;
    SDL_AudioStream* stream_{};
    std::array<float, 2048> buffer_{};
    std::atomic<bool> failed_{};
    bool initialized_{};
    static constexpr std::size_t movie_capacity_ = 96000; // frames (2 s at 48 kHz)
    std::array<float, movie_capacity_ * 2> movie_ring_{};
    std::atomic<std::uint64_t> movie_read_{}, movie_write_{}, movie_played_{};
    std::atomic<bool> movie_active_{};
};
}
