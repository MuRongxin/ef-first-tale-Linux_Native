#pragma once
#include <string>
#include <string_view>
namespace ef {
// Strict decoding: scripts must already have been converted by the importer.
std::u32string decode_utf8(std::string_view text);
}
