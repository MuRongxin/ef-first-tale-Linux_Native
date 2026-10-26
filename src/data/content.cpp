#include "data/content.hpp"
#include "data/image.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>

namespace ef {
std::string resource_name(const std::string& name) {
    std::string result = name;
    for (char& ch : result) {
        if (ch == '\\') ch = '/';
        else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    }
    if (result.empty() || result.find('\0') != std::string::npos) throw std::runtime_error("Invalid resource name");
    std::size_t start{};
    while (true) {
        const auto end = result.find('/', start);
        const auto component = result.substr(start, end == std::string::npos ? end : end - start);
        if (component.empty() || component == "." || component == "..") throw std::runtime_error("Invalid resource name: " + name);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
namespace {
std::string image_alias(std::string name) {
    for (const std::string suffix : {".png.tmp", ".png-"}) {
        if (name.ends_with(suffix)) return name.replace(name.size() - suffix.size(), suffix.size(), ".png");
    }
    return name;
}
}
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
    script_index_ = doc.value("script_index", "scripts/index.json");
    (void)resolve(script_index_);
    if (doc.contains("resources")) {
        has_resource_catalog_ = true;
        const auto catalog = read_json(doc.at("resources").get<std::string>());
        if (catalog.at("version") != 1 || !catalog.at("entries").is_object())
            throw std::runtime_error("Unsupported resource catalog");
        const std::map<std::string, std::set<std::string>> tags{
            {"image", {"bg", "st", "sys", "r18_bg", "r18_st", "r18_sys"}},
            {"script", {"scr", "r18_scr"}}, {"voice", {"voice", "r18_voice"}},
            {"bgm", {"bgm"}}, {"se", {"se"}}, {"video", {"mov"}}};
        for (const auto& [id, spec] : catalog.at("entries").items()) {
            Resource item{id, spec.at("kind").get<std::string>(), spec.at("name").get<std::string>(),
                          spec.at("tag").get<std::string>(), spec.value("path", ""), spec.at("source_sha256").get<std::string>()};
            const auto accepted = tags.find(item.kind);
            if (accepted == tags.end() || !accepted->second.contains(item.tag) || id != item.tag + '/' + item.name)
                throw std::runtime_error("Invalid resource kind/tag/ID: " + id);
            const auto normalized = resource_name(item.name);
            if (item.source_sha256.size() != 64 || !std::all_of(item.source_sha256.begin(), item.source_sha256.end(), [](char ch) {
                    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
                })) throw std::runtime_error("Invalid resource SHA-256: " + id);
            if (item.kind == "image") {
                if (!assets_.contains(id) || doc.at("images").at(id).at("source_sha256") != item.source_sha256)
                    throw std::runtime_error("Image/catalog mismatch: " + id);
            } else (void)resolve(item.path);
            resource_names_[{item.kind, normalized}].push_back(id);
            resource_aliases_[{item.kind, item.kind == "image" ? image_alias(normalized) : normalized}].push_back(id);
            resources_.emplace(id, std::move(item));
        }
    }
}
ResourceMatch Content::lookup_resource(const std::string& kind, const std::string& name, bool r18) const {
    ResourceMatch result;
    const auto normalized = resource_name(name);
    const auto select = [&](const std::vector<std::string>& ids) {
        ResourceMatch selected;
        std::set<std::string> replaced;
        if (r18) {
            for (const auto& id : ids) {
                const auto& tag = resources_.at(id).tag;
                if (tag.starts_with("r18_")) replaced.insert(tag.substr(4));
            }
        }
        std::set<std::string> hashes;
        for (const auto& id : ids) {
            const auto& item = resources_.at(id);
            if ((!r18 && item.tag.starts_with("r18_")) || replaced.contains(item.tag)) continue;
            selected.candidates.push_back(id);
            hashes.insert(item.source_sha256);
        }
        if (!selected.candidates.empty()) selected.status = hashes.size() == 1 ? ResourceStatus::resolved : ResourceStatus::ambiguous;
        // Native stage-stand loader (0x4df99c -> 0x5081a0 -> 0x5068e0) probes the st
        // archive reader first (slot 3 / 0x5e0898 at 0x506cb0) and only falls back to the
        // bg reader (slot 2 / 0x5e0628 at 0x506d2e) when st.paz lacks the file. Every
        // cross-archive bg/st duplicate in this content set is a tachie image, so resolve
        // such a pair to st instead of failing closed. Other ambiguities stay unresolved.
        if (selected.status == ResourceStatus::ambiguous) {
            std::vector<std::string> st_ids;
            bool only_bg_st = true;
            for (const auto& id : selected.candidates) {
                const auto& item = resources_.at(id);
                only_bg_st = only_bg_st && item.kind == "image" && (item.tag == "bg" || item.tag == "st");
                if (item.tag == "st") st_ids.push_back(id);
            }
            if (only_bg_st && st_ids.size() == 1) {
                selected.candidates = st_ids;
                selected.status = ResourceStatus::resolved;
            }
        }
        return selected;
    };
    if (const auto exact = resource_names_.find({kind, normalized}); exact != resource_names_.end()) {
        result = select(exact->second);
        if (!result.candidates.empty()) return result;
    }
    if (const auto alias = resource_aliases_.find({kind, kind == "image" ? image_alias(normalized) : normalized}); alias != resource_aliases_.end())
        result = select(alias->second);
    return result;
}
const Resource& Content::resource(const std::string& kind, const std::string& name, bool r18) const {
    const auto match = lookup_resource(kind, name, r18);
    if (match.status == ResourceStatus::missing) throw std::runtime_error("Missing resource: " + kind + '/' + name);
    if (match.status == ResourceStatus::ambiguous) {
        std::string message = "Ambiguous resource: " + kind + '/' + name + "; candidates:";
        for (const auto& id : match.candidates) message += ' ' + id;
        throw std::runtime_error(message);
    }
    return resources_.at(match.candidates.front());
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
