#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace ef {
inline bool supported_transition(int type) { return type==0 || type==1 || type==2 || type==14 || type==16 || type==17; }
// Native expansion cases finish strictly AFTER the canvas edge.
inline int transition_completion(int type) {
    switch (type) {
    case 0:return 256;
    case 1:return 512;
    case 2:return 400;
    case 14:case 17:return 801;
    case 16:return 601;
    default:throw std::runtime_error("Unimplemented transition type");
    }
}
inline std::array<int,4> transition_region(int type,int step) {
    switch (type) {
    case 14: { const int w=std::clamp(step,0,800);return {0,0,w,w*600/800}; }
    case 16:return {0,0,800,std::clamp(step,0,600)};
    case 17:return {0,0,std::clamp(step,0,800),600};
    default:throw std::runtime_error("Transition has no expansion rectangle");
    }
}
inline std::array<int,4> zoom_transition_region(int step) {
    const int index=step<200 ? std::clamp(step,0,199) : std::clamp(399-step,0,199);
    // Reconstructs all 200 observed lookup values, including their decimal rounding.
    const double t=static_cast<double>(index)/199;
    const double value=1+99*t*t;
    const double digits=std::pow(10.0,9-std::floor(std::log10(value)));
    const double scale=std::round(value*digits)/digits;
    const int w=static_cast<int>(800/scale),h=static_cast<int>(600/scale);
    return {(800-w)/2,(600-h)/2,w,h};
}
}
