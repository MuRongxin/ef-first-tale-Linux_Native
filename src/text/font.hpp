#pragma once
#include "render/renderer.hpp"
#include "text/font_faces.hpp"
#include <map>
namespace ef {
struct TextDrawResult { Point cursor{}; float line_height{}; bool overflow{}; };
class Font {
public:
    explicit Font(const Content& content,const FontSettings& settings={});
    void style(Scene& scene) const;
    std::string name() const { return faces_.name(); }
    long face_count() const { return faces_.face_count(); }
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
        FT_Face face{};
    };
    const Glyph& glyph(char32_t codepoint);
    FontFaces faces_;
    float scale_{1};
    unsigned pixels_{};
    std::map<std::pair<unsigned, char32_t>, Glyph> glyphs_;
};
}
