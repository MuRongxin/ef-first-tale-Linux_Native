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
            self.mix_movie(self.buffer_.data(), frames);
            const auto bytes = static_cast<int>(frames)*frame_bytes;
            if (!SDL_PutAudioStreamData(stream, self.buffer_.data(), bytes)) { self.failed_ = true; return; }
            additional -= bytes;
        }
    } catch (...) { self.failed_ = true; }
}
void AudioOutput::movie_begin() {
    movie_active_.store(false, std::memory_order_release);
    movie_read_.store(0, std::memory_order_relaxed);
    movie_write_.store(0, std::memory_order_relaxed);
    movie_played_.store(0, std::memory_order_relaxed);
    movie_active_.store(true, std::memory_order_release);
}
void AudioOutput::movie_end() { movie_active_.store(false, std::memory_order_release); }
std::size_t AudioOutput::movie_push(const float* interleaved, std::size_t frames) {
    if (!interleaved || !movie_active_.load(std::memory_order_acquire)) return 0;
    const std::uint64_t write = movie_write_.load(std::memory_order_relaxed);
    const std::uint64_t read = movie_read_.load(std::memory_order_acquire);
    const std::uint64_t queued = write - read;
    const std::size_t free_frames = queued >= movie_capacity_ ? 0 : movie_capacity_ - static_cast<std::size_t>(queued);
    const std::size_t count = std::min(frames, free_frames);
    for (std::size_t n = 0; n < count; ++n) {
        const auto index = static_cast<std::size_t>((write + n) % movie_capacity_);
        movie_ring_[index * 2] = interleaved[n * 2];
        movie_ring_[index * 2 + 1] = interleaved[n * 2 + 1];
    }
    movie_write_.store(write + count, std::memory_order_release);
    return count;
}
std::size_t AudioOutput::movie_queued_frames() const {
    if (!movie_active_.load(std::memory_order_acquire)) return 0;
    const std::uint64_t write = movie_write_.load(std::memory_order_acquire);
    const std::uint64_t read = movie_read_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(std::min<std::uint64_t>(write - read, movie_capacity_));
}
std::uint64_t AudioOutput::movie_played_frames() const { return movie_played_.load(std::memory_order_relaxed); }
void AudioOutput::mix_movie(float* output, std::size_t frames) {
    if (!movie_active_.load(std::memory_order_acquire)) return;
    const std::uint64_t write = movie_write_.load(std::memory_order_acquire);
    const std::uint64_t read = movie_read_.load(std::memory_order_relaxed);
    const std::uint64_t queued = std::min<std::uint64_t>(write - read, movie_capacity_);
    const std::size_t count = std::min(frames, static_cast<std::size_t>(queued));
    for (std::size_t n = 0; n < count; ++n) {
        const auto index = static_cast<std::size_t>((read + n) % movie_capacity_);
        output[n * 2] += movie_ring_[index * 2];
        output[n * 2 + 1] += movie_ring_[index * 2 + 1];
    }
    movie_read_.store(read + count, std::memory_order_release);
    movie_played_.store(movie_played_.load(std::memory_order_relaxed) + count, std::memory_order_relaxed);
}
}
