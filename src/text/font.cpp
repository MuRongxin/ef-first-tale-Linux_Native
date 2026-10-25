#include "text/font.hpp"
#include "core/utf8.hpp"
#include "text/layout.hpp"
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
    // Measure wrapping in font units so window scaling cannot change line breaks.
    if (face_->units_per_EM<=0 || FT_Load_Glyph(face_,index,FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING))
        throw std::runtime_error("FreeType font-unit measurement failed");
    result.em_advance=static_cast<float>(face_->glyph->metrics.horiAdvance)/face_->units_per_EM;
    return glyphs_.emplace(key, std::move(result)).first->second;
}
void Font::fit_messages(Scene& scene,float display_scale) {
    for (auto& body : scene.layers) {
        if (body.upward_room<=0) continue;
        const auto requested=static_cast<unsigned>(std::clamp(std::lround(body.font_size*display_scale),1L,2048L));
        if (pixels_!=requested) {
            if (FT_Set_Pixel_Sizes(face_,0,requested)) throw std::runtime_error("Cannot set font pixel size");
            pixels_=requested;
        }
        const auto layout=layout_text(body.text,[&](char32_t cp) { return glyph(cp).em_advance*body.font_size; },
            {body.width,body.height+body.upward_room,body.line_height,body.letter_spacing});
        if (layout.overflow) throw std::runtime_error("Text exceeds expandable message region");
        std::size_t visible_lines{};
        for (const auto& item : layout.glyphs) if (item.source_index<body.visible_characters) visible_lines=std::max(visible_lines,item.line+1);
        if (!body.text.empty() && (body.text.back()=='\n' || body.text.back()=='\r') &&
            body.visible_characters>=decode_utf8(body.text).size()) visible_lines=std::max(visible_lines,layout.line_count);
        const float needed=static_cast<float>(visible_lines)*body.line_height;
        const float shift=std::clamp(needed-body.height,0.0f,body.upward_room);
        if (shift>0) {
            for (auto& layer : scene.layers) if (layer.slot=="message_background" || layer.slot=="message_name" || layer.slot=="message_body") layer.y-=shift;
            body.height+=shift; body.upward_room-=shift;
        }
    }
}
TextDrawResult Font::draw(Renderer& renderer, const Layer& layer, float display_scale) {
    if (display_scale <= 0) return {};
    const auto requested = static_cast<unsigned>(std::clamp(std::lround(layer.font_size * display_scale), 1L, 2048L));
    if (glyphs_.size() > 2048) glyphs_.clear();
    if (pixels_ != requested) {
        if (FT_Set_Pixel_Sizes(face_, 0, requested)) throw std::runtime_error("Cannot set font pixel size");
        pixels_ = requested;
    }
    const float line_height = layer.line_height>0 ? layer.line_height : static_cast<float>(face_->size->metrics.height) / 64.0f / display_scale;
    const float ascender = static_cast<float>(face_->size->metrics.ascender) / 64.0f / display_scale;
    if (layer.kinsoku) {
        const auto layout=layout_text(layer.text,[&](char32_t cp) { return glyph(cp).em_advance*layer.font_size; },
            {layer.width,layer.height>0 ? layer.height+layer.upward_room : 100000.0,line_height,layer.letter_spacing});
        if (layout.overflow) throw std::runtime_error("Text exceeds message region: " + layer.slot);
        for (const auto& placement : layout.glyphs) {
            if (placement.source_index>=layer.visible_characters) continue;
            const auto& item=glyph(placement.codepoint);
            if (item.texture.id()) renderer.quad(item.texture,
                layer.x+static_cast<float>(placement.x)+item.left/display_scale,
                layer.y+static_cast<float>(placement.y)+ascender-item.top/display_scale,
                item.width/display_scale,item.height/display_scale,layer.color);
        }
        Point cursor{layer.x,layer.y};
        if (!layout.glyphs.empty()) {
            const auto& last=layout.glyphs.back();
            cursor={layer.x+static_cast<float>(last.x+last.advance)+layer.letter_spacing,layer.y+static_cast<float>(last.y)};
        }
        if (!layer.text.empty() && (layer.text.back()=='\n' || layer.text.back()=='\r'))
            cursor={layer.x,layer.y+static_cast<float>(layout.line_count-1)*line_height};
        return {cursor,line_height,layout.overflow};
    }
    float pen_x = layer.x;
    float baseline = layer.y + ascender;
    FT_UInt previous{};
    std::size_t source_index{};
    for (const auto cp : decode_utf8(layer.text)) {
        const auto character=source_index++;
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
        if (character<layer.visible_characters && item.texture.id())
            renderer.quad(item.texture, pen_x + item.left / display_scale, baseline - item.top / display_scale,
                          item.width / display_scale, item.height / display_scale, layer.color);
        pen_x += advance;
        previous = item.index;
    }
    return {{pen_x,baseline-ascender},line_height,false};
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
