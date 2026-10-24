#include "scene/scene.hpp"
#include "core/utf8.hpp"
#include <cmath>
#include <stdexcept>
namespace ef {
namespace {
float number(const nlohmann::json& value, const char* key, float fallback) {
    const float result = value.value(key, fallback);
    if (!std::isfinite(result) || std::abs(result) > 100000) throw std::runtime_error(std::string("Invalid scene number: ") + key);
    return result;
}
Color color(const nlohmann::json& value) {
    const auto result = value.get<Color>();
    for (const float component : result) {
        if (!std::isfinite(component) || component < 0 || component > 1) throw std::runtime_error("Color must be RGBA in [0,1]");
    }
    return result;
}
}
Scene Scene::load(const Content& content, const std::string& path) {
    const auto doc = content.read_json(path.empty() ? content.entry_scene() : path);
    if (doc.at("version") != 1) throw std::runtime_error("Unsupported scene version");
    Scene scene;
    scene.title = doc.at("title").get<std::string>();
    (void)decode_utf8(scene.title);
    if (doc.contains("background")) scene.background = color(doc.at("background"));
    if (!doc.at("layers").is_array()) throw std::runtime_error("Scene layers must be an array");
    for (const auto& item : doc.at("layers")) {
        Layer layer;
        layer.slot = item.value("slot", std::string{});
        const auto kind = item.at("kind").get<std::string>();
        layer.x = number(item, "x", 0);
        layer.y = number(item, "y", 0);
        if (item.contains("color")) layer.color = color(item.at("color"));
        if (kind == "image") {
            layer.kind = LayerKind::image;
            layer.asset = item.at("asset").get<std::string>();
            const auto& source = content.asset(layer.asset);
            layer.width = static_cast<float>(source.width);
            layer.height = static_cast<float>(source.height);
        } else if (kind == "rect") {
            layer.kind = LayerKind::rectangle;
            layer.width = number(item, "width", 0);
            layer.height = number(item, "height", 0);
            if (layer.width <= 0 || layer.height <= 0) throw std::runtime_error("Rectangle must have positive dimensions");
        } else if (kind == "text") {
            layer.kind = LayerKind::text;
            layer.text = item.at("text").get<std::string>();
            (void)decode_utf8(layer.text);
            layer.font_size = number(item, "font_size", 24);
            layer.width = number(item, "width", 800 - layer.x);
            layer.height = number(item, "height", 0);
            if (layer.font_size <= 0 || layer.font_size > 256 || layer.width <= 0) throw std::runtime_error("Invalid text size");
        } else throw std::runtime_error("Unknown scene layer: " + kind);
        scene.layers.push_back(std::move(layer));
    }
    return scene;
}
}
