#pragma once
#include "scene/scene.hpp"
namespace ef {
// Normal StageInfo path; canonical 100ms samples use the installation's effect interval.
class Walking {
public:
    Walking(int period,int speed);
    void update(double seconds);
    std::vector<Layer> layers(const std::vector<Layer>& base) const;
private:
    double elapsed_{};
};
}
