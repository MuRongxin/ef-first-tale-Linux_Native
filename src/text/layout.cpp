#include "text/layout.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ef {
namespace {
constexpr std::size_t maximum_bytes = 1024 * 1024;
constexpr std::size_t maximum_codepoints = 65536;
constexpr std::u32string_view prohibited_start = U"，。！？、：；）】》」』〕〉〗〙〛］｝’”％‰,.;:!?)]}%";
constexpr std::u32string_view prohibited_end = U"（【《「『〔〈〖〘〚［｛‘“([{";
bool cannot_start(char32_t cp) { return prohibited_start.find(cp) != std::u32string_view::npos; }
bool cannot_end(char32_t cp) { return prohibited_end.find(cp) != std::u32string_view::npos; }
void finite(double value, const char* label) {
    if (!std::isfinite(value)) throw std::runtime_error(std::string("Non-finite text layout ")+label);
}
struct InputGlyph { char32_t codepoint; std::size_t source_index; double advance; };
struct Paragraph { std::size_t begin, end; };
}
TextLayout layout_text(std::string_view text, const GlyphAdvance& advance, const TextLayoutOptions& options) {
    finite(options.width,"width"); finite(options.height,"height");
    finite(options.line_height,"line height"); finite(options.letter_spacing,"letter spacing");
    if (options.width <= 0 || options.height < 0 || options.line_height <= 0)
        throw std::runtime_error("Invalid text layout bounds or line height");
    if (!advance) throw std::runtime_error("Missing glyph advance callback");
    if (text.size() > maximum_bytes) throw std::runtime_error("Text layout exceeds 1 MiB source budget");
    const auto codepoints = decode_utf8(text);
    if (codepoints.size() > maximum_codepoints) throw std::runtime_error("Text layout exceeds 65,536 code-point budget");
    if (codepoints.find(U'\0') != std::u32string::npos) throw std::runtime_error("NUL in layout text");
    TextLayout result;
    if (codepoints.empty()) return result;
    std::vector<InputGlyph> input;
    std::vector<Paragraph> paragraphs;
    input.reserve(codepoints.size());
    std::size_t paragraph_start{};
    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        const auto cp = codepoints[i];
        if (cp == U'\r' || cp == U'\n') {
            paragraphs.push_back({paragraph_start,input.size()});
            paragraph_start = input.size();
            if (cp == U'\r' && i+1 < codepoints.size() && codepoints[i+1] == U'\n') ++i;
            continue;
        }
        const double width = advance(cp);
        finite(width,"glyph advance");
        if (width < 0) throw std::runtime_error("Negative glyph advance");
        const double step = width+options.letter_spacing;
        finite(step,"glyph step");
        if (step < 0) throw std::runtime_error("Negative glyph step after letter spacing");
        input.push_back({cp,i,width});
    }
    paragraphs.push_back({paragraph_start,input.size()});
    // Widths are monotone because glyph steps were validated. Prefix sums and
    // predecessor/successor tables eliminate unbounded punctuation backtracking.
    std::vector<long double> prefix(input.size()+1,0);
    std::vector<std::size_t> previous_break(input.size()+1,0), next_break(input.size()+1,input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        prefix[i+1] = prefix[i]+static_cast<long double>(input[i].advance)+options.letter_spacing;
        if (!std::isfinite(prefix[i+1])) throw std::runtime_error("Text layout width accumulation overflow");
        const bool legal = i+1 == input.size() || (!cannot_end(input[i].codepoint) && !cannot_start(input[i+1].codepoint));
        previous_break[i+1] = legal ? i+1 : previous_break[i];
    }
    for (std::size_t i = input.size(); i > 0; --i) {
        const bool legal = i == input.size() || (!cannot_end(input[i-1].codepoint) && !cannot_start(input[i].codepoint));
        next_break[i] = legal ? i : next_break[i+1];
    }
    result.glyphs.reserve(input.size());
    // Advances can arrive from scaled floating-point font metrics. Permit only a
    // relative rounding tolerance at the right/bottom edge, never a fixed pixel gap.
    const long double width_tolerance = static_cast<long double>(options.width)*1e-12L;
    const long double height_tolerance = static_cast<long double>(options.height)*1e-12L;
    for (const auto& paragraph : paragraphs) {
        std::size_t begin = paragraph.begin;
        if (begin == paragraph.end) { ++result.line_count; continue; }
        while (begin < paragraph.end) {
            const long double limit = prefix[begin]+options.width+options.letter_spacing+width_tolerance;
            const auto upper = std::upper_bound(prefix.begin()+static_cast<std::ptrdiff_t>(begin+1),
                prefix.begin()+static_cast<std::ptrdiff_t>(paragraph.end+1),limit);
            auto end = static_cast<std::size_t>(upper-prefix.begin())-1;
            end = std::max(begin+1,end); // a single oversized glyph still makes progress
            if (end < paragraph.end) {
                if (previous_break[end] > begin) end = previous_break[end];
                else end = std::min(paragraph.end,next_break[end+1]);
            }
            if (end <= begin || end > paragraph.end) throw std::runtime_error("Text layout failed to advance");
            if (cannot_start(input[begin].codepoint) || cannot_end(input[end-1].codepoint)) result.kinsoku_violation = true;
            const double y = static_cast<double>(result.line_count)*options.line_height;
            finite(y,"line position");
            double pen{};
            for (std::size_t i = begin; i < end; ++i) {
                const auto& glyph = input[i];
                result.glyphs.push_back({glyph.codepoint,glyph.source_index,result.line_count,pen,y,glyph.advance});
                pen += glyph.advance;
                finite(pen,"line width");
                if (i+1 < end) { pen += options.letter_spacing; finite(pen,"glyph position"); }
            }
            result.measured_width = std::max(result.measured_width,pen);
            ++result.line_count;
            begin = end;
        }
    }
    result.measured_height = static_cast<double>(result.line_count)*options.line_height;
    finite(result.measured_height,"measured height");
    result.overflow = static_cast<long double>(result.measured_width) > options.width+width_tolerance ||
        static_cast<long double>(result.measured_height) > options.height+height_tolerance;
    return result;
}
}
