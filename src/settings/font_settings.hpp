#pragma once
#include <filesystem>
#include <string>
#include <optional>
#include "text/appearance.hpp"
namespace ef {
struct FontSettings {
    std::filesystem::path file;
    int face{};
    float scale{1};
    std::optional<TextAppearance> appearance{};
    bool operator==(const FontSettings&) const = default;
    void validate() const;
};
std::filesystem::path default_font_config();
FontSettings load_font_settings(const std::filesystem::path& path);
void save_font_settings(const std::filesystem::path& path,const FontSettings& settings);
}
