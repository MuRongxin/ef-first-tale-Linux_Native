#include "settings/font_settings.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
namespace ef {
void FontSettings::validate() const {
    if (appearance) appearance->validate();
    if (face<0 || face>65535 || (file.empty() && face!=0)) throw std::runtime_error("Invalid font face index");
    if (!std::isfinite(scale) || scale<0.75f || scale>1.5f) throw std::runtime_error("Font scale must be between 0.75 and 1.5");
    if (!file.empty() && !file.is_absolute()) throw std::runtime_error("Custom font path must be absolute");
}
std::filesystem::path default_font_config() {
    if (const auto* config=std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        return std::filesystem::path(config)/"ef-native/font.json";
    if (const auto* home=std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home)/".config/ef-native/font.json";
    throw std::runtime_error("Cannot locate user font configuration; use --font-config FILE");
}
FontSettings load_font_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    if (std::filesystem::file_size(path)>65536) throw std::runtime_error("Font configuration is too large");
    std::ifstream input(path);
    const auto json=nlohmann::json::parse(input);
    if (!json.is_object() || json.at("version")!=1) throw std::runtime_error("Unsupported font configuration version");
    FontSettings result;
    result.file=json.at("file").get<std::string>();
    result.face=json.at("face").get<int>();
    result.scale=json.at("scale").get<float>();
    if (json.contains("appearance") && !json.at("appearance").is_null()) result.appearance=json.at("appearance").get<TextAppearance>();
    result.validate();
    return result;
}
void save_font_settings(const std::filesystem::path& path,const FontSettings& settings) {
    settings.validate();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // Exclusive temporary file keeps concurrent instances from truncating each other's writes.
    auto pattern=path.string()+".tmp.XXXXXX";
    const int fd=mkstemp(pattern.data());
    if (fd<0) throw std::runtime_error("Cannot create font configuration: "+path.string());
    close(fd);
    try {
        std::ofstream output(pattern);
        nlohmann::json json{{"version",1},{"file",settings.file.string()},{"face",settings.face},{"scale",settings.scale}};
        json["appearance"]=settings.appearance ? nlohmann::json(*settings.appearance) : nlohmann::json(nullptr);
        output << json.dump(2) << '\n';
        output.close();
        if (!output) throw std::runtime_error("Cannot write font configuration");
        std::filesystem::rename(pattern,path);
    } catch (...) { std::filesystem::remove(pattern); throw; }
}
}
