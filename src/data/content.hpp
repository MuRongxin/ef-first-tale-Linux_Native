#pragma once
#include "core/display.hpp"
#include "data/image.hpp"
#include "data/animation.hpp"
#include <array>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ef {
struct ImageVariant {
    int tier{}, width{}, height{};
    std::string path;
    int derived_width{}, derived_height{};
    std::array<int,4> crop{}; // source pixels; only set for runtime-derived thumbnails
};
struct Asset {
    int width{}, height{}; // design_size, never derived from the selected tier
    std::map<int, ImageVariant> variants;
    std::string derived_source;
    std::array<int,4> derived_crop{}; // source design coordinates
};
struct Resource {
    std::string id, kind, name, tag, path, source_sha256;
};
enum class ResourceStatus { resolved, missing, ambiguous };
struct ResourceMatch {
    ResourceStatus status{ResourceStatus::missing};
    std::vector<std::string> candidates;
};
std::string resource_name(const std::string& name);
class Content {
public:
    explicit Content(const std::filesystem::path& root);
    [[nodiscard]] std::filesystem::path resolve(const std::string& relative) const;
    [[nodiscard]] const Asset& asset(const std::string& id) const;
    [[nodiscard]] std::vector<ImageVariant> candidates(const std::string& id, AssetTier tier) const;
    [[nodiscard]] Image decode_variant(const ImageVariant&,int max_width,int max_height) const;
    [[nodiscard]] nlohmann::json read_json(const std::string& relative) const;
    [[nodiscard]] const std::map<std::string, Asset>& assets() const { return assets_; }
    [[nodiscard]] const std::map<std::string, Resource>& resources() const { return resources_; }
    [[nodiscard]] bool has_resource_catalog() const { return has_resource_catalog_; }
    [[nodiscard]] ResourceMatch lookup_resource(const std::string& kind, const std::string& name, bool r18 = false) const;
    [[nodiscard]] const Resource& resource(const std::string& kind, const std::string& name, bool r18 = false) const;
    [[nodiscard]] const std::string& script_index() const { return script_index_; }
    [[nodiscard]] const std::string& entry_scene() const { return entry_scene_; }
    [[nodiscard]] const std::string& font_path() const { return font_path_; }
    [[nodiscard]] int font_face() const { return font_face_; }
    [[nodiscard]] const AnimationSpec* animation(const std::string& id) const {
        const auto it=animations_.find(id);return it==animations_.end() ? nullptr : &it->second;
    }
    void validate_images() const;
private:
    std::filesystem::path root_;
    std::map<std::string, Asset> assets_;
    std::map<std::string, AnimationSpec> animations_;
    std::map<std::string, Resource> resources_;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> resource_names_;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> resource_aliases_;
    std::string script_index_{"scripts/index.json"};
    bool has_resource_catalog_{};
    std::string entry_scene_, font_path_;
    int font_face_{};
};
}
