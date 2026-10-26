#include "scene/shake.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
namespace ef {
Shake::Shake(const std::string& mode,int amplitude,int duration,std::uint32_t seed)
    : mode_(mode.empty() ? 'R' : static_cast<char>(std::toupper(static_cast<unsigned char>(mode.front())))),
      amplitude_(amplitude),duration_(duration),seed_(seed) {
    if (amplitude<0 || amplitude>10000 || duration<0 || duration>1000000)
        throw std::runtime_error("Unsupported shake amplitude or duration");
    if (mode_=='T') throw std::runtime_error("T shake mode has not been reconstructed");
    if (mode_!='Z' && mode_!='O' && mode_!='H' && mode_!='V') mode_='R'; // Native default also accepts a numeric token.
    if (mode_=='Z' && amplitude>=300) throw std::runtime_error("Zoom shake crop must leave a positive image region");
    if (mode_=='O' && amplitude>90) throw std::runtime_error("Rotation shake beyond 90 degrees is not supported");
    finished_=duration==0;
}
std::uint32_t Shake::random_at(std::uint64_t n) const {
    // CRT-compatible LCG with logarithmic jump-ahead; the initial seed is a port choice.
    std::uint32_t a=214013U,b=2531011U,m=1,c=0;
    while (n) { if (n&1) { c=a*c+b; m=a*m; } b=a*b+b; a*=a; n>>=1; }
    return ((m*seed_+c)>>16)&32767U;
}
void Shake::update(double seconds) {
    if (!std::isfinite(seconds) || seconds<0) throw std::runtime_error("Invalid shake delta time");
    if (finished_) return;
    elapsed_=std::min(static_cast<double>(duration_)/100,elapsed_+seconds);
    sampled_=true;
    finished_=elapsed_*100>=duration_-1e-9;
}
ShakeFrame Shake::frame() const {
    ShakeFrame result;
    if (finished_ || !sampled_) return result;
    const auto phase=static_cast<std::uint64_t>(std::floor((elapsed_*100+1e-9)/3));
    result.sample=phase+1;
    const auto a=static_cast<float>(amplitude_);
    if (mode_=='Z') { result.kind=ShakeKind::zoom; result.crop=(phase&1) ? 0 : a; }
    else if (mode_=='O') {
        result.kind=ShakeKind::rotation;
        result.angle=amplitude_ ? static_cast<float>(static_cast<int>(random_at(phase+1)%(2*static_cast<unsigned>(amplitude_)))-amplitude_) : 0;
    } else if (mode_=='H') result.x=(phase&1) ? a : -a;
    else if (mode_=='V') result.y=(phase&1) ? a : -a;
    else switch ((random_at(phase+1)&7U)/2) {
        case 0: result.x=-a; break;
        case 1: result.x=a; break;
        case 2: result.y=a; break;
        case 3: result.y=-a; break;
    }
    return result;
}
}
