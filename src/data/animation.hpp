#pragma once
#include <string>
#include <vector>
namespace ef {
struct AnimationPatch {
    std::string actor,state,path;
    int width{},height{},x{},y{};
};
struct AnimationSpec {
    std::string source_sha256;
    std::vector<AnimationPatch> patches;
};
}
