#include "text/font.hpp"
#include "core/utf8.hpp"
#include "text/layout.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include FT_STROKER_H
#include FT_GLYPH_H

namespace ef {
namespace {
Texture mask_texture(const FT_Bitmap& bitmap) {
    if (!bitmap.width || !bitmap.rows) return {};
    if (bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY) throw std::runtime_error("Font requires grayscale outline glyphs");
    Image image{static_cast<int>(bitmap.width),static_cast<int>(bitmap.rows),{}};
    image.rgba.resize(static_cast<std::size_t>(bitmap.width)*bitmap.rows*4,255);
    for (unsigned y=0; y<bitmap.rows; ++y) {
        const auto row=bitmap.pitch>=0 ? y : bitmap.rows-1-y;
        const auto* source=bitmap.buffer+static_cast<std::size_t>(row)*static_cast<unsigned>(std::abs(bitmap.pitch));
        for (unsigned x=0; x<bitmap.width; ++x) image.rgba[(static_cast<std::size_t>(y)*bitmap.width+x)*4+3]=source[x];
    }
    return Texture(image);
}
struct Stroke {
    FT_Stroker stroker{};
    FT_Glyph glyph{};
    ~Stroke() { if (glyph) FT_Done_Glyph(glyph); if (stroker) FT_Stroker_Done(stroker); }
};
}
Font::Font(const Content& content,const FontSettings& settings) : faces_(content,settings),scale_(settings.scale),appearance_(settings.appearance) {}
Font::~Font() = default;
void Font::style(Scene& scene) const {
    if (appearance_) for (auto& layer : scene.layers) if (layer.kind==LayerKind::text) layer.appearance=*appearance_;
    if (!faces_.customized() && scale_==1) return;
    std::map<char32_t,float> advances;
    for (auto& layer : scene.layers) if (layer.kind==LayerKind::text) {
        layer.font_size*=scale_;
        if (layer.line_height>0) layer.line_height=std::max(layer.line_height*scale_,layer.font_size*1.25f);
        layer.letter_spacing*=scale_;
    }
    Layer* named_body=nullptr;
    float name_gap{};
    for (auto& body : scene.layers) if (body.slot=="message_body" && !body.flow_body)
        for (const auto& name : scene.layers) if (name.slot=="message_name" && body.y>name.y) {
            named_body=&body; name_gap=body.y-name.y;
        }
    // Keep complete dialogue pages visible even when a user chooses a wider face.
    // A shared reduction preserves body/name alignment and stable typewriter wrapping.
    float reduction=1;
    for (const auto& layer : scene.layers) {
        if (layer.kind!=LayerKind::text || layer.text.empty() || !layer.kinsoku) continue;
        auto fits=[&](float factor) {
            const auto measure=[&](char32_t cp) {
                if (const auto found=advances.find(cp); found!=advances.end()) return found->second*layer.font_size*factor;
                auto face=faces_.for_character(cp);
                if (FT_Load_Glyph(face,FT_Get_Char_Index(face,static_cast<FT_ULong>(cp)),FT_LOAD_NO_SCALE|FT_LOAD_NO_HINTING))
                    throw std::runtime_error("Cannot measure custom font");
                const auto advance=static_cast<float>(face->glyph->metrics.horiAdvance)/face->units_per_EM;
                advances.emplace(cp,advance); return advance*layer.font_size*factor;
            };
            const float name_room=&layer==named_body ? std::max(0.0f,layer.line_height*factor-name_gap) : 0;
            return !layout_text(layer.text,measure,{layer.width,layer.height+layer.upward_room-name_room,
                layer.line_height*factor,layer.letter_spacing*factor}).overflow;
        };
        float factor=reduction;
        while (!fits(factor) && factor>0.4f) factor=std::max(0.4f,factor-0.025f);
        if (!fits(factor)) throw std::runtime_error("Custom font cannot fit message region");
        reduction=std::min(reduction,factor);
    }
    for (auto& layer : scene.layers) if (layer.kind==LayerKind::text) {
        layer.font_size*=reduction; layer.line_height*=reduction; layer.letter_spacing*=reduction;
    }
    if (named_body) {
        const float extra=std::max(0.0f,named_body->line_height-name_gap);
        named_body->y+=extra; named_body->height-=extra;
    }
}
const Font::Glyph& Font::glyph(char32_t cp) {
    const auto key = std::make_tuple(pixels_,stroke_,cp);
    if (const auto it = glyphs_.find(key); it != glyphs_.end()) return it->second;
    const auto face_=faces_.for_character(cp);
    const auto index = FT_Get_Char_Index(face_, static_cast<FT_ULong>(cp));
    if (!index) throw std::runtime_error("Content font is missing code point " + std::to_string(static_cast<unsigned>(cp)));
    if (FT_Load_Glyph(face_, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_BITMAP)) throw std::runtime_error("FreeType glyph rasterization failed");
    const auto& bitmap = face_->glyph->bitmap;
    Glyph result;
    result.width = static_cast<float>(bitmap.width);
    result.height = static_cast<float>(bitmap.rows);
    result.left = static_cast<float>(face_->glyph->bitmap_left);
    result.top = static_cast<float>(face_->glyph->bitmap_top);
    result.advance = static_cast<float>(face_->glyph->advance.x) / 64.0f;
    result.index = index; result.face=face_;
    result.texture=mask_texture(bitmap);
    if (stroke_ && result.texture.id()) {
        Stroke outline;
        if (FT_Load_Glyph(face_,index,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP) ||
            FT_Get_Glyph(face_->glyph,&outline.glyph) || FT_Stroker_New(face_->glyph->library,&outline.stroker))
            throw std::runtime_error("Cannot prepare font outline");
        FT_Stroker_Set(outline.stroker,static_cast<FT_Fixed>(stroke_),FT_STROKER_LINECAP_ROUND,FT_STROKER_LINEJOIN_ROUND,0);
        if (FT_Glyph_StrokeBorder(&outline.glyph,outline.stroker,false,true) ||
            FT_Glyph_To_Bitmap(&outline.glyph,FT_RENDER_MODE_NORMAL,nullptr,true))
            throw std::runtime_error("Cannot rasterize font outline");
        const auto stroked=reinterpret_cast<FT_BitmapGlyph>(outline.glyph);
        result.outline=mask_texture(stroked->bitmap);
        result.outline_width=static_cast<float>(stroked->bitmap.width); result.outline_height=static_cast<float>(stroked->bitmap.rows);
        result.outline_left=static_cast<float>(stroked->left); result.outline_top=static_cast<float>(stroked->top);
    }
    // Measure wrapping in font units so window scaling cannot change line breaks.
    if (face_->units_per_EM<=0 || FT_Load_Glyph(face_,index,FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING))
        throw std::runtime_error("FreeType font-unit measurement failed");
    result.em_advance=static_cast<float>(face_->glyph->metrics.horiAdvance)/face_->units_per_EM;
    return glyphs_.emplace(key, std::move(result)).first->second;
}
void Font::fit_messages(Scene& scene,float display_scale) {
    stroke_=0;
    for (auto& body : scene.layers) {
        if (!body.flow_body) continue;
        const auto requested=static_cast<unsigned>(std::clamp(std::lround(body.font_size*display_scale),1L,2048L));
        if (pixels_!=requested) {
            faces_.set_pixels(requested);
            pixels_=requested;
        }
        const auto advance=[&](char32_t cp) { return glyph(cp).em_advance*body.font_size; };
        const TextLayoutOptions bounds{body.width,body.height,body.line_height,body.letter_spacing};
        if (layout_text(body.text,advance,bounds).overflow) throw std::runtime_error("Full-screen message page exceeds its text region");
        for (auto& name : scene.layers) if (name.flow_anchor) {
            if (*name.flow_anchor>body.text.size()) throw std::runtime_error("Speaker anchor outside full-screen text");
            const auto prefix=body.text.substr(0,*name.flow_anchor);
            const auto layout=layout_text(prefix,advance,bounds);
            const auto row=layout.line_count ? layout.line_count-1 : 0;
            const float offset=static_cast<float>(row)*body.line_height;
            const float bottom=name.y+name.height;
            name.y=body.y+offset; name.height=std::max(1.0f,bottom-name.y);
        }
    }
    for (auto& body : scene.layers) {
        if (body.upward_room<=0) continue;
        const auto requested=static_cast<unsigned>(std::clamp(std::lround(body.font_size*display_scale),1L,2048L));
        if (pixels_!=requested) {
            faces_.set_pixels(requested);
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
    if (!std::isfinite(display_scale) || display_scale <= 0) return {};
    layer.appearance.validate();
    stroke_=static_cast<unsigned>(std::lround(layer.appearance.outline_width*display_scale*64));
    const auto requested = static_cast<unsigned>(std::clamp(std::lround(layer.font_size * display_scale), 1L, 2048L));
    if (glyphs_.size() > 2048) glyphs_.clear();
    if (pixels_ != requested) {
        faces_.set_pixels(requested);
        pixels_ = requested;
    }
    const auto face_=faces_.primary();
    const float line_height = layer.line_height>0 ? layer.line_height : static_cast<float>(face_->size->metrics.height) / 64.0f / display_scale;
    const float ascender = static_cast<float>(face_->size->metrics.ascender) / 64.0f / display_scale;
    struct Placement { const Glyph* glyph; float x,y; };
    std::vector<Placement> placements;
    auto paint=[&]() {
        const auto& effect=layer.appearance;
        auto shadow_color=effect.shadow_color,outline_color=effect.outline_color;
        shadow_color[3]*=layer.color[3]; outline_color[3]*=layer.color[3];
        // Whole-layer passes keep adjacent outlines and shadows behind every foreground glyph.
        if ((effect.shadow_x!=0 || effect.shadow_y!=0) && shadow_color[3]>0)
            for (const auto& p : placements) {
                const auto& g=*p.glyph;
                if (g.texture.id()) renderer.quad(g.texture,p.x+g.left/display_scale+effect.shadow_x,
                    p.y-g.top/display_scale+effect.shadow_y,g.width/display_scale,g.height/display_scale,shadow_color);
            }
        if (stroke_ && outline_color[3]>0) for (const auto& p : placements) {
            const auto& g=*p.glyph;
            if (g.outline.id()) renderer.quad(g.outline,p.x+g.outline_left/display_scale,p.y-g.outline_top/display_scale,
                g.outline_width/display_scale,g.outline_height/display_scale,outline_color);
        }
        for (const auto& p : placements) {
            const auto& g=*p.glyph;
            if (g.texture.id()) renderer.quad(g.texture,p.x+g.left/display_scale,p.y-g.top/display_scale,
                g.width/display_scale,g.height/display_scale,layer.color);
        }
    };
    if (layer.kinsoku) {
        const auto layout=layout_text(layer.text,[&](char32_t cp) { return glyph(cp).em_advance*layer.font_size; },
            {layer.width,layer.height>0 ? layer.height+layer.upward_room : 100000.0,line_height,layer.letter_spacing});
        if (layout.overflow) throw std::runtime_error("Text exceeds message region: " + layer.slot);
        for (const auto& placement : layout.glyphs) {
            if (placement.source_index>=layer.visible_characters) continue;
            const auto& item=glyph(placement.codepoint);
            placements.push_back({&item,layer.x+static_cast<float>(placement.x),
                layer.y+static_cast<float>(placement.y)+ascender+layer.appearance.baseline_offset});
        }
        Point cursor{layer.x,layer.y};
        if (!layout.glyphs.empty()) {
            const auto& last=layout.glyphs.back();
            cursor={layer.x+static_cast<float>(last.x+last.advance)+layer.letter_spacing,layer.y+static_cast<float>(last.y)};
        }
        if (!layer.text.empty() && (layer.text.back()=='\n' || layer.text.back()=='\r'))
            cursor={layer.x,layer.y+static_cast<float>(layout.line_count-1)*line_height};
        paint();
        return {cursor,line_height,layout.overflow};
    }
    float pen_x = layer.x;
    float baseline = layer.y + ascender;
    FT_UInt previous{};
    FT_Face previous_face{};
    std::size_t source_index{};
    for (const auto cp : decode_utf8(layer.text)) {
        const auto character=source_index++;
        if (cp == U'\r') continue;
        if (cp == U'\n') { pen_x = layer.x; baseline += line_height; previous = 0; continue; }
        const auto& item = glyph(cp);
        if (previous && previous_face==item.face && FT_HAS_KERNING(item.face)) {
            FT_Vector kern{};
            FT_Get_Kerning(item.face, previous, item.index, FT_KERNING_DEFAULT, &kern);
            pen_x += static_cast<float>(kern.x) / 64.0f / display_scale;
        }
        const float advance = item.advance / display_scale;
        if (pen_x > layer.x && pen_x + advance > layer.x + layer.width) { pen_x = layer.x; baseline += line_height; }
        if (character<layer.visible_characters) placements.push_back({&item,pen_x,baseline+layer.appearance.baseline_offset});
        pen_x += advance;
        previous = item.index; previous_face=item.face;
    }
    paint();
    return {{pen_x,baseline-ascender},line_height,false};
}
void Font::validate(const Scene& scene) const {
    for (const auto& layer : scene.layers) {
        if (layer.kind != LayerKind::text) continue;
        for (const auto cp : decode_utf8(layer.text)) {
            if (cp == U'\r' || cp == U'\n') continue;
            (void)faces_.for_character(cp);
        }
    }
}
}
