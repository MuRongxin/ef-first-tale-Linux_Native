#include "text/font.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ef {
Font::Font(const Content& content) {
    if (FT_Init_FreeType(&library_)) throw std::runtime_error("FreeType initialization failed");
    if (FT_New_Face(library_, content.resolve(content.font_path()).c_str(), content.font_face(), &face_)) {
        FT_Done_FreeType(library_);
        throw std::runtime_error("Cannot load content font: " + content.font_path());
    }
    if (FT_Select_Charmap(face_, FT_ENCODING_UNICODE)) {
        FT_Done_Face(face_);
        FT_Done_FreeType(library_);
        throw std::runtime_error("Content font has no Unicode charmap");
    }
}
Font::~Font() { FT_Done_Face(face_); FT_Done_FreeType(library_); }
const Font::Glyph& Font::glyph(char32_t cp) {
    const auto key = std::make_pair(pixels_, cp);
    if (const auto it = glyphs_.find(key); it != glyphs_.end()) return it->second;
    const auto index = FT_Get_Char_Index(face_, static_cast<FT_ULong>(cp));
    if (!index) throw std::runtime_error("Content font is missing code point " + std::to_string(static_cast<unsigned>(cp)));
    if (FT_Load_Glyph(face_, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) throw std::runtime_error("FreeType glyph rasterization failed");
    const auto& bitmap = face_->glyph->bitmap;
    Glyph result;
    result.width = static_cast<float>(bitmap.width);
    result.height = static_cast<float>(bitmap.rows);
    result.left = static_cast<float>(face_->glyph->bitmap_left);
    result.top = static_cast<float>(face_->glyph->bitmap_top);
    result.advance = static_cast<float>(face_->glyph->advance.x) / 64.0f;
    result.index = index;
    if (bitmap.width && bitmap.rows) {
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) throw std::runtime_error("Font must provide grayscale outline glyphs");
        Image image{static_cast<int>(bitmap.width), static_cast<int>(bitmap.rows), {}};
        image.rgba.resize(static_cast<std::size_t>(bitmap.width) * bitmap.rows * 4, 255);
        for (unsigned y = 0; y < bitmap.rows; ++y) {
            const auto row = bitmap.pitch >= 0 ? y : bitmap.rows - 1 - y;
            const auto* source = bitmap.buffer + static_cast<std::size_t>(row) * static_cast<unsigned>(std::abs(bitmap.pitch));
            for (unsigned x = 0; x < bitmap.width; ++x)
                image.rgba[(static_cast<std::size_t>(y) * bitmap.width + x) * 4 + 3] = source[x];
        }
        result.texture = Texture(image);
    }
    return glyphs_.emplace(key, std::move(result)).first->second;
}
void Font::draw(Renderer& renderer, const Layer& layer, float display_scale) {
    if (display_scale <= 0) return;
    const auto requested = static_cast<unsigned>(std::clamp(std::lround(layer.font_size * display_scale), 1L, 2048L));
    if (glyphs_.size() > 2048) glyphs_.clear();
    if (pixels_ != requested) {
        if (FT_Set_Pixel_Sizes(face_, 0, requested)) throw std::runtime_error("Cannot set font pixel size");
        pixels_ = requested;
    }
    const float line_height = static_cast<float>(face_->size->metrics.height) / 64.0f / display_scale;
    const float ascender = static_cast<float>(face_->size->metrics.ascender) / 64.0f / display_scale;
    float pen_x = layer.x;
    float baseline = layer.y + ascender;
    FT_UInt previous{};
    for (const auto cp : decode_utf8(layer.text)) {
        if (cp == U'\r') continue;
        if (cp == U'\n') { pen_x = layer.x; baseline += line_height; previous = 0; continue; }
        const auto& item = glyph(cp);
        if (previous && FT_HAS_KERNING(face_)) {
            FT_Vector kern{};
            FT_Get_Kerning(face_, previous, item.index, FT_KERNING_DEFAULT, &kern);
            pen_x += static_cast<float>(kern.x) / 64.0f / display_scale;
        }
        const float advance = item.advance / display_scale;
        if (pen_x > layer.x && pen_x + advance > layer.x + layer.width) { pen_x = layer.x; baseline += line_height; }
        if (item.texture.id())
            renderer.quad(item.texture, pen_x + item.left / display_scale, baseline - item.top / display_scale,
                          item.width / display_scale, item.height / display_scale, layer.color);
        pen_x += advance;
        previous = item.index;
    }
}
void Font::validate(const Scene& scene) const {
    for (const auto& layer : scene.layers) {
        if (layer.kind != LayerKind::text) continue;
        for (const auto cp : decode_utf8(layer.text)) {
            if (cp == U'\r' || cp == U'\n') continue;
            if (!FT_Get_Char_Index(face_, static_cast<FT_ULong>(cp)))
                throw std::runtime_error("Content font is missing code point " + std::to_string(static_cast<unsigned>(cp)));
        }
    }
}
}
