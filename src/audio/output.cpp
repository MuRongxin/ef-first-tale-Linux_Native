#include "audio/output.hpp"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_error.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <iostream>

namespace ef {
namespace {
std::string output_error(const char* phase) {
    std::string result=std::string(phase)+": "+SDL_GetError()+"; compiled audio drivers: ";
    bool native=false;
    for (int n=0; n<SDL_GetNumAudioDrivers(); ++n) {
        const std::string driver=SDL_GetAudioDriver(n);
        if (n) result+=", ";
        result+=driver;
        if (driver!="dummy" && driver!="disk") native=true;
    }
    if (!native) result+=". This SDL build lacks a native audio backend; install ALSA/PipeWire/PulseAudio development headers and rebuild SDL.";
    return result;
}
}
AudioOutput::AudioOutput(AudioMixer& mixer, bool enabled) : mixer_(mixer) {
    if (!enabled) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw std::runtime_error(output_error("SDL audio initialization"));
    initialized_ = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, mixer_.sample_rate()};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
        const std::string error = output_error("SDL audio output");
        if (stream_) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_ = false;
        throw std::runtime_error(error);
    }
    const auto* driver=SDL_GetCurrentAudioDriver();
    const auto* device=SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    std::cout << "[audio] driver=" << (driver ? driver : "unknown") << " device=" << (device ? device : "default") << '\n';
}
AudioOutput::~AudioOutput() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    if (initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void AudioOutput::callback(void* userdata, SDL_AudioStream* stream, int additional, int total) {
    (void)total;
    auto& self = *static_cast<AudioOutput*>(userdata);
    try {
        constexpr int frame_bytes = static_cast<int>(sizeof(float)*2);
        while (additional > 0 && !self.failed_.load()) {
            const auto frames = std::min<std::size_t>(self.buffer_.size()/2,
                (static_cast<std::size_t>(additional)+frame_bytes-1)/frame_bytes);
            self.mixer_.mix(std::span(self.buffer_.data(), frames*2));
            const auto bytes = static_cast<int>(frames)*frame_bytes;
            if (!SDL_PutAudioStreamData(stream, self.buffer_.data(), bytes)) { self.failed_ = true; return; }
            additional -= bytes;
        }
    } catch (...) { self.failed_ = true; }
}
}
