#include "audio/clip.hpp"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include EF_STB_VORBIS_HEADER
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ef {
namespace {
constexpr std::size_t max_encoded = 64ULL * 1024 * 1024;
constexpr std::size_t max_pcm = 256ULL * 1024 * 1024;
using Bytes = std::vector<unsigned char>;
std::uint16_t u16(const Bytes& b, std::size_t p) {
    return static_cast<std::uint16_t>(b[p] | (static_cast<unsigned>(b[p+1]) << 8));
}
std::uint32_t u32(const Bytes& b, std::size_t p) {
    return static_cast<std::uint32_t>(b[p]) | (static_cast<std::uint32_t>(b[p+1]) << 8) |
        (static_cast<std::uint32_t>(b[p+2]) << 16) | (static_cast<std::uint32_t>(b[p+3]) << 24);
}
bool magic(const Bytes& bytes, std::size_t at, std::string_view text) {
    return at <= bytes.size() && text.size() <= bytes.size()-at &&
        std::equal(text.begin(), text.end(), bytes.begin()+static_cast<std::ptrdiff_t>(at));
}
void format(int rate, int channels) {
    if (rate < 8000 || rate > 192000 || (channels != 1 && channels != 2))
        throw std::runtime_error("Audio must be mono/stereo at 8..192 kHz");
}
AudioClip wav(const Bytes& bytes) {
    if (!magic(bytes, 8, "WAVE") || bytes.size() < 12) throw std::runtime_error("Invalid RIFF/WAVE header");
    const std::uint64_t end64 = static_cast<std::uint64_t>(u32(bytes, 4)) + 8;
    if (end64 > bytes.size() || end64 < 12) throw std::runtime_error("Truncated RIFF/WAVE");
    const auto end = static_cast<std::size_t>(end64);
    std::size_t fmt{}, fmt_size{}, data{}, data_size{};
    for (std::size_t pos = 12; pos + 8 <= end;) {
        const auto size = static_cast<std::size_t>(u32(bytes, pos+4));
        if (size > end-pos-8) throw std::runtime_error("Truncated WAVE chunk");
        if (magic(bytes, pos, "fmt ")) {
            if (fmt) throw std::runtime_error("Duplicate WAVE fmt chunk");
            fmt = pos+8; fmt_size = size;
        } else if (magic(bytes, pos, "data")) {
            if (data) throw std::runtime_error("Duplicate WAVE data chunk");
            data = pos+8; data_size = size;
        }
        pos += 8 + size;
        if (size & 1U) {
            if (pos == end) throw std::runtime_error("Missing WAVE chunk padding");
            ++pos;
        }
    }
    if (!fmt || fmt_size < 16 || !data || !data_size) throw std::runtime_error("Missing WAVE fmt/data");
    const unsigned encoding = u16(bytes, fmt), channels = u16(bytes, fmt+2), bits = u16(bytes, fmt+14);
    const auto rate = u32(bytes, fmt+4);
    if (rate > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid WAVE sample rate");
    format(static_cast<int>(rate), static_cast<int>(channels));
    if (!((encoding == 1 && bits == 16) || (encoding == 3 && bits == 32)))
        throw std::runtime_error("WAVE supports PCM16 or IEEE float32 only");
    const auto sample_bytes = bits / 8;
    const auto alignment = channels * sample_bytes;
    if (u16(bytes, fmt+12) != alignment || u32(bytes, fmt+8) != rate * alignment || data_size % alignment)
        throw std::runtime_error("Invalid WAVE block alignment/byte rate");
    const auto count = data_size / sample_bytes;
    if (count > max_pcm / sizeof(float)) throw std::runtime_error("Audio exceeds 256 MiB PCM budget");
    AudioClip clip{static_cast<int>(rate), static_cast<int>(channels), std::vector<float>(count)};
    for (std::size_t n = 0; n < count; ++n) {
        if (encoding == 1) {
            const auto signed_sample = std::bit_cast<std::int16_t>(u16(bytes, data+n*2));
            clip.samples[n] = static_cast<float>(signed_sample) / 32768.0f;
        } else clip.samples[n] = std::bit_cast<float>(u32(bytes, data+n*4));
    }
    return clip;
}
AudioClip vorbis(const Bytes& bytes) {
    int error{};
    struct Owner {
        stb_vorbis* value;
        ~Owner() { if (value) stb_vorbis_close(value); }
    } decoder{stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr)};
    if (!decoder.value) throw std::runtime_error("Vorbis open failed: " + std::to_string(error));
    const auto info = stb_vorbis_get_info(decoder.value);
    if (info.sample_rate > static_cast<unsigned>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid Vorbis sample rate");
    format(static_cast<int>(info.sample_rate), info.channels);
    const auto frames = stb_vorbis_stream_length_in_samples(decoder.value);
    if (!frames || frames > max_pcm / (static_cast<std::size_t>(info.channels)*sizeof(float)))
        throw std::runtime_error("Invalid Vorbis duration or 256 MiB PCM budget exceeded");
    const auto count = static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.channels);
    AudioClip clip{static_cast<int>(info.sample_rate), info.channels, std::vector<float>(count)};
    const int actual = stb_vorbis_get_samples_float_interleaved(decoder.value, info.channels, clip.samples.data(), static_cast<int>(count));
    error = stb_vorbis_get_error(decoder.value);
    if (actual <= 0 || error) throw std::runtime_error("Vorbis decode failed: " + std::to_string(error));
    clip.samples.resize(static_cast<std::size_t>(actual)*static_cast<std::size_t>(info.channels));
    return clip;
}
}
std::vector<std::uint32_t> make_voice_envelope(const AudioClip& clip) {
    if (clip.channels<1 || clip.channels>2 || clip.samples.size()%static_cast<std::size_t>(clip.channels)) throw std::runtime_error("Invalid voice PCM");
    std::vector<std::uint32_t> result;
    result.reserve((clip.samples.size()+voice_envelope_words-1)/voice_envelope_words);
    for (std::size_t begin=0;begin<clip.samples.size();begin+=voice_envelope_words) {
        std::uint32_t total{};
        const auto end=std::min(begin+voice_envelope_words,clip.samples.size());
        for (auto i=begin;i<end;++i) {
            const auto sample=clip.samples[i];
            if (!std::isfinite(sample)) throw std::runtime_error("Non-finite voice PCM");
            const auto value=static_cast<int>(std::lrint(std::clamp(static_cast<double>(sample),-1.0,32767.0/32768.0)*32768.0));
            total+=static_cast<std::uint32_t>(value<0 ? -value : value);
        }
        result.push_back(total/static_cast<std::uint32_t>(voice_envelope_words));
    }
    return result;
}
std::shared_ptr<const AudioClip> decode_audio(const std::filesystem::path& path) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open audio");
        const auto length = input.tellg();
        if (length <= 0 || static_cast<std::uint64_t>(length) > max_encoded) throw std::runtime_error("Audio file exceeds 64 MiB encoded budget or is empty");
        Bytes bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) throw std::runtime_error("Cannot read audio");
        auto clip = magic(bytes, 0, "OggS") ? vorbis(bytes) :
                    magic(bytes, 0, "RIFF") ? wav(bytes) : throw std::runtime_error("Unsupported audio magic; expected Ogg Vorbis or RIFF/WAVE");
        if (!std::all_of(clip.samples.begin(), clip.samples.end(), [](float value) { return std::isfinite(value); }))
            throw std::runtime_error("Audio contains non-finite PCM");
        return std::make_shared<const AudioClip>(std::move(clip));
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
}
