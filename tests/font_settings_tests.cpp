#include "settings/font_settings.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("Font settings assertion failed"); }
template<class F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected invalid font settings rejection"); }
}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("ef-font-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto path=dir/"font.json";
        require(ef::load_font_settings(path)==ef::FontSettings{});
        ef::FontSettings settings{dir/"中文 font.ttc",2,1.25f};
        ef::save_font_settings(path,settings);
        require(ef::load_font_settings(path)==settings);
        auto invalid=settings; invalid.scale=std::numeric_limits<float>::quiet_NaN();
        rejects([&] { ef::save_font_settings(path,invalid); });
        require(ef::load_font_settings(path)==settings);
        invalid=settings; invalid.file="relative.ttf"; rejects([&] { invalid.validate(); });
        invalid=settings; invalid.face=-1; rejects([&] { invalid.validate(); });
        invalid=settings; invalid.scale=2; rejects([&] { invalid.validate(); });
        std::ofstream(path) << R"({"version":2})";
        rejects([&] { (void)ef::load_font_settings(path); });
        std::ofstream(path) << "invalid json";
        rejects([&] { (void)ef::load_font_settings(path); });
        ef::save_font_settings(path,{});
        require(ef::load_font_settings(path)==ef::FontSettings{});
        require(std::distance(std::filesystem::directory_iterator(dir),std::filesystem::directory_iterator{})==1);
        std::filesystem::remove_all(dir);
        std::cout << "Font settings PASS: persistence, invalid values, atomic replacement and reset\n";
    } catch (const std::exception& e) { std::filesystem::remove_all(dir); std::cerr << e.what() << '\n'; return 1; }
}
