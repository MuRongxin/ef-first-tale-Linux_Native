#include "core/utf8.hpp"
#include <stdexcept>

namespace ef {
std::u32string decode_utf8(std::string_view text) {
    std::u32string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        char32_t cp{};
        int continuation{};
        char32_t minimum{};
        if (first < 0x80) cp = first;
        else if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1f; continuation = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0f; continuation = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 0x07; continuation = 3; minimum = 0x10000; }
        else throw std::runtime_error("Invalid UTF-8 leading byte");
        for (int j = 0; j < continuation; ++j) {
            if (i == text.size()) throw std::runtime_error("Truncated UTF-8 sequence");
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) throw std::runtime_error("Invalid UTF-8 continuation byte");
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            throw std::runtime_error("Invalid UTF-8 code point");
        result.push_back(cp);
    }
    return result;
}
}
