#include "settings/window_settings.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path() /
        ("test-window-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        const auto config = temp.path / "ef-native/window.json";
        require(load_window_settings(config).empty(), "Missing window configuration must be empty");
        const WindowSettings expected{1200, 900, true};
        expected.validate();
        save_window_settings(config, expected);
        require(load_window_settings(config) == expected, "Window geometry round trip");
        require(std::filesystem::exists(config), "Window configuration was not written");
        require(load_window_settings(temp.path / "ef-native/window.json") == expected, "Parent directories");
        rejects([&] { save_window_settings(config, WindowSettings{319, 900, false}); });
        rejects([&] { save_window_settings(config, WindowSettings{1200, 239, false}); });
        rejects([&] { save_window_settings(config, WindowSettings{16385, 900, false}); });
        rejects([&] { save_window_settings(config, WindowSettings{1200, 16385, false}); });

        const auto write = [&](const std::string& name, const std::string& text) {
            const auto file = temp.path / name;
            std::ofstream(file) << text;
            return file;
        };
        rejects([&] { (void)load_window_settings(write("bad-json.json", "not json")); });
        rejects([&] { (void)load_window_settings(write("bad-version.json", R"({"version":2,"width":1200,"height":900})")); });
        rejects([&] { (void)load_window_settings(write("bad-size.json", R"({"version":1,"width":0,"height":900})")); });
        rejects([&] { (void)load_window_settings(write("too-large.json", std::string(20000, 'x'))); });
        const auto no_maximized = write("no-maximized.json", R"({"version":1,"width":1200,"height":900})");
        require(load_window_settings(no_maximized) == WindowSettings{1200, 900, false}, "maximized defaults to false");
        const auto extra = write("extra.json", R"({"version":1,"width":800,"height":600,"maximized":false,"future":1})");
        require(load_window_settings(extra) == WindowSettings{800, 600, false}, "Unknown fields are ignored");

        // Atomic replacement must not leave temporary files behind.
        std::size_t entries = 0;
        for (const auto& entry : std::filesystem::directory_iterator(config.parent_path())) {
            (void)entry;
            ++entries;
        }
        require(entries == 1, "Window configuration left temporary files");

        unsetenv("XDG_CONFIG_HOME");
        setenv("XDG_CONFIG_HOME", temp.path.c_str(), 1);
        require(default_window_config() == temp.path / "ef-native/window.json", "XDG window config path");
        unsetenv("XDG_CONFIG_HOME");
        setenv("HOME", temp.path.c_str(), 1);
        require(default_window_config() == temp.path / ".config/ef-native/window.json", "HOME window config fallback");
        std::cout << "Window settings tests PASS: validation, round trip, atomic write, corrupt fallback and paths\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
