#include "settings/window_settings.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace ef {
void WindowSettings::validate() const {
    if (width < 320 || width > 16384 || height < 240 || height > 16384)
        throw std::runtime_error("Window geometry is outside the supported range");
}
std::filesystem::path default_window_config() {
    if (const auto* config = std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        return std::filesystem::path(config) / "ef-native/window.json";
    if (const auto* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home) / ".config/ef-native/window.json";
    throw std::runtime_error("Cannot locate user window configuration; use --window-config FILE");
}
WindowSettings load_window_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    if (std::filesystem::file_size(path) > 16384) throw std::runtime_error("Window configuration is too large");
    std::ifstream input(path);
    const auto json = nlohmann::json::parse(input);
    if (!json.is_object() || json.at("version") != 1) throw std::runtime_error("Unsupported window configuration version");
    WindowSettings result;
    result.width = json.at("width").get<int>();
    result.height = json.at("height").get<int>();
    result.maximized = json.value("maximized", false);
    result.validate();
    return result;
}
void save_window_settings(const std::filesystem::path& path, const WindowSettings& settings) {
    settings.validate();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // Atomic replace keeps concurrent instances from truncating each other's writes.
    auto pattern = path.string() + ".tmp.XXXXXX";
    const int fd = mkstemp(pattern.data());
    if (fd < 0) throw std::runtime_error("Cannot create window configuration: " + path.string());
    close(fd);
    try {
        std::ofstream output(pattern);
        const nlohmann::json json{{"version", 1}, {"width", settings.width},
            {"height", settings.height}, {"maximized", settings.maximized}};
        output << json.dump(2) << '\n';
        output.close();
        if (!output) throw std::runtime_error("Cannot write window configuration");
        std::filesystem::rename(pattern, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(pattern, ignored);
        throw;
    }
}
}
