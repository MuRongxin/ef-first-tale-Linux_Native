#pragma once
#include "scene/scene.hpp"
#include <cstdint>
namespace ef {
class Cutin {
    friend struct CheckpointCodec;
public:
    Cutin(Layer image,int hold_ticks,int acceleration=-1);
    void update(double seconds);
    void fade_out() { exiting_=true; }
    bool finished() const { return state_==3; }
    Layer layer() const;
private:
    void step();
    Layer image_;
    int hold_{},state_{},x_{};
    bool rightward_{},exiting_{};
    double factor_{},velocity_{},elapsed_{};
    std::uint64_t next_tick_{1};
};
}
