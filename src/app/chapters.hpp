#pragma once
#include <array>
namespace ef {
struct ChapterEntry { const char* title; const char* script; };
inline constexpr std::array<ChapterEntry,4> chapters{{
    {"序章", "scr/000_01mugai.sc"}, {"第一章", "scr/100_01.sc"},
    {"幕间", "scr/700_01mugai.sc"}, {"第二章", "scr/200_01.sc"}}};
}
