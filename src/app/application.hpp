#pragma once
#include <filesystem>
#include <string>
#include <optional>
namespace ef {
struct Options {
    std::filesystem::path content{"content"}, screenshot;
    int width{}, height{}, frames{}; // 0 = adaptive 4:3 tier from the display
    int movie_texture_cap{3200}; // max movie texture side; quality-first, lower saves upload
    bool fullscreen{}, maximized{}, validate{}, smoke{};
    std::string script;
    char variant{'e'};
    bool text_preview{}, audit_scripts{}, audit_resources{};
    int trace_messages{}, start_line{};
    bool silent{};
    std::filesystem::path load_save, save_file;
    int text_speed{40};
    std::filesystem::path font_config, window_config;
    std::optional<std::filesystem::path> font_file;
    std::optional<int> font_face, font_percent;
    bool default_font{};
    std::optional<int> text_outline,text_shadow,text_baseline;
};
int run(const Options& options);
}
