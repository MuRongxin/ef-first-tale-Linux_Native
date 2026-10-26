#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace ef {
class AudioOutput;
// One-shot movie playback for the strict script host. Decodes an imported
// content/video asset (H.264/MPEG, optional AAC) with FFmpeg, scales each frame to
// the native destination rectangle and streams audio into the shared SDL output.
// Control thread only: update() must be called once per rendered frame.
class MoviePlayer {
public:
    MoviePlayer(const std::filesystem::path& path, int width, int height, AudioOutput* audio);
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;
    // wall_seconds is used only when no audio clock is available (silent mode or no audio track).
    void update(double wall_seconds);
    void skip();
    [[nodiscard]] bool finished() const;
    [[nodiscard]] const std::vector<std::uint8_t>& rgba() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] double media_time() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
