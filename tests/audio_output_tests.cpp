#include "audio/output.hpp"
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    try {
        using namespace ef;
        AudioMixer mixer;
        {
            AudioOutput disabled(mixer,false);
            if (disabled.active() || disabled.failed() || SDL_WasInit(SDL_INIT_AUDIO))
                throw std::runtime_error("Disabled audio unexpectedly initialized a device");
        }
        // Always force a silent device, including direct/manual execution.
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE);
        auto clip = std::make_shared<const AudioClip>(AudioClip{48000,1,std::vector<float>(960,0.125f)});
        mixer.play(AudioChannel::voice,clip);
        {
            AudioOutput output(mixer);
            if (!output.active() || !SDL_WasInit(SDL_INIT_AUDIO)) throw std::runtime_error("Dummy output did not initialize");
            for (int n = 0; n < 100 && mixer.playing(AudioChannel::voice); ++n)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (output.failed() || mixer.playing(AudioChannel::voice)) throw std::runtime_error("Audio stream callback did not consume PCM");
        }
        if (SDL_WasInit(SDL_INIT_AUDIO)) throw std::runtime_error("Audio subsystem reference leaked");
        std::cout << "Audio output PASS: disabled mode and silent SDL device callback/lifecycle\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
