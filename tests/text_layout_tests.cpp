#include "text/layout.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected layout rejection");
}
std::u32string glyphs(const ef::TextLayout& layout) {
    std::u32string result;
    for (const auto& glyph : layout.glyphs) result += glyph.codepoint;
    return result;
}
bool near(double a, double b) { return std::abs(a-b) < 1e-9; }
}
int main() {
    try {
        using namespace ef;
        const GlyphAdvance mono = [](char32_t) { return 10.0; };
        const TextLayoutOptions box{30,100,20,0};
        auto plain = layout_text("中文😀AB",mono,box);
        require(plain.glyphs.size()==5 && plain.line_count==2 && !plain.overflow,"Unicode glyphs and automatic wrapping");
        require(plain.glyphs[2].codepoint==U'😀' && plain.glyphs[2].source_index==2 && plain.glyphs[2].x==20,"supplementary code point and source index");
        require(plain.glyphs[3].line==1 && plain.glyphs[3].x==0 && plain.glyphs[3].y==20,"line coordinates");
        require(plain.measured_width==30 && plain.measured_height==40,"measured line boxes");
        auto lines = layout_text("甲\r\n乙\n\n丙\r",mono,box);
        require(lines.line_count==5 && glyphs(lines)==U"甲乙丙","explicit CRLF, LF, blank and trailing lines");
        require(lines.glyphs[1].source_index==3 && lines.glyphs[1].line==1 && lines.glyphs[2].source_index==6 && lines.glyphs[2].line==3,"original source indices retain line controls");
        auto empty = layout_text("",mono,box);
        require(empty.line_count==0 && empty.glyphs.empty() && !empty.overflow,"empty text has no line boxes");
        auto blank = layout_text("\n",mono,box);
        require(blank.line_count==2 && blank.measured_height==40,"a newline preserves both empty lines");

        auto closing = layout_text("甲乙，丙",mono,{20,100,20,0});
        require(closing.line_count==3 && closing.glyphs[1].line==1 && closing.glyphs[2].line==1 && !closing.kinsoku_violation,"closing punctuation follows preceding character");
        auto opening = layout_text("甲（乙丙",mono,{20,100,20,0});
        require(opening.glyphs[1].line==1 && opening.glyphs[2].line==1 && !opening.kinsoku_violation,"opening punctuation moves with following character");
        auto narrow = layout_text("甲，乙",mono,{10,100,20,0});
        require(glyphs(narrow)==U"甲，乙" && narrow.line_count==2 && narrow.overflow && !narrow.kinsoku_violation,"unbreakable punctuation group overflows without dropping text");
        auto forced = layout_text("甲\n，乙",mono,box);
        require(forced.kinsoku_violation && glyphs(forced)==U"甲，乙","explicit newline remains authoritative");
        auto group = layout_text("（【《甲，。！？）】》乙",mono,{10,1000,20,0});
        require(glyphs(group)==U"（【《甲，。！？）】》乙" && group.overflow && !group.kinsoku_violation,"long punctuation group cannot loop or lose glyphs");
        auto oversized = layout_text("中A",[](char32_t cp) { return cp==U'中' ? 100.0 : 10.0; },box);
        require(oversized.overflow && oversized.glyphs.size()==2 && oversized.glyphs[1].line==1,"oversized first glyph always advances");
        auto spaces = layout_text("  A ",mono,{20,100,20,0});
        require(glyphs(spaces)==U"  A " && spaces.glyphs.size()==4,"whitespace is never trimmed");

        auto spaced = layout_text("ABC",mono,{34,100,20,2});
        require(spaced.line_count==1 && spaced.glyphs[1].x==12 && spaced.measured_width==34,"letter spacing occurs only between glyphs");
        auto tight = layout_text("ABC",mono,{26,100,20,-2});
        require(tight.line_count==1 && tight.glyphs[2].x==16 && tight.measured_width==26,"negative spacing with nonnegative steps");
        auto fractional = layout_text("ABC",[](char32_t) { return 0.1; },{0.3,0.2,0.2,0});
        require(fractional.line_count==1 && !fractional.overflow,"scaled metric rounding does not add a spurious line");
        auto tiny = layout_text("ABC",[](char32_t) { return 1e-15; },{1e-15,1,0.1,0});
        require(tiny.line_count==3,"rounding tolerance scales with tiny bounds");
        auto zeros = layout_text("ABC",[](char32_t) { return 0.0; },{1,20,20,0});
        require(zeros.glyphs.size()==3 && zeros.line_count==1 && !zeros.overflow,"zero advance cannot cause infinite wrapping");
        auto height = layout_text("ABCD",mono,{20,20,20,0});
        require(height.glyphs.size()==4 && height.line_count==2 && height.overflow,"height overflow preserves complete layout");
        auto zero_height = layout_text("A",mono,{20,0,20,0});
        require(zero_height.overflow && zero_height.glyphs.size()==1,"zero-height box reports overflow");
        std::size_t measured{};
        auto stable = layout_text("甲乙，丙丁",[&](char32_t) { ++measured; return 10.0; },{20,100,20,0});
        require(measured==5,"advance callback runs once per drawable code point");
        std::size_t count{};
        for (const auto& glyph : stable.glyphs) if (glyph.source_index<2) {
            ++count;
            if (glyph.source_index==1) require(glyph.line==1,"prelayout keeps character stable before punctuation is revealed");
        }
        require(count==2,"source code-point filter drives incremental reveal without relayout");

        for (const auto* text : {"\xc0\xaf","\xed\xa0\x80","\xf0\x9f"}) rejects([&] { (void)layout_text(text,mono,box); });
        rejects([&] { (void)layout_text(std::string("a\0b",3),mono,box); });
        rejects([&] { (void)layout_text("A",{},box); });
        for (const auto invalid : {TextLayoutOptions{0,100,20,0},TextLayoutOptions{-1,100,20,0},TextLayoutOptions{30,-1,20,0},TextLayoutOptions{30,100,0,0},TextLayoutOptions{30,100,-1,0}})
            rejects([&] { (void)layout_text("A",mono,invalid); });
        for (const double bad : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            rejects([&] { (void)layout_text("A",mono,{bad,100,20,0}); });
            rejects([&] { (void)layout_text("A",mono,{30,bad,20,0}); });
            rejects([&] { (void)layout_text("A",mono,{30,100,bad,0}); });
            rejects([&] { (void)layout_text("A",mono,{30,100,20,bad}); });
            rejects([&] { (void)layout_text("A",[&](char32_t) { return bad; },box); });
        }
        rejects([&] { (void)layout_text("A",[](char32_t) { return -1.0; },box); });
        rejects([&] { (void)layout_text("A",mono,{30,100,20,-11}); });
        rejects([&] { (void)layout_text("A",[](char32_t) { return std::numeric_limits<double>::max(); },{30,100,20,std::numeric_limits<double>::max()}); });
        rejects([&] { (void)layout_text("A\nB",mono,{30,100,std::numeric_limits<double>::max(),0}); });
        rejects([&] { (void)layout_text(std::string(65537,'A'),mono,box); });
        rejects([&] { (void)layout_text(std::string(1024*1024+1,'A'),mono,box); });
        auto limit = layout_text(std::string(65536,'A'),mono,{10,100,20,0});
        require(limit.glyphs.size()==65536 && limit.line_count==65536 && limit.overflow,"maximum-size narrow layout terminates with all glyphs");
        require(near(tight.measured_width,26),"layout output remains finite");
        std::cout << "Text layout tests PASS: UTF-8, full-text reveal indices, wrapping, kinsoku, spacing, overflow preservation and strict bounds\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
