#pragma once
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace ef {
struct TextLayoutOptions {
    double width{}, height{}, line_height{}, letter_spacing{};
};
struct GlyphPlacement {
    char32_t codepoint{};
    // Original UTF-8 source's code-point index, including CR/LF. It is independent
    // of UTF-8 byte count, so reveal can filter a complete, stable layout by index.
    std::size_t source_index{}, line{};
    double x{}, y{}, advance{}; // y is the line-box top; caller supplies font baseline
};
struct TextLayout {
    std::vector<GlyphPlacement> glyphs;
    std::size_t line_count{};
    double measured_width{}, measured_height{};
    bool overflow{};
    // An unavoidable prohibited start/end (e.g. after an explicit newline).
    bool kinsoku_violation{};
};
using GlyphAdvance = std::function<double(char32_t)>;
// Layout the entire string once, then draw placements whose source_index is below
// the revealed code-point count. Wrapping therefore never depends on reveal state.
// Text is preserved, including whitespace; CRLF and standalone CR/LF make new lines.
// If a punctuation group cannot fit without breaking kinsoku, keep it together and
// report overflow. Explicit newlines are authoritative even if kinsoku cannot hold.
// width > 0, height >= 0, line_height > 0; every value/advance must be finite.
// Negative spacing is supported when every glyph's advance + spacing stays >= 0.
// Limits: 1 MiB UTF-8 source, 65,536 code points. Callback runs once per drawable CP.
TextLayout layout_text(std::string_view text, const GlyphAdvance& advance, const TextLayoutOptions& options);
}
