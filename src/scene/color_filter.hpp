#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
namespace ef {
// Native HSV command uses a fixed-point saturation/brightness matrix.
// The hue field is retained in scene state; the shipped pixel routine does not use it.
inline std::array<int,4> color_filter_coefficients(const std::array<int,3>& hsv) {
    const int saturation=(std::clamp(hsv[1],-100,100)+100)*65536/100;
    const int inverse=65536-saturation;
    const int brightness=std::clamp(hsv[2],-100,100)*256;
    const auto weight=[&](int n) {
        // Native IMUL wraps to 32 bits before the arithmetic right shift.
        const auto product=static_cast<std::uint32_t>(static_cast<std::int64_t>(inverse)*n);
        return (std::bit_cast<std::int32_t>(product)>>16)+brightness;
    };
    return {weight(19595),weight(38470),weight(7471),saturation};
}
}
