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
private:
    static void callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    AudioMixer& mixer_;
    SDL_AudioStream* stream_{};
    std::array<float, 2048> buffer_{};
    std::atomic<bool> failed_{};
    bool initialized_{};
};
}
