#pragma once
#include <filesystem>

namespace ef {
// Persisted windowed geometry. Fullscreen is transient and never stored.
struct WindowSettings {
    int width{}, height{};
    bool maximized{};
    [[nodiscard]] bool empty() const { return width == 0 && height == 0; }
    bool operator==(const WindowSettings&) const = default;
    void validate() const;
};
[[nodiscard]] std::filesystem::path default_window_config();
// Missing file returns an empty setting; malformed content throws.
[[nodiscard]] WindowSettings load_window_settings(const std::filesystem::path& path);
void save_window_settings(const std::filesystem::path& path, const WindowSettings& settings);
}
