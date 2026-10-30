#include "audio/mixer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ef {
namespace {
void gain_value(float gain) {
    if (!std::isfinite(gain) || gain < 0 || gain > 1) throw std::runtime_error("Audio gain must be 0..1");
}
void fade_rate(double rate) {
    if (!std::isfinite(rate) || rate<0) throw std::runtime_error("Invalid audio fade rate");
}
}
AudioMixer::AudioMixer(int output_rate) : output_rate_(output_rate) {
    if (output_rate < 8000 || output_rate > 192000) throw std::runtime_error("Audio output rate must be 8..192 kHz");
}
AudioMixer::AudioMixer(const AudioMixer& other) : output_rate_(48000) {
    const std::lock_guard lock(other.mutex_);
    output_rate_=other.output_rate_;
    slots_=other.slots_;
}
std::size_t AudioMixer::index(AudioChannel channel) {
    const auto result = static_cast<std::size_t>(channel);
    if (result >= 4) throw std::runtime_error("Invalid audio channel");
    return result;
}
std::uint64_t AudioMixer::duration_frames(double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9) throw std::runtime_error("Invalid audio duration");
    return static_cast<std::uint64_t>(std::ceil(seconds * output_rate_));
}
void AudioMixer::play(AudioChannel channel, std::shared_ptr<const AudioClip> clip, bool loop, float gain, float pan, double fade_in_seconds, double fade_out_seconds, double fade_out_rate, std::string resource_id) {
    const auto i = index(channel);
    gain_value(gain);
    if (!std::isfinite(pan) || pan < -1 || pan > 1) throw std::runtime_error("Audio pan must be -1..1");
    const auto fade = duration_frames(fade_in_seconds);
    const auto requested_out = duration_frames(fade_out_seconds);
    fade_rate(fade_out_rate);
    if (!clip || clip->sample_rate < 8000 || clip->sample_rate > 192000 || (clip->channels != 1 && clip->channels != 2) ||
        clip->samples.empty() || clip->samples.size() % static_cast<std::size_t>(clip->channels))
        throw std::runtime_error("Invalid audio clip");
    // Validate caller-provided PCM on the control thread, never in the callback.
    if (!std::all_of(clip->samples.begin(), clip->samples.end(), [](float value) { return std::isfinite(value); }))
        throw std::runtime_error("Non-finite audio clip");
    std::shared_ptr<const std::vector<std::uint32_t>> envelope;
    if (channel==AudioChannel::voice) envelope=std::make_shared<const std::vector<std::uint32_t>>(make_voice_envelope(*clip));
    std::shared_ptr<const AudioClip> retired;
    {
        std::scoped_lock lock(mutex_);
        auto& current=slots_[i];
        const auto out=fade_out_rate>0 ? duration_frames(current.gain/fade_out_rate) : requested_out;
        const bool preserve=current.active && out && current.gain>0;
        const auto active=std::count_if(slots_.begin(),slots_.end(),[](const Slot& slot) { return slot.active; });
        if ((!current.active || preserve) && active>=static_cast<std::ptrdiff_t>(voice_limit))
            throw std::runtime_error("Audio overlap voice limit reached");
        if (preserve) {
            auto tail=std::find_if(slots_.begin()+channel_count,slots_.end(),[](const Slot& slot) { return !slot.active; });
            retired=std::move(tail->clip);
            *tail=std::move(current);
            tail->target=0; tail->gain_step=-tail->gain/static_cast<double>(out);
            tail->ramp_left=out; tail->stop_after_ramp=true;
        } else retired=std::move(current.clip);
        slots_[i] = Slot{std::move(clip), 0, fade ? 0.0 : gain, gain, fade ? gain/static_cast<double>(fade) : 0,
                         pan, fade, true, loop, false, channel, std::move(resource_id),std::move(envelope)};
    } // Retired PCM is freed outside the callback's lock.
}
void AudioMixer::stop_current(AudioChannel channel,double fade_out_seconds,double rate) {
    const auto i=index(channel),requested=duration_frames(fade_out_seconds);
    fade_rate(rate);
    std::shared_ptr<const AudioClip> retired;
    {
        std::scoped_lock lock(mutex_);
        auto& current=slots_[i];
        const auto frames=rate>0 ? duration_frames(current.gain/rate) : requested;
        if (current.active && frames && current.gain>0) {
            auto tail=std::find_if(slots_.begin()+channel_count,slots_.end(),[](const Slot& slot) { return !slot.active; });
            retired=std::move(tail->clip); *tail=std::move(current);
            tail->target=0; tail->gain_step=-tail->gain/static_cast<double>(frames);
            tail->ramp_left=frames; tail->stop_after_ramp=true;
            current={};
        } else { current.active=false; retired=std::move(current.clip); }
    }
}
void AudioMixer::stop(AudioChannel channel,double fade_out_seconds) {
    const auto i=index(channel),frames=duration_frames(fade_out_seconds);
    std::array<std::shared_ptr<const AudioClip>,channel_count+voice_limit> retired;
    {
        std::scoped_lock lock(mutex_);
        for (std::size_t n=0; n<slots_.size(); ++n) {
            auto& slot=slots_[n];
            if (n!=i && !(n>=channel_count && slot.channel==channel)) continue;
            if (!frames || !slot.active) { slot.active=false; retired[n]=std::move(slot.clip); }
            else {
                slot.target=0; slot.gain_step=-slot.gain/static_cast<double>(frames);
                slot.ramp_left=frames; slot.stop_after_ramp=true;
            }
        }
    }
}
void AudioMixer::set_gain(AudioChannel channel, float gain, double fade_seconds, double rate) {
    const auto i = index(channel), requested = duration_frames(fade_seconds);
    gain_value(gain); fade_rate(rate);
    std::scoped_lock lock(mutex_);
    auto& slot = slots_[i];
    const auto frames=rate>0 ? duration_frames(std::abs(gain-slot.gain)/rate) : requested;
    slot.target = gain; slot.stop_after_ramp = false; slot.ramp_left = frames;
    slot.gain_step = frames ? (gain-slot.gain)/static_cast<double>(frames) : 0;
    if (!frames) slot.gain = gain;
}
void AudioMixer::stop_all() {
    std::array<Slot,channel_count+voice_limit> retired;
    {
        std::scoped_lock lock(mutex_);
        std::swap(retired,slots_);
    }
}
bool AudioMixer::playing(AudioChannel channel) const {
    const auto i = index(channel);
    std::scoped_lock lock(mutex_);
    if (slots_[i].active) return true;
    return std::any_of(slots_.begin()+channel_count,slots_.end(),[&](const Slot& slot) { return slot.active && slot.channel==channel; });
}
bool AudioMixer::current_playing(AudioChannel channel) const {
    const auto i=index(channel);
    std::scoped_lock lock(mutex_);
    return slots_[i].active;
}
VoiceEnvelope AudioMixer::voice_envelope() const {
    std::scoped_lock lock(mutex_);
    const auto& slot=slots_[index(AudioChannel::voice)];
    if (!slot.active || !slot.clip || !slot.envelope) return {};
    const auto words=static_cast<std::size_t>(slot.position)*static_cast<std::size_t>(slot.clip->channels);
    const auto block=words/voice_envelope_words;
    const auto amplitude=block && block<=slot.envelope->size() ? slot.envelope->at(block-1) : 0;
    return {amplitude,static_cast<std::uint32_t>(block),true,slot.resource_id};
}
float AudioMixer::gain(AudioChannel channel) const {
    const auto i = index(channel);
    std::scoped_lock lock(mutex_);
    return slots_[i].active ? static_cast<float>(slots_[i].gain) : 0;
}
void AudioMixer::ramp(Slot& slot, std::uint64_t frames) {
    if (!slot.ramp_left) return;
    const auto amount = std::min(slot.ramp_left, frames);
    slot.gain += slot.gain_step * static_cast<double>(amount);
    slot.ramp_left -= amount;
    if (!slot.ramp_left) {
        slot.gain = slot.target;
        if (slot.stop_after_ramp) slot.active = false;
    }
}
void AudioMixer::mix(std::span<float> output) {
    if (output.size() % 2) throw std::runtime_error("Audio output must contain stereo frames");
    std::fill(output.begin(), output.end(), 0);
    std::scoped_lock lock(mutex_);
    for (std::size_t frame = 0; frame < output.size()/2; ++frame) {
        double left{}, right{};
        for (auto& slot : slots_) {
            if (!slot.active) continue;
            const auto& clip = *slot.clip;
            const auto count = clip.frames();
            const auto first = static_cast<std::size_t>(slot.position);
            const auto second = first+1 < count ? first+1 : slot.loop ? 0 : first;
            const auto fraction = static_cast<float>(slot.position - static_cast<double>(first));
            const auto sample = [&](std::size_t channel) {
                const auto channels = static_cast<std::size_t>(clip.channels);
                const double a = clip.samples[first*channels+channel], b = clip.samples[second*channels+channel];
                return a + (b-a)*fraction;
            };
            const double l = sample(0), r = sample(clip.channels == 1 ? 0U : 1U);
            // Linear balance preserves the centre amplitude; mono is duplicated.
            left += l * slot.gain * (slot.pan > 0 ? 1-slot.pan : 1);
            right += r * slot.gain * (slot.pan < 0 ? 1+slot.pan : 1);
            slot.position += static_cast<double>(clip.sample_rate) / output_rate_;
            if (slot.position >= static_cast<double>(count)) {
                if (slot.loop) slot.position = std::fmod(slot.position, static_cast<double>(count));
                else slot.active = false;
            }
            ramp(slot, 1);
        }
        output[frame*2] = static_cast<float>(std::clamp(left, -1.0, 1.0));
        output[frame*2+1] = static_cast<float>(std::clamp(right, -1.0, 1.0));
    }
}
void AudioMixer::advance(double seconds) {
    const auto frames = duration_frames(seconds);
    std::scoped_lock lock(mutex_);
    for (auto& slot : slots_) {
        if (!slot.active) continue;
        const auto count = static_cast<double>(slot.clip->frames());
        slot.position += static_cast<double>(frames)*slot.clip->sample_rate/output_rate_;
        if (slot.position >= count) {
            if (slot.loop) slot.position = std::fmod(slot.position, count);
            else slot.active = false;
        }
        ramp(slot, frames);
    }
}
void AudioMixer::collect_finished() {
    std::array<std::shared_ptr<const AudioClip>,channel_count+voice_limit> retired;
    {
        std::scoped_lock lock(mutex_);
        for (std::size_t n = 0; n < slots_.size(); ++n)
            if (!slots_[n].active) retired[n] = std::move(slots_[n].clip);
    }
}
}
