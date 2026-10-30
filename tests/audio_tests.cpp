#include "audio/clip.hpp"
#include "audio/mixer.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected) { require(std::abs(actual-expected) < 0.00001f, "Unexpected mixed PCM sample"); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path() / ("test-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void le16(std::ostream& out, std::uint16_t value) { out.put(static_cast<char>(value & 255)); out.put(static_cast<char>(value >> 8)); }
void le32(std::ostream& out, std::uint32_t value) { le16(out, static_cast<std::uint16_t>(value)); le16(out, static_cast<std::uint16_t>(value >> 16)); }
void wav(const std::filesystem::path& path, bool floating) {
    std::ofstream out(path, std::ios::binary);
    const unsigned sample_bytes = floating ? 4 : 2;
    out.write("RIFF",4); le32(out,36+sample_bytes*4); out.write("WAVEfmt ",8); le32(out,16);
    le16(out,floating ? 3 : 1); le16(out,1); le32(out,24000); le32(out,24000*sample_bytes);
    le16(out,static_cast<std::uint16_t>(sample_bytes)); le16(out,static_cast<std::uint16_t>(sample_bytes*8));
    out.write("data",4); le32(out,sample_bytes*4);
    if (floating) { for (float value : {0.0f,0.25f,-0.5f,0.75f}) le32(out,std::bit_cast<std::uint32_t>(value)); }
    else { for (const std::int16_t value : std::array<std::int16_t,4>{0,8192,-16384,24576}) le16(out,std::bit_cast<std::uint16_t>(value)); }
}
std::shared_ptr<const ef::AudioClip> clip(std::vector<float> samples, int rate = 48000, int channels = 1) {
    return std::make_shared<const ef::AudioClip>(ef::AudioClip{rate,channels,std::move(samples)});
}
}
int main(int argc, char** argv) {
    try {
        using namespace ef;
        Temporary temp;
        for (const bool floating : {false,true}) {
            const auto path = temp.path / (floating ? "float.wav" : "pcm.false-extension");
            wav(path,floating);
            const auto decoded = decode_audio(path);
            require(decoded->channels == 1 && decoded->sample_rate == 24000 && decoded->frames() == 4, "WAVE dimensions and magic sniffing");
            near(decoded->samples[1],0.25f); near(decoded->samples[2],-0.5f); near(decoded->samples[3],0.75f);
        }
        std::ofstream(temp.path / "broken.ogg",std::ios::binary) << "OggSbad";
        rejects([&] { (void)decode_audio(temp.path / "broken.ogg"); });
        rejects([&] { (void)decode_audio(temp.path / "missing"); });
        wav(temp.path / "truncated.wav",false);
        std::filesystem::resize_file(temp.path / "truncated.wav",45);
        rejects([&] { (void)decode_audio(temp.path / "truncated.wav"); });
        // Self-generated fixture; no game content. Generate with:
        // ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=22050:duration=0.032'
        //        -ac 1 -c:a libvorbis -q:a 4 tests/fixtures/audio-tone.ogg
        const auto ogg = decode_audio(EF_AUDIO_FIXTURE);
        require(ogg->sample_rate == 22050 && ogg->channels == 1 && ogg->frames() > 400 && ogg->frames() < 1000, "Vorbis format and granule duration");
        require(std::any_of(ogg->samples.begin(),ogg->samples.end(),[](float value) { return std::abs(value) > 0.02f; }), "Vorbis tone must decode into nonzero PCM");

        AudioMixer original(24000);
        original.play(AudioChannel::bgm,clip({.1f,.2f}),true,1,0,1);
        original.advance(.00001);
        AudioMixer fork(original);
        std::array<float,8> original_pcm{},fork_pcm{};
        original.mix(original_pcm);fork.mix(fork_pcm);
        require(original_pcm==fork_pcm,"Mixer checkpoint lost fractional position or fade envelope");
        require(fork.sample_rate()==24000 && fork.playing(AudioChannel::bgm),"Mixer checkpoint lost clock or active track");
        original.stop_all();
        require(fork.playing(AudioChannel::bgm),"Mixer checkpoint shares mutable playback state");
        fork.advance(20);
        require(fork.playing(AudioChannel::bgm),"Forked loop did not survive its independent clock");
        AudioMixer mixer;
        std::array<float,12> output{};
        mixer.play(AudioChannel::bgm,clip({0.1f,0.2f}),true);
        mixer.play(AudioChannel::voice,clip({0.4f,0.6f}));
        mixer.mix(output);
        near(output[0],0.5f); near(output[2],0.8f); near(output[4],0.1f); near(output[6],0.2f);
        require(!mixer.playing(AudioChannel::voice) && mixer.playing(AudioChannel::bgm), "Independent one-shot voice and whole-track BGM loop");
        mixer.stop_all();
        mixer.play(AudioChannel::se,clip({0.5f,0.5f}),false,1,-1);
        mixer.play(AudioChannel::se2,clip({0.25f,0.25f}),false,1,1);
        mixer.mix(output);
        near(output[0],0.5f); near(output[1],0.25f);
        require(!mixer.playing(AudioChannel::se) && !mixer.playing(AudioChannel::se2), "Separate SE channels finish");
        mixer.play(AudioChannel::voice,clip({0.9f}),true);
        mixer.play(AudioChannel::voice,clip({0.2f,0.3f}));
        mixer.mix(output);
        near(output[0],0.2f); near(output[2],0.3f); near(output[4],0);
        mixer.play(AudioChannel::voice,clip({0,1,0,-1}),false,1,0,0);
        mixer.set_gain(AudioChannel::voice,0.5f);
        mixer.mix(output);
        near(output[0],0); near(output[2],0.5f); near(output[6],-0.5f);

        mixer.play(AudioChannel::bgm,clip({1}),true,1,0,4.0/48000);
        mixer.mix(std::span(output.data(),8));
        near(output[0],0); near(output[2],0.25f); near(output[4],0.5f); near(output[6],0.75f);
        mixer.stop(AudioChannel::bgm,4.0/48000);
        mixer.mix(std::span(output.data(),8));
        near(output[0],1); near(output[2],0.75f); near(output[4],0.5f); near(output[6],0.25f);
        require(!mixer.playing(AudioChannel::bgm), "Fade stop ends exactly after requested output frames");
        mixer.play(AudioChannel::se,clip({0,1},24000));
        mixer.mix(std::span(output.data(),8));
        near(output[0],0); near(output[2],0.5f); near(output[4],1); near(output[6],1);
        mixer.play(AudioChannel::voice,clip({1}),true);
        mixer.play(AudioChannel::bgm,clip({1}),true);
        mixer.mix(std::span(output.data(),2));
        near(output[0],1); near(output[1],1);
        mixer.stop_all();
        mixer.play(AudioChannel::voice,ogg);
        mixer.play(AudioChannel::bgm,ogg,true);
        mixer.advance(86400);
        require(!mixer.playing(AudioChannel::voice) && mixer.playing(AudioChannel::bgm), "Headless analytical advancement is bounded and loop-aware");
        mixer.stop(AudioChannel::bgm,0.01); mixer.advance(0.01);
        require(!mixer.playing(AudioChannel::bgm), "Headless fade completion");
        mixer.collect_finished();
        mixer.stop_all();
        // Independent outgoing/incoming envelopes, sample-accurate at the callback boundary.
        auto outgoing=clip({0.4f}); std::weak_ptr<const AudioClip> old_pcm=outgoing;
        mixer.play(AudioChannel::bgm,outgoing,true); outgoing.reset();
        mixer.play(AudioChannel::bgm,clip({0.8f}),true,1,0,4.0/48000,4.0/48000);
        mixer.mix(std::span(output.data(),8));
        near(output[0],0.4f); near(output[2],0.5f); near(output[4],0.6f); near(output[6],0.7f);
        require(!old_pcm.expired(),"Callback must defer outgoing PCM release");
        mixer.collect_finished(); require(old_pcm.expired(),"Control thread reclaims completed outgoing PCM");
        mixer.mix(std::span(output.data(),2)); near(output[0],0.8f);
        mixer.stop_all();
        mixer.play(AudioChannel::bgm,clip({0.4f}),true);
        mixer.play(AudioChannel::bgm,clip({0.8f}),true,1,0,4.0/48000,8.0/48000);
        mixer.mix(std::span(output.data(),4)); near(output[0],0.4f); near(output[2],0.55f);
        // Interrupt halfway into fade-in. The oldest tail keeps its envelope.
        mixer.play(AudioChannel::bgm,clip({0.2f}),true,1,0,2.0/48000,4.0/48000);
        mixer.mix(std::span(output.data(),4)); near(output[0],0.7f); near(output[2],0.65f);
        mixer.stop_current(AudioChannel::bgm);
        require(!mixer.current_playing(AudioChannel::bgm) && mixer.playing(AudioChannel::bgm),"Current-only stop preserves earlier outgoing voices");
        mixer.stop(AudioChannel::bgm);
        require(!mixer.playing(AudioChannel::bgm),"Channel stop stops every outgoing voice");
        mixer.mix(output); near(output[0],0);
        mixer.play(AudioChannel::se,clip({0.2f}),true);
        mixer.stop_current(AudioChannel::se,4.0/48000);
        require(!mixer.current_playing(AudioChannel::se) && mixer.playing(AudioChannel::se),"Fading stop retires the current track immediately");
        mixer.advance(2.0/48000); mixer.stop_current(AudioChannel::se,100);
        mixer.advance(2.0/48000);
        require(!mixer.playing(AudioChannel::se),"Repeated script stop must not extend an already outgoing envelope");
        // Analytic silent clock and real mixing must produce the same future PCM.
        AudioMixer mixed,analytic;
        for (auto* actual : {&mixed,&analytic}) {
            actual->play(AudioChannel::bgm,clip({0,0.2f,0.4f,0.6f},24000),true);
            actual->play(AudioChannel::bgm,clip({0.5f,0.25f,0.1f},48000),true,0.75f,0,8.0/48000,12.0/48000);
        }
        mixed.mix(std::span(output.data(),12)); analytic.advance(6.0/48000);
        std::array<float,12> expected{}; mixed.mix(expected); analytic.mix(output);
        for (std::size_t n=0; n<output.size(); ++n) near(output[n],expected[n]);
        mixer.stop_all();
        mixer.play(AudioChannel::bgm,clip({0.4f}),true,1,0,1);
        mixer.advance(.5); // Gain has changed during the control thread's hypothetical decode window.
        mixer.play(AudioChannel::bgm,clip({0}),true,1,0,0,0,1);
        mixer.mix(std::span(output.data(),2)); near(output[0],.2f);
        mixer.advance(.25); mixer.mix(std::span(output.data(),2));
        require(output[0]>.09f && output[0]<.11f,"Outgoing rate uses gain at commit, not before decode");
        mixer.stop_all(); mixer.play(AudioChannel::bgm,clip({.2f}),true,.5f);
        mixer.set_gain(AudioChannel::bgm,.25f,0,.25); mixer.advance(.5);
        near(mixer.gain(AudioChannel::bgm),.375f);
        mixer.stop_current(AudioChannel::bgm,0,.75); mixer.advance(.49);
        require(mixer.playing(AudioChannel::bgm),"Rate-based stop ended before its current-gain duration");
        mixer.advance(.02); require(!mixer.playing(AudioChannel::bgm),"Rate-based stop did not complete");
        // Capacity exhaustion is atomic. An immediate replacement still works at the limit.
        mixer.stop_all(); auto constant=clip({0.01f});
        mixer.play(AudioChannel::bgm,constant,true);
        for (int n=0; n<9; ++n) mixer.play(AudioChannel::bgm,constant,true,1,0,0,1);
        rejects([&] { mixer.play(AudioChannel::bgm,clip({0.9f}),true,1,0,0,1); });
        mixer.mix(std::span(output.data(),2)); near(output[0],0.1f);
        mixer.play(AudioChannel::bgm,constant,true); mixer.stop_all();
        require(!mixer.playing(AudioChannel::bgm),"Stop all clears the complete overlap pool");
        rejects([&] { mixer.play(AudioChannel::voice,clip({std::numeric_limits<float>::quiet_NaN()})); });
        rejects([&] { mixer.set_gain(AudioChannel::bgm,-1); });
        rejects([&] { mixer.play(AudioChannel::bgm,constant,true,1,0,0,0,-1); });
        rejects([&] { mixer.stop_current(AudioChannel::bgm,0,std::numeric_limits<double>::infinity()); });
        rejects([&] { mixer.advance(-1); });
        rejects([&] { mixer.mix(std::span(output.data(),3)); });
        // Native notification path averages absolute PCM16 over 8192 bytes,
        // before gain/pan. Stereo has 2048 frames per packet, mono 4096.
        AudioClip words{48000,1,std::vector<float>(4096,-1.0f)};
        require(make_voice_envelope(words)==std::vector<std::uint32_t>{32768},"PCM16 -32768 absolute value");
        words.samples.assign(4096,800.0f/32768);words.samples[0]=0;
        require(make_voice_envelope(words)[0]==799,"Mean absolute amplitude must floor below threshold");
        words.samples.assign(1,1.0f);require(make_voice_envelope(words)[0]==7,"Last packet zero padding");
        std::vector<float> speech_pcm(4096*4,.125f);
        std::fill(speech_pcm.begin()+4096,speech_pcm.begin()+8192,-.25f);
        const auto speech_clip=clip(speech_pcm);
        AudioMixer spoken(48000),spoken_mix(48000);
        for (auto* m:{&spoken,&spoken_mix}) {
            m->play(AudioChannel::bgm,clip({1}),true);
            m->play(AudioChannel::voice,speech_clip,false,0,-1,0,0,0,"voice/test");
            require(m->voice_envelope().active && m->voice_envelope().amplitude==0,"Envelope must wait for completed PCM block");
        }
        spoken.advance(4095.0/48000);require(spoken.voice_envelope().serial==0,"Premature PCM envelope packet");
        spoken.advance(1.0/48000);
        std::vector<float> pcm_output(4096*2);spoken_mix.mix(pcm_output);
        require(spoken.voice_envelope().amplitude==4096 && spoken.voice_envelope().serial==1,"Completed mono meanabs/gain independence");
        require(spoken_mix.voice_envelope().amplitude==spoken.voice_envelope().amplitude,"Device and silent envelope clocks disagree");
        spoken.advance(4096.0/48000);require(spoken.voice_envelope().amplitude==8192,"Second PCM packet");
        spoken.stop_current(AudioChannel::voice,1);require(!spoken.voice_envelope().active,"Outgoing voice fade must not own lips");
        spoken.play(AudioChannel::voice,clip(speech_pcm,48000,2),false,1,0,0,0,0,"voice/stereo");
        spoken.advance(2048.0/48000);require(spoken.voice_envelope().amplitude==4096 && spoken.voice_envelope().serial==1,"Stereo words/frame envelope boundary");
        spoken.advance(1);require(!spoken.voice_envelope().active,"Completed voice retained a stale packet");
        spoken.play(AudioChannel::voice,speech_clip);require(spoken.voice_envelope().serial==0 && spoken.voice_envelope().amplitude==0,"Replayed voice retained prior amplitude");
        spoken.stop_all();require(!spoken.voice_envelope().active,"Stopped voice envelope");
        for (int n = 1; n < argc; ++n) {
            const auto actual = decode_audio(argv[n]);
            std::cout << "decoded " << argv[n] << " rate=" << actual->sample_rate << " channels=" << actual->channels << " frames=" << actual->frames() << " seconds=" << actual->duration() << '\n';
        }
        std::cout << "Audio tests PASS: OGG/WAV, PCM mixing, loops, replacement, pan, resampling, fades, clipping, offline clock and invalid input\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
