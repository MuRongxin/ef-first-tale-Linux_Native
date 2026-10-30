#pragma once
#include "data/animation.hpp"
#include "scene/animation_clock.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <optional>

namespace ef {
// Native name aliases plus the verified Chinese localization table.
std::string animation_actor_name(std::string name);
class ImageAnimation {
public:
    ImageAnimation(const AnimationSpec&,int eye_sequence=2);
    void update(double seconds,bool animate=true);
    void voice(const std::string& actor,std::uint32_t amplitude,std::uint32_t serial,bool active);
    void reset_voice();
    [[nodiscard]] std::vector<std::size_t> frames() const;
    [[nodiscard]] nlohmann::json checkpoint() const;
    static ImageAnimation restore(const AnimationSpec&,const nlohmann::json&);
private:
    struct Track {
        std::string actor;
        std::map<std::string,std::size_t> images;
        std::optional<EyeClock> eye;
        std::optional<LipState> lip;
        std::optional<AnimationLoop> loop;
    };
    std::string hash_;
    int sequence_{};
    double elapsed_{};
    std::uint32_t seed_{0x616e6931};
    std::vector<Track> tracks_;
};
}
