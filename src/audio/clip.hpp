#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace ef {
struct AudioClip {
    int sample_rate{}, channels{};
    std::vector<float> samples; // Native-rate, interleaved mono/stereo PCM.
    [[nodiscard]] std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
    [[nodiscard]] double duration() const { return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0; }
};
// Main/control thread only. Decodes standalone package paths, never archives.
[[nodiscard]] std::shared_ptr<const AudioClip> decode_audio(const std::filesystem::path& path);
}
