#pragma once
#include "data/content.hpp"
#include "settings/font_settings.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
namespace ef {
// Owns a primary outline face and the packaged CJK fallback. No graphics context needed.
class FontFaces {
public:
    FontFaces(const Content& content,const FontSettings& settings={});
    ~FontFaces();
    FontFaces(const FontFaces&)=delete;
    FontFaces& operator=(const FontFaces&)=delete;
    FT_Face primary() const { return custom_ ? custom_ : fallback_; }
    FT_Face for_character(char32_t cp) const;
    void set_pixels(unsigned pixels);
    std::string name() const;
    long face_count() const { return primary()->num_faces; }
    bool customized() const { return custom_!=nullptr; }
private:
    FT_Face open(const std::filesystem::path& path,int index);
    FT_Library library_{};
    FT_Face fallback_{},custom_{};
};
}
