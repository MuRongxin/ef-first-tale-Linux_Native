#include "scene/image_animation.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <set>

namespace ef {
namespace {
void require(bool value) {if (!value) throw std::runtime_error("Invalid saved image animation");}
std::uint32_t u32(const nlohmann::json& j) {
    require(j.is_number_integer() && j.get<double>()>=0 && j.get<double>()<=UINT32_MAX);
    return j.get<std::uint32_t>();
}
}
std::string animation_actor_name(std::string name) {
    if (name.starts_with('#') || name.starts_with('@')) name.erase(0,1);
    for (auto at=name.find("　");at!=std::string::npos;at=name.find("　")) name.erase(at,3);
    std::erase(name,' ');
    static const std::map<std::string,std::string> aliases{
        {"Yuu","夕"},{"Yuuko","優子"},{"优子","優子"},{"Hiro","紘"},{"纮","紘"},
        {"Miyako","みやこ"},{"宫子","みやこ"},{"Kyousuke","京介"},{"Kei","景"},
        {"Chihiro","千尋"},{"千寻","千尋"},{"Mizuki","ミズキ"},{"瑞希","ミズキ"},
        {"Nagi","凪"},{"Yoshihiko","大村"}};
    const auto found=aliases.find(name);return found==aliases.end() ? name : found->second;
}
namespace {
std::string actor_key(std::string name) {
    if (name.size()>1) {
        if ((name[0]>='a' && name[0]<='z') || name[0]=='$') name.erase(0,1);
        if (name.ends_with('>')) if (const auto at=name.find('<');at!=std::string::npos) name.resize(at);
    }
    return animation_actor_name(std::move(name));
}
}
ImageAnimation::ImageAnimation(const AnimationSpec& spec,int eye_sequence):hash_(spec.source_sha256),sequence_(eye_sequence) {
    for (const unsigned char ch:hash_) seed_=(seed_^ch)*16777619U;
    std::map<std::string,Track> groups;
    for (std::size_t i=0;i<spec.patches.size();++i) {
        const auto& patch=spec.patches[i];auto& group=groups[patch.actor];group.actor=patch.actor;
        if (!patch.path.empty()) {
            if (!group.images.emplace(patch.state,i).second) throw std::runtime_error("Duplicate animation state");
        } else {
            if (group.loop) throw std::runtime_error("Duplicate animation loop");
            group.loop.emplace(parse_animation_loop(patch.state));
        }
    }
    for (auto& [actor,group]:groups) {
        (void)actor;
        if (group.images.contains("中口") || group.images.contains("開き口")) group.lip.emplace();
        if (group.images.contains("中目") || group.images.contains("閉じ目")) group.eye.emplace(eye_sequence);
        if (group.loop) for (const auto& step:group.loop->steps_)
            if (step.frame!="*" && !group.images.contains(step.frame)) throw std::runtime_error("Animation loop references missing state");
        if (group.eye || group.loop || group.lip) tracks_.push_back(std::move(group));
    }
    update(0);
}
void ImageAnimation::update(double seconds,bool animate) {
    if (!std::isfinite(seconds) || seconds<0 || elapsed_+seconds>1e9) throw std::runtime_error("Invalid animation delta");
    elapsed_+=seconds;
    if (!animate) return;
    // Native tick conversion is established in the host integration evidence.
    const auto tick=static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::floor(elapsed_*100+1e-7)));
    for (auto& track:tracks_) {
        if (track.eye && (!track.eye->started_ || track.eye->last_!=tick)) {
            const auto& eye=*track.eye;
            if (!eye.cursor_ && (!eye.started_ || tick-eye.last_>=eye.remaining_)) seed_=seed_*1664525U+1013904223U;
            track.eye->update(tick,seed_>>16);
        }
        if (track.loop) track.loop->update(tick);
    }
}
void ImageAnimation::voice(const std::string& actor,std::uint32_t amplitude,std::uint32_t serial,bool active) {
    for (auto& track:tracks_) if (track.lip) {
        if (actor=="*" || actor_key(track.actor)==animation_actor_name(actor)) track.lip->update(amplitude,serial,active);
        else *track.lip=LipState{};
    }
}
void ImageAnimation::reset_voice() {
    for (auto& track:tracks_) if (track.lip) *track.lip=LipState{};
}
std::vector<std::size_t> ImageAnimation::frames() const {
    std::vector<std::size_t> result;
    for (const auto& track:tracks_) {
        const auto add=[&](const std::string& state) {const auto it=track.images.find(state);if (it!=track.images.end()) result.push_back(it->second);};
        if (track.eye && track.eye->frame()) add(track.eye->frame()==1 ? "中目" : "閉じ目");
        if (track.lip && track.lip->frame()) add(track.lip->frame()==1 ? "中口" : "開き口");
        if (track.loop && track.loop->frame()!="*") add(track.loop->frame());
    }
    return result;
}
nlohmann::json ImageAnimation::checkpoint() const {
    auto tracks=nlohmann::json::array();
    for (const auto& t:tracks_) {
        nlohmann::json eye=nullptr,loop=nullptr,lip=nullptr;
        if (t.eye) {const auto& e=*t.eye;eye={e.frame_,e.cursor_,e.last_,e.remaining_,e.started_};}
        if (t.loop) {const auto& l=*t.loop;loop={l.frame_,l.cursor_,l.last_,l.remaining_,l.started_};}
        if (t.lip) {const auto& l=*t.lip;lip={l.frame_,l.amplitude_,l.serial_};}
        tracks.push_back({{"actor",t.actor},{"eye",eye},{"loop",loop},{"lip",lip}});
    }
    return {{"version",1},{"source_sha256",hash_},{"sequence",sequence_},{"elapsed",elapsed_},{"seed",seed_},{"tracks",tracks}};
}
ImageAnimation ImageAnimation::restore(const AnimationSpec& spec,const nlohmann::json& j) {
    require(j.is_object() && j.size()==6 && j.at("version")==1 && j.at("source_sha256")==spec.source_sha256);
    const auto sequence=u32(j.at("sequence"));require(sequence<=5);
    ImageAnimation result(spec,static_cast<int>(sequence));
    require(j.at("elapsed").is_number());result.elapsed_=j.at("elapsed").get<double>();
    require(std::isfinite(result.elapsed_) && result.elapsed_>=0 && result.elapsed_<=1e9);
    result.seed_=u32(j.at("seed"));
    const auto& tracks=j.at("tracks");require(tracks.is_array() && tracks.size()<=result.tracks_.size());
    const auto tick=static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::floor(result.elapsed_*100+1e-7)));
    std::set<std::string> restored_actors;
    for (const auto& saved:tracks) {
        require(saved.is_object() && (saved.size()==3 || (saved.size()==4 && saved.contains("lip"))) && saved.at("actor").is_string());
        const auto actor=saved.at("actor").get<std::string>();
        require(restored_actors.insert(actor).second);
        auto found=std::find_if(result.tracks_.begin(),result.tracks_.end(),[&](const auto& t) {return t.actor==actor;});
        require(found!=result.tracks_.end());auto& t=*found;
        const auto& eye=saved.at("eye");const auto& loop=saved.at("loop");
        require(eye.is_null()==!t.eye && loop.is_null()==!t.loop);
        if (saved.contains("lip")) {
            const auto& lip=saved.at("lip");require(lip.is_null()==!t.lip);
            if (t.lip) {
                require(lip.is_array() && lip.size()==3);
                const auto frame=u32(lip[0]);require(frame<=2);
                t.lip->frame_=static_cast<int>(frame);t.lip->amplitude_=u32(lip[1]);t.lip->serial_=u32(lip[2]);
            }
        }
        if (t.eye) {
            require(eye.is_array() && eye.size()==5 && eye[4].is_boolean() && eye[4].get<bool>());
            auto& e=*t.eye;const auto frame=u32(eye[0]),cursor=u32(eye[1]);require(frame<=2 && cursor<(sequence==5 ? 4U : 13U));
            const auto previous=(cursor+(sequence==5 ? 4U : 13U)-1)%(sequence==5 ? 4U : 13U);
            require(frame==(previous%4==0 ? 0U : previous%4==2 ? 2U : 1U));
            e.frame_=static_cast<int>(frame);e.cursor_=cursor;e.last_=u32(eye[2]);e.remaining_=u32(eye[3]);e.started_=true;
            require(e.last_==tick && e.remaining_>0 && e.remaining_<=5000);
        }
        if (t.loop) {
            require(loop.is_array() && loop.size()==5 && loop[0].is_string() && loop[4].is_boolean() && loop[4].get<bool>());
            auto& l=*t.loop;l.frame_=loop[0].get<std::string>();l.cursor_=u32(loop[1]);l.last_=u32(loop[2]);l.remaining_=u32(loop[3]);l.started_=true;
            require(l.cursor_<l.steps_.size() && l.last_==tick);
            const auto& previous=l.steps_[(l.cursor_+l.steps_.size()-1)%l.steps_.size()];
            require(l.frame_==previous.frame && l.remaining_>0 && l.remaining_<=previous.duration);
        }
    }
    for (const auto& t:result.tracks_) if (t.eye || t.loop) require(restored_actors.contains(t.actor));
    return result;
}
}
