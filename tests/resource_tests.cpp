#include "data/content.hpp"
#include "script/script.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
void write(const std::filesystem::path& path, const nlohmann::json& value) { std::ofstream(path) << value.dump(); }
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path() / ("test-resources-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        const std::string same(64, 'a'), different(64, 'b');
        nlohmann::json manifest{{"version", 1}, {"design_size", {800, 600}}, {"development", true},
            {"entry_scene", "scene.json"}, {"font", {{"path", "font.ttf"}}}, {"images", nlohmann::json::object()},
            {"resources", "catalog.json"}, {"script_index", "imported.scripts.json"}};
        nlohmann::json catalog{{"version", 1}, {"entries", nlohmann::json::object()}};
        const auto add = [&](const std::string& tag, const std::string& name, const std::string& kind, const std::string& digest) {
            const auto id = tag + '/' + name;
            catalog["entries"][id] = {{"tag", tag}, {"name", name}, {"kind", kind}, {"source_sha256", digest}};
            if (kind == "image") manifest["images"][id] = {{"source_sha256", digest}, {"design_size", {2, 1}},
                {"variants", {{"1", {{"path", "image.png"}, {"pixels", {2, 1}}}}}}};
            else catalog["entries"][id]["path"] = kind == "script" ? "script.sc" : "sound.ogg";
        };
        add("bg", "Test.png", "image", same);
        add("st", "test.png", "image", same);
        add("sys", "button.png-", "image", same);
        add("r18_sys", "button.png", "image", different);
        add("r18_bg", "only.png", "image", same);
        add("sys", "icon.png", "image", same);
        add("sys", "icon.png-", "image", different);
        add("voice", "voice-key", "voice", same);
        add("scr", "test.sc", "script", same);
        write(temp.path / "manifest.json", manifest);
        write(temp.path / "catalog.json", catalog);
        write(temp.path / "imported.scripts.json", {{"version", 1}, {"encoding", "utf-8"},
            {"scripts", {{"scr/test.sc", {{"path", "script.sc"}}}}}});
        std::ofstream(temp.path / "image.png") << "file-presence fixture; decoder tested separately";
        std::ofstream(temp.path / "sound.ogg") << "transport fixture";
        std::ofstream(temp.path / "script.sc") << ".message 10 voice-key #名字 对话里fake.png\n.message 20   旁白\n[j].stage * TEST.PNG 0 0\n";
        Content content(temp.path);
        require(content.resource("image", "TEST.PNG").id == "bg/Test.png", "same-content aliases are deterministic");
        require(content.resource("image", "button.png.tmp").id == "sys/button.png-", "archive extension artifact");
        require(content.resource("image", "BUTTON.PNG", true).id == "r18_sys/button.png", "patch replaces its own family");
        require(content.lookup_resource("image", "only.png").status == ResourceStatus::missing, "patch-only content gated");
        require(content.resource("voice", "VOICE-KEY").path == "sound.ogg", "extensionless voice IDs");
        require(content.resource("image", "icon.png").id == "sys/icon.png" && content.resource("image", "icon.png-").id == "sys/icon.png-",
            "exact archive names take precedence over fake-extension compatibility aliases");
        require(content.lookup_resource("image", "icon.png.tmp").status == ResourceStatus::ambiguous, "ambiguous fallback aliases rejected");
        for (const auto* name : {"../outside", "/outside", "a//b", "a/./b"}) rejects([&] { (void)resource_name(name); });
        auto report = ScriptLibrary(content).audit_resources();
        require(report.at("references") == 2 && report.at("missing") == 0 && report.at("ambiguous") == 0 && report.at("unavailable_files").empty(),
            "audit covers message voices and both timing variants, excludes dialogue filenames");
        std::ofstream(temp.path / "script.sc", std::ios::app) << ".transition 0 missing.png 10\n";
        report = ScriptLibrary(content).audit_resources();
        require(report.at("missing") == 1 && report.at("issues")[0]["line"] == 4, "missing resource has physical source line");
        std::ofstream(temp.path / "script.sc", std::ios::app) << ".effect Snow\n";
        report = ScriptLibrary(content).audit_resources();
        require(report.at("missing")==4 && report.at("references")==6,"snow's three implicit sprite dependencies are audited");
        // Restore the fixture so later checks retain their original missing-file scope.
        std::ofstream(temp.path / "script.sc") << ".message 10 voice-key #名字 对话里fake.png\n.message 20   旁白\n[j]:.stage * TEST.PNG 0 0\n.transition 0 missing.png 10\n";
        std::filesystem::remove(temp.path / "sound.ogg");
        require(ScriptLibrary(content).audit_resources().at("unavailable_files").size() == 1, "declared but missing package file");

        catalog["entries"]["st/test.png"]["source_sha256"] = different;
        manifest["images"]["st/test.png"]["source_sha256"] = different;
        write(temp.path / "catalog.json", catalog);
        write(temp.path / "manifest.json", manifest);
        Content preferred(temp.path);
        require(preferred.lookup_resource("image", "test.png").status == ResourceStatus::resolved &&
            preferred.resource("image", "test.png").id == "st/test.png",
            "native stage-stand loader resolves bg/st duplicates to st");
        catalog["entries"]["voice/voice-key"]["path"] = "../outside";
        write(temp.path / "catalog.json", catalog);
        rejects([&] { Content invalid(temp.path); });
        std::cout << "Resource tests PASS: names, aliases, patch gating, native bg/st priority, catalog confinement and script reference audit\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
