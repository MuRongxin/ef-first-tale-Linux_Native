#include "scene/animation_clock.hpp"
#include <array>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace ef {
namespace {
// REA read_bytes 0x573e68..0x5740bf, pointers at 0x5aa054.
constexpr std::array<std::array<int,26>,6> eyes{{
    {0,40,1,40,2,80,1,40,0,400,1,40,2,80,1,40,0,3600,1,40,2,80,1,40,0,1200},
    {0,10,1,20,2,40,1,20,0,200,1,20,2,40,1,20,0,1800,1,20,2,40,1,20,0,600},
    {0,10,1,10,2,20,1,10,0,100,1,10,2,20,1,10,0,900,1,10,2,20,1,10,0,300},
    {0,5,1,5,2,10,1,5,0,100,1,5,2,10,1,5,0,450,1,5,2,10,1,5,0,150},
    {0,3,1,3,2,7,1,3,0,33,1,3,2,7,1,3,0,225,1,3,2,7,1,3,0,75},
    {0,30,1,30,2,30,1,30}
}};
}
EyeClock::EyeClock(int sequence):sequence_(sequence) {
    if (sequence<0 || sequence>5) throw std::runtime_error("Unsupported eye sequence");
}
bool EyeClock::update(std::uint32_t tick,std::uint32_t random_value) {
    if (started_) {
        const auto elapsed=tick-last_;
        if (!elapsed) return false;
        last_=tick;
        if (elapsed<remaining_) {remaining_-=elapsed;return false;}
    } else {started_=true;last_=tick;}
    const auto& sequence=eyes[static_cast<std::size_t>(sequence_)];
    frame_=sequence[cursor_*2];remaining_=static_cast<std::uint32_t>(sequence[cursor_*2+1]);
    if (!cursor_ && sequence_<5) remaining_+=random_value%500+50;
    if (++cursor_==(sequence_==5 ? 4U : 13U)) cursor_=0;
    return true;
}
bool LipState::update(std::uint32_t amplitude,std::uint32_t serial,bool active) {
    if (serial==serial_) return false;
    serial_=serial;const int old=frame_;
    if (active && amplitude>=800) {
        if (amplitude>amplitude_) frame_=frame_==0 ? 1 : 2;
        else frame_=frame_==1 && amplitude>=(amplitude_>>1) ? 1 : 0;
    } else frame_=frame_==2 ? 1 : 0;
    amplitude_=amplitude;
    return old!=frame_;
}
std::vector<AnimationStep> parse_animation_loop(const std::string& source) {
    if (source.size()>4096 || !source.starts_with("@;") || !source.ends_with(";~"))
        throw std::runtime_error("Unsupported animation loop grammar");
    std::vector<AnimationStep> result;
    std::size_t start=2;
    while (start<source.size()-1) {
        const auto end=source.find(';',start),colon=source.find(':',start);
        if (end==std::string::npos || colon==std::string::npos || colon>=end || colon==start || result.size()>=256)
            throw std::runtime_error("Invalid animation step");
        auto name=source.substr(start,colon-start);
        if (name.find_first_of("@~:\0",0,4)!=std::string::npos) throw std::runtime_error("Invalid animation frame name");
        std::uint32_t duration{};
        const auto parsed=std::from_chars(source.data()+colon+1,source.data()+end,duration);
        if (parsed.ec!=std::errc{} || parsed.ptr!=source.data()+end || !duration || duration>1000000000)
            throw std::runtime_error("Invalid animation duration");
        result.push_back({std::move(name),duration});start=end+1;
    }
    if (result.empty()) throw std::runtime_error("Empty animation loop");
    return result;
}
AnimationLoop::AnimationLoop(std::vector<AnimationStep> steps):steps_(std::move(steps)) {
    if (steps_.empty() || steps_.size()>256) throw std::runtime_error("Invalid animation loop size");
    for (const auto& s:steps_) if (s.frame.empty() || !s.duration || s.duration>1000000000)
        throw std::runtime_error("Invalid animation loop step");
}
bool AnimationLoop::update(std::uint32_t tick) {
    if (started_) {
        const auto elapsed=tick-last_;last_=tick;
        if (elapsed<remaining_) {remaining_-=elapsed;return false;}
    } else {started_=true;last_=tick;}
    const auto& step=steps_[cursor_];frame_=step.frame;remaining_=step.duration;
    cursor_=(cursor_+1)%steps_.size();return true;
}
}
