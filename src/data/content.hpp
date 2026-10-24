#pragma once
#include "core/display.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ef {
struct ImageVariant {
    int tier{}, width{}, height{};
    std::string path;
};
struct Asset {
    int width{}, height{}; // design_size, never derived from the selected tier
    std::map<int, ImageVariant> variants;
};
class Content {
public:
    explicit Content(const std::filesystem::path& root);
    [[nodiscard]] std::filesystem::path resolve(const std::string& relative) const;
    [[nodiscard]] const Asset& asset(const std::string& id) const;
    [[nodiscard]] std::vector<ImageVariant> candidates(const std::string& id, AssetTier tier) const;
    [[nodiscard]] nlohmann::json read_json(const std::string& relative) const;
    [[nodiscard]] const std::map<std::string, Asset>& assets() const { return assets_; }
    [[nodiscard]] const std::string& entry_scene() const { return entry_scene_; }
    [[nodiscard]] const std::string& font_path() const { return font_path_; }
    [[nodiscard]] int font_face() const { return font_face_; }
    void validate_images() const;
private:
    std::filesystem::path root_;
    std::map<std::string, Asset> assets_;
    std::string entry_scene_, font_path_;
    int font_face_{};
};
}
