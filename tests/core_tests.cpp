#include "core/display.hpp"
#include "core/utf8.hpp"
#include "data/content.hpp"
#include "data/image.hpp"
#include "scene/scene.hpp"
#include <png.h>
#include <webp/encode.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& action, const std::string& message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection: " + message);
}
void write_json(const std::filesystem::path& path, const nlohmann::json& doc) { std::ofstream(path) << doc.dump(); }
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path() / ("test-content-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
int main() {
    try {
        using namespace ef;
        require(asset_tier(false, false) == AssetTier::x4, "normal must use 4x");
        require(asset_tier(true, false) == AssetTier::x8, "maximized must use 8x");
        require(asset_tier(false, true) == AssetTier::x8, "borderless must use 8x");
        const auto viewport = fit_viewport(1920, 1080);
        require(viewport.x == 240 && viewport.y == 0 && viewport.width == 1440 && viewport.height == 1080, "16:9 letterbox");
        const auto center = viewport.to_design(960, 540);
        require(center && center->x == 400 && center->y == 300, "physical input -> virtual center");
        require(!viewport.to_design(239, 500) && !viewport.to_design(1680, 500), "black bars reject input");
        const auto tall = fit_viewport(800, 1000);
        require(tall.y == 200 && tall.height == 600, "portrait letterbox");
        require(fit_viewport(0, 0).width == 0 && !fit_viewport(0, 0).to_design(0, 0), "minimized canvas");
        require(fit_viewport(1001, 751).width * 3 == fit_viewport(1001, 751).height * 4, "odd framebuffer aspect");
        FixedStep clock;
        require(clock.advance(1.0 / 120) == 0 && clock.advance(1.0 / 120) == 1, "fixed 60 Hz");
        require(clock.advance(1000) == 15 && clock.advance(-1) == 0, "bounded catch-up");
        require(decode_utf8("中文\\n") == U"中文\\n", "UTF-8 preserves text escapes");
        require(decode_utf8("\xf0\x9f\x8c\x80").size() == 1, "four-byte UTF-8");
        for (const auto* invalid : {"\xc0\xaf", "\xe4\xb8", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe4x\x80"})
            rejects([&] { (void)decode_utf8(invalid); }, "invalid UTF-8");

        Temporary temp;
        const std::vector<unsigned char> rgba{255, 30, 10, 255, 8, 70, 200, 128};
        png_image png{};
        png.version = PNG_IMAGE_VERSION;
        png.width = 2; png.height = 1; png.format = PNG_FORMAT_RGBA;
        require(png_image_write_to_file(&png, (temp.path / "raw.png-").c_str(), 0, rgba.data(), 0, nullptr), "write PNG fixture");
        const auto decoded = decode_image(temp.path / "raw.png-");
        require(decoded.width == 2 && decoded.height == 1 && decoded.rgba == rgba, "PNG magic sniff, RGBA and soft alpha");
        unsigned char* encoded{};
        const auto length = WebPEncodeLosslessRGBA(rgba.data(), 2, 1, 8, &encoded);
        require(length != 0, "encode WebP fixture");
        { std::ofstream file(temp.path / "raw.webp", std::ios::binary); file.write(reinterpret_cast<char*>(encoded), static_cast<std::streamsize>(length)); }
        WebPFree(encoded);
        require(decode_image(temp.path / "raw.webp").rgba == rgba, "lossless WebP alpha exactness");
        std::ofstream(temp.path / "broken.webp") << "RIFFbad";
        rejects([&] { (void)decode_image(temp.path / "broken.webp"); }, "corrupt image");
        rejects([&] { (void)decode_image(temp.path / "missing.png"); }, "missing image");

        nlohmann::json manifest = {
            {"version", 1}, {"design_size", {800, 600}}, {"development", true},
            {"entry_scene", "scene.json"}, {"font", {{"path", "font.ttf"}}},
            {"images", {{"bg/example.png", {{"design_size", {2, 1}}, {"variants", {
                {"1", {{"path", "raw.png-"}, {"pixels", {2, 1}}}},
                {"4", {{"path", "four.webp"}, {"pixels", {8, 4}}}},
                {"8", {{"path", "eight.webp"}, {"pixels", {16, 8}}}}
            }}}}}}
        };
        write_json(temp.path / "manifest.json", manifest);
        Content content(temp.path);
        require(content.candidates("bg/example.png", AssetTier::x8).front().tier == 1, "missing HD falls back to 1x");
        std::ofstream(temp.path / "four.webp").put('x');
        std::ofstream(temp.path / "eight.webp").put('x');
        require(content.candidates("bg/example.png", AssetTier::x4).front().tier == 4, "normal window never upgrades to 8x");
        require(content.candidates("bg/example.png", AssetTier::x8).front().tier == 8, "8x preferred when available");
        rejects([&] { content.validate_images(); }, "declared corrupt HD must fail validation");
        rejects([&] { (void)content.asset("unknown"); }, "unknown asset");
        for (const auto* path : {"../outside", "/etc/passwd", "dir\\file", "dir/../../file"})
            rejects([&] { (void)content.resolve(path); }, "path confinement");
        std::filesystem::create_directory_symlink(temp.path.parent_path(), temp.path / "escape");
        rejects([&] { (void)content.resolve("escape/outside"); }, "symlink escape");

        nlohmann::json scene = {{"version", 1}, {"title", "预览"}, {"layers", nlohmann::json::array({
            {{"kind", "image"}, {"asset", "bg/example.png"}, {"x", -2}},
            {{"kind", "text"}, {"text", "测试"}, {"font_size", 24}}
        })}};
        write_json(temp.path / "scene.json", scene);
        const auto parsed = Scene::load(content);
        require(parsed.layers[0].width == 2 && parsed.layers[0].height == 1, "scene uses design_size, not HD pixels");
        scene["layers"][0]["asset"] = "missing";
        write_json(temp.path / "scene.json", scene);
        rejects([&] { (void)Scene::load(content); }, "scene resource references");

        auto invalid_manifest = manifest;
        invalid_manifest["images"]["bg/example.png"]["variants"]["8"]["pixels"] = {8, 4};
        write_json(temp.path / "manifest.json", invalid_manifest);
        rejects([&] { Content invalid(temp.path); }, "unexpected 4x data in ordinary 8x variant");
        invalid_manifest = manifest;
        invalid_manifest["development"] = false;
        write_json(temp.path / "manifest.json", invalid_manifest);
        rejects([&] { Content invalid(temp.path); }, "1x must be explicitly development-only");
        invalid_manifest = manifest;
        invalid_manifest["images"] = nlohmann::json::object();
        invalid_manifest["images"]["wide"] = {
            {"design_size", {3000, 600}},
            {"variants", {{"8", {{"path", "wide.webp"}, {"pixels", {12000, 2400}}}}}}
        };
        write_json(temp.path / "manifest.json", invalid_manifest);
        Content wide(temp.path);
        require(wide.asset("wide").width == 3000, "oversized WebP 8x tree / 4x content exception");
        std::cout << "Core tests PASS: viewport, tiers, clock, UTF-8, PNG/WebP/alpha, manifest, confinement, scene\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
