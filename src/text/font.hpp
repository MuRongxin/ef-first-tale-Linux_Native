#pragma once
#include "render/renderer.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
namespace ef {
struct TextDrawResult { Point cursor{}; float line_height{}; bool overflow{}; };
class Font {
public:
    explicit Font(const Content& content);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    void fit_messages(Scene& scene,float display_scale);
    TextDrawResult draw(Renderer& renderer, const Layer& layer, float display_scale);
    void validate(const Scene& scene) const;
private:
    struct Glyph {
        Texture texture;
        float width{}, height{}, left{}, top{}, advance{}, em_advance{};
        FT_UInt index{};
    };
    const Glyph& glyph(char32_t codepoint);
    FT_Library library_{};
    FT_Face face_{};
    unsigned pixels_{};
    std::map<std::pair<unsigned, char32_t>, Glyph> glyphs_;
};
}
