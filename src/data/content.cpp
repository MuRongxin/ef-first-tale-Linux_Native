#include "data/content.hpp"
#include "data/image.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ef {
Content::Content(const std::filesystem::path& root) : root_(std::filesystem::canonical(root)) {
    const auto doc = read_json("manifest.json");
    if (doc.at("version") != 1 || doc.at("design_size") != nlohmann::json::array({800, 600}))
        throw std::runtime_error("Unsupported content version or logical canvas (expected v1, 800x600)");
    const bool development = doc.value("development", false);
    entry_scene_ = doc.at("entry_scene").get<std::string>();
    font_path_ = doc.at("font").at("path").get<std::string>();
    font_face_ = doc.at("font").value("face", 0);
    if (font_face_ < 0) throw std::runtime_error("Font face index must be nonnegative");
    (void)resolve(entry_scene_);
    (void)resolve(font_path_);
    const auto& images = doc.at("images");
    if (!images.is_object()) throw std::runtime_error("images must be an object");
    for (const auto& [id, spec] : images.items()) {
        if (id.empty()) throw std::runtime_error("Empty asset ID");
        const auto& size = spec.at("design_size");
        if (!size.is_array() || size.size() != 2 || !size[0].is_number_integer() || !size[1].is_number_integer())
            throw std::runtime_error(id + ": invalid design_size");
        Asset asset{size[0].get<int>(), size[1].get<int>(), {}};
        if (asset.width <= 0 || asset.height <= 0 || asset.width > 65536 || asset.height > 65536)
            throw std::runtime_error(id + ": design_size out of range");
        if (!spec.at("variants").is_object()) throw std::runtime_error(id + ": variants must be an object");
        for (const auto& [tier, variant] : spec.at("variants").items()) {
            if (tier != "4" && tier != "8" && !(tier == "1" && development))
                throw std::runtime_error(id + ": invalid asset tier; 1x requires development=true");
            const int value = std::stoi(tier);
            const auto& pixels = variant.at("pixels");
            if (!pixels.is_array() || pixels.size() != 2 || !pixels[0].is_number_integer() || !pixels[1].is_number_integer())
                throw std::runtime_error(id + ": invalid pixels");
            ImageVariant item{value, pixels[0].get<int>(), pixels[1].get<int>(), variant.at("path").get<std::string>()};
            const bool exact = item.width == asset.width * value && item.height == asset.height * value;
            // Remote pipeline writes 4x into the 8x tree only when 8x exceeds WebP's limit.
            const bool webp_fallback = value == 8 && (asset.width * 8 > 16383 || asset.height * 8 > 16383) &&
                item.width == asset.width * 4 && item.height == asset.height * 4;
            if (!exact && !webp_fallback) throw std::runtime_error(id + ": pixel dimensions violate the HD contract");
            (void)resolve(item.path);
            asset.variants.emplace(value, std::move(item));
        }
        if (asset.variants.empty()) throw std::runtime_error(id + ": no image variants");
        assets_.emplace(id, std::move(asset));
    }
}
std::filesystem::path Content::resolve(const std::string& relative) const {
    const std::filesystem::path path(relative);
    if (path.empty() || path.is_absolute() || relative.find('\\') != std::string::npos || relative.find('\0') != std::string::npos)
        throw std::runtime_error("Content path must be relative: " + relative);
    for (const auto& component : path) {
        if (component == "..") throw std::runtime_error("Parent traversal in content path: " + relative);
    }
    const auto resolved = std::filesystem::weakly_canonical(root_ / path);
    const auto inside = resolved.lexically_relative(root_);
    if (inside.empty() || *inside.begin() == "..") throw std::runtime_error("Content path escapes its root: " + relative);
    return resolved;
}
nlohmann::json Content::read_json(const std::string& relative) const {
    const auto path = resolve(relative);
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open " + path.string() + "; run tools/prepare_content.py first");
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024) throw std::runtime_error("JSON exceeds 16 MiB: " + relative);
    try { return nlohmann::json::parse(input); }
    catch (const std::exception& e) { throw std::runtime_error(relative + ": " + e.what()); }
}
const Asset& Content::asset(const std::string& id) const {
    const auto it = assets_.find(id);
    if (it == assets_.end()) throw std::runtime_error("Unknown asset ID: " + id);
    return it->second;
}
std::vector<ImageVariant> Content::candidates(const std::string& id, AssetTier tier) const {
    const auto& source = asset(id);
    std::vector<ImageVariant> result;
    for (const int level : {8, 4, 1}) {
        if (level > static_cast<int>(tier)) continue;
        const auto it = source.variants.find(level);
        if (it != source.variants.end() && std::filesystem::is_regular_file(resolve(it->second.path))) result.push_back(it->second);
    }
    return result;
}
void Content::validate_images() const {
    for (const auto& [id, source] : assets_) {
        for (const auto& [tier, variant] : source.variants) {
            const auto image = decode_image(resolve(variant.path));
            if (image.width != variant.width || image.height != variant.height)
                throw std::runtime_error(id + ": decoded dimensions differ from manifest for tier " + std::to_string(tier));
            std::cout << "validated " << id << " tier=" << tier << " pixels=" << image.width << 'x' << image.height << '\n';
        }
    }
}
}
