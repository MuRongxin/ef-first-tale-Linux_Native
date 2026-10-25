#include "text/font_faces.hpp"
#include <stdexcept>
namespace ef {
FT_Face FontFaces::open(const std::filesystem::path& path,int index) {
    FT_Face face{};
    if (FT_New_Face(library_,path.c_str(),index,&face)) throw std::runtime_error("Cannot load font face "+std::to_string(index)+": "+path.string());
    if (!FT_IS_SCALABLE(face) || FT_HAS_COLOR(face) || FT_Select_Charmap(face,FT_ENCODING_UNICODE)) {
        FT_Done_Face(face);
        throw std::runtime_error("Font requires scalable monochrome outlines and a Unicode charmap: "+path.string());
    }
    return face;
}
FontFaces::FontFaces(const Content& content,const FontSettings& settings) {
    settings.validate();
    if (FT_Init_FreeType(&library_)) throw std::runtime_error("FreeType initialization failed");
    try {
        fallback_=open(content.resolve(content.font_path()),content.font_face());
        if (!settings.file.empty()) custom_=open(settings.file,settings.face);
    } catch (...) {
        if (fallback_) FT_Done_Face(fallback_);
        FT_Done_FreeType(library_);
        throw;
    }
}
FontFaces::~FontFaces() {
    if (custom_) FT_Done_Face(custom_);
    FT_Done_Face(fallback_); FT_Done_FreeType(library_);
}
FT_Face FontFaces::for_character(char32_t cp) const {
    if (custom_ && FT_Get_Char_Index(custom_,static_cast<FT_ULong>(cp))) return custom_;
    if (FT_Get_Char_Index(fallback_,static_cast<FT_ULong>(cp))) return fallback_;
    throw std::runtime_error("Primary and fallback fonts are missing code point "+std::to_string(static_cast<unsigned>(cp)));
}
void FontFaces::set_pixels(unsigned pixels) {
    if (FT_Set_Pixel_Sizes(fallback_,0,pixels) || (custom_ && FT_Set_Pixel_Sizes(custom_,0,pixels)))
        throw std::runtime_error("Cannot set font pixel size");
}
std::string FontFaces::name() const {
    const auto face=primary();
    return std::string(face->family_name ? face->family_name : "Custom font")+" / "+(face->style_name ? face->style_name : "Regular");
}
}
