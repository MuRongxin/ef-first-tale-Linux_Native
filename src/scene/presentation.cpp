#include "scene/presentation.hpp"
#include "scene/transition.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <iostream>

namespace ef {
namespace {
int number(const std::string& value) {
    int result{};
    const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size()) throw std::runtime_error("Expected integer: " + value);
    return result;
}
std::pair<int,int> placement(const std::string& value) {
    const auto comma=value.find(',');
    if (comma==std::string::npos) return {number(value),0};
    return {number(value.substr(0,comma)),std::max(0,number(value.substr(comma+1)))};
}
bool numeric(const std::string& value) {
    try { (void)number(value); return true; } catch (const std::exception&) { return false; }
}
void count(const Instruction& instruction, std::size_t minimum, std::size_t maximum) {
    if (instruction.args.size() < minimum || instruction.args.size() > maximum) throw std::runtime_error("Invalid argument count for " + instruction.command);
}
int optional(const std::vector<std::string>& args, std::size_t index, int fallback) {
    return index >= args.size() ? fallback : args[index] == "*" ? 2 : number(args[index]);
}
double fade(int speed, float gain = 1) { return speed <= 0 ? 0 : 2.0 * gain / speed; }
const char* channel_name(AudioChannel channel) {
    switch (channel) {
    case AudioChannel::bgm:return "BGM";
    case AudioChannel::voice:return "VOICE";
    case AudioChannel::se:return "SE";
    case AudioChannel::se2:return "SE2";
    }
    return "unknown";
}
}
std::vector<Layer> Presentation::current() const {
    auto result=walking_ ? walking_->layers(current_) : current_;
    const auto overlay=effects();
    const auto frame=std::find_if(result.begin(),result.end(),[](const Layer& layer) { return layer.part==StagePart::frame; });
    result.insert(frame,overlay.begin(),overlay.end());
    const auto hud=fading_ ? transition_frame_ : frame_layers();
    result.insert(result.end(),hud.begin(),hud.end());
    return result;
}
void Presentation::local_assignment(const std::string& name, std::int64_t value) {
    if (name!="finderCounter") return;
    if (value<0 || value>35999) throw std::runtime_error("finderCounter outside supported 0..35999 seconds");
    finder_ticks_=static_cast<double>(value)*100;
}
std::int64_t Presentation::finder_counter() const {
    return static_cast<std::int64_t>(std::floor(finder_ticks_+1e-7))/100;
}
std::vector<Layer> Presentation::frame_layers() const {
    if (!frame_) return {};
    std::vector<Layer> result{*frame_};
    if (!finder_) return result;
    // Native copies battery RGBA into the finder bitmap. Cut out its original full icon,
    // so a translucent low/empty battery is composited once instead of over the full one.
    result.clear();
    constexpr std::array<std::array<float,4>,4> strips{{{0,0,800,12},{0,12,28,35},{87,12,713,35},{0,47,800,553}}};
    for (const auto& rect : strips) {
        auto piece=*frame_;piece.source=rect;piece.x=rect[0];piece.y=rect[1];piece.width=rect[2];piece.height=rect[3];
        result.push_back(std::move(piece));
    }
    const auto seconds=finder_counter();
    const std::size_t battery=seconds>32400 ? 3 : seconds>25200 ? 2 : seconds>18000 ? 1 : 0;
    result.push_back(finder_batteries_[battery]);
    const std::array<int,5> digits{static_cast<int>(seconds/3600),static_cast<int>(seconds/60%60/10),
        static_cast<int>(seconds/60%10),static_cast<int>(seconds%60/10),static_cast<int>(seconds%10)};
    constexpr std::array<float,5> x{597,641,672,716,747};
    for (std::size_t i=0;i<digits.size();++i) {
        Layer digit;digit.kind=LayerKind::text;digit.slot="finder_counter";
        digit.text=std::to_string(digits[i]);digit.x=x[i];digit.y=13;digit.width=30;digit.height=46;
        // Native positions are observed; this FreeType typography is a port choice, not a bitmap match.
        digit.font_size=46;digit.color={0.78f,0.9f,1,1};
        result.push_back(std::move(digit));
    }
    return result;
}
void Presentation::stop_walking() { if (walking_) { current_=walking_->layers(current_); walking_.reset(); } }
Layer Presentation::image(const std::string& name, bool r18) const {
    const auto& resource = content_.resource("image", name, r18);
    const auto& asset = content_.asset(resource.id);
    Layer result;
    result.kind = LayerKind::image; result.asset = resource.id;
    result.width = static_cast<float>(asset.width); result.height = static_cast<float>(asset.height);
    return result;
}
void Presentation::play(AudioChannel channel, const std::string& kind, const std::string& name, bool r18, bool loop, float gain, double fade_seconds, double fade_out_rate) {
    const auto index=static_cast<std::size_t>(channel);
    if (name.empty() || name == "*") {
        mixer_.stop_current(channel, fade_seconds, fade_out_rate);
        if (logging_ && !playing_ids_.at(index).empty())
            std::cout<<"[audio] stop channel="<<channel_name(channel)<<" resource="<<playing_ids_.at(index)
                     <<" fade_seconds="<<fade_seconds<<" fade_out_rate="<<fade_out_rate<<"\n\n"<<std::flush;
        playing_ids_.at(static_cast<std::size_t>(channel)).clear();
        playing_loops_.at(static_cast<std::size_t>(channel))=false;
        return;
    }
    const auto& resource = content_.resource(kind, name, r18);
    // Decode only on the control thread. The mixer retains a bounded pool of current/outgoing clips.
    auto clip = decoder_(content_.resolve(resource.path));
    mixer_.play(channel, clip, loop, gain, 0, fade_seconds, 0, fade_out_rate, resource.id);
    if (logging_) {
        std::cout<<"[audio] play channel="<<channel_name(channel)<<" resource="<<resource.id
                 <<" previous="<<(playing_ids_.at(index).empty() ? "none" : playing_ids_.at(index))
                 <<" source_rate="<<clip->sample_rate<<"Hz source_channels="<<clip->channels
                 <<" duration_seconds="<<clip->duration()<<" loop="<<(loop ? "yes" : "no")
                 <<" gain="<<gain<<" fade_in_seconds="<<fade_seconds<<" fade_out_rate="<<fade_out_rate
                 <<" clock="<<(realtime_ ? "audio-device" : "silent")<<" file="<<content_.resolve(resource.path)
                 <<"\n\n"<<std::flush;
    }
    playing_ids_.at(static_cast<std::size_t>(channel)) = resource.id;
    playing_loops_.at(static_cast<std::size_t>(channel))=loop;
}
void Presentation::voice(const std::string& name, bool r18) { play(AudioChannel::voice,"voice",name,r18,false,1,0); }
void Presentation::message(const Message& value) { voice(value.voice, value.script.starts_with("r18_scr/")); }
float Presentation::progress() const {
    if (!fading_) return 1;
    // Native transition state is an integer counter for every type:
    // step=(elapsed_10ms_ticks*speed)/10 (dispatcher 0x53adc0; type0/type1 update
    // functions consume DAT_005df330 as an int as well).
    const double ticks=std::floor(elapsed_*100+1e-9);
    const double step=std::floor(ticks*active_speed_/10);
    return static_cast<float>(std::min(1.0,step/transition_completion(active_type_)));
}
bool Presentation::busy() const { return movie_active_ || shaking() || fading_ || (wait_scroll_ && scroll_.has_value()); }
void Presentation::update(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid presentation delta time");
    finder_ticks_=std::min(3599900.0,finder_ticks_+seconds*100);
    if (!realtime_) mixer_.advance(seconds);
    mixer_.collect_finished();
    if (shake_) { shake_->update(seconds); if (shake_->finished()) shake_.reset(); }
    if (fading_) { elapsed_ += seconds; if (progress() >= 1) { fading_ = false; previous_.clear(); transition_frame_.clear(); } }
    if (walking_) walking_->update(seconds);
    if (cutin_) { cutin_->update(seconds); if (cutin_->finished()) cutin_.reset(); }
    if (snow_) { snow_->update(seconds); if (snow_->finished()) snow_.reset(); }
    if (!scroll_) return;
    auto& scroll = *scroll_;
    scroll.elapsed += seconds;
    const double distance = std::max(std::abs(scroll.target_x-scroll.x),std::abs(scroll.target_y-scroll.y));
    const double fraction = distance == 0 ? 1 : std::min(1.0,std::floor(scroll.elapsed*100)*scroll.speed/10/distance);
    auto& layer = current_.at(scroll.layer);
    layer.x = scroll.x + static_cast<float>(fraction)*(scroll.target_x-scroll.x);
    layer.y = scroll.y + static_cast<float>(fraction)*(scroll.target_y-scroll.y);
    if (fraction >= 1) { scroll_.reset(); wait_scroll_ = false; }
}
bool Presentation::command(const Instruction& instruction) {
    const auto& cmd = instruction.command;
    const auto& args = instruction.args;
    const bool r18 = instruction.script.starts_with("r18_scr/");
    if (cmd == ".movie") {
        count(instruction, 4, 7);
        if (!movie_supported_) throw std::runtime_error(".movie requires FFmpeg movie playback support");
        MovieRequest request;
        request.id = number(args[0]);
        const auto& resource = content_.resource("video", args[1], r18);
        request.asset = resource.id;
        request.path = resource.path;
        request.width = args.size() > 2 ? number(args[2]) : 800;
        request.height = args.size() > 3 ? number(args[3]) : 600;
        if (request.width <= 0 || request.height <= 0 || request.width > 8192 || request.height > 8192)
            throw std::runtime_error(".movie destination rectangle is out of range");
        const std::string flag = args.size() > 4 ? args[4] : std::string{};
        request.skippable = flag.empty() || !(flag[0] == '0' || flag[0] == 'f' || flag[0] == 'F');
        request.x = args.size() > 5 ? number(args[5]) : 0;
        request.y = args.size() > 6 ? number(args[6]) : 0;
        // The native player clears the current scene before handing the screen to the
        // video renderer (0x4db670 -> 0x4d7e00/0x4d7f00/0x4d8d20).
        previous_.clear(); current_.clear(); transition_frame_.clear();
        fading_ = false; scroll_.reset(); wait_scroll_ = false;
        snow_.reset(); cutin_.reset(); shake_.reset();
        screens_ = {}; overlays_ = {}; hsv_ = {};
        movie_request_ = std::move(request);
        movie_active_ = true;
        return true;
    }
    if (cmd == ".frame") {
        count(instruction,1,1);
        std::optional<Layer> next;
        const bool finder=!args[0].empty() && resource_name(args[0])=="finder.png";
        auto batteries=finder_batteries_;
        if (!args[0].empty() && args[0]!="*") {
            next=image(args[0],r18);
            if (pending_frame_ && next->asset==pending_frame_->asset) return true;
            if (finder) {
                if (next->width!=800 || next->height!=600) throw std::runtime_error("Finder requires 800x600 canvas");
                constexpr std::array<const char*,4> names{"batFull.png","batMid.png","batLow.png","batZero.png"};
                for (std::size_t i=0;i<names.size();++i) {
                    batteries[i]=image(names[i],r18);batteries[i].x=28;batteries[i].y=12;
                    if (batteries[i].width!=59 || batteries[i].height!=35) throw std::runtime_error("Unexpected finder battery dimensions");
                }
            }
        }
        pending_frame_=std::move(next);pending_finder_=finder;finder_batteries_=std::move(batteries);
        if (finder) finder_ticks_=static_cast<double>(finder_counter())*100;
    } else if (cmd == ".shakeScreen") {
        count(instruction,3,3);
        Shake next(args[0],number(args[1]),number(args[2]));
        shake_=std::move(next); ++shake_serial_;
        scroll_.reset(); wait_scroll_=false;
    } else if (cmd == ".screen" || cmd == ".overlay") {
        count(instruction,2,3);
        std::array<std::string,3> next;
        const std::size_t first=args.size()==2 ? 1 : 0;
        for (std::size_t n=0; n<args.size(); ++n)
            if (!args[n].empty() && args[n]!="*") next[first+n]=image(args[n],r18).asset;
        (cmd==".screen" ? screens_ : overlays_)=std::move(next); // Filters update pending state only.
    } else if (cmd == ".HSVfilter") {
        count(instruction,6,6);
        std::array<std::array<int,3>,2> next;
        for (std::size_t n=0;n<6;++n) next[n/3][n%3]=std::clamp(number(args[n]),n%3==0 ? -180 : -100,n%3==0 ? 180 : 100);
        hsv_=next;
    } else if (cmd == ".transition") {
        count(instruction,3,3);
        const int type=number(args[0]),speed=number(args[2]);
        // type2's ghost replay samples once per transitionDrawInterval (5 ticks); the counter
        // then advances speed/2 per sample, which the renderer models exactly only for the
        // single scripted speed=20. Keep the corpus-driven restriction until other speeds appear.
        if (speed<=0 || !supported_transition(type) || (type!=1 && args[1]!="*") || (type==1 && (args[1]=="*" || speed>255)) || (type==2 && speed!=20))
            throw std::runtime_error("Unsupported transition type, mask or speed");
        std::string mask;
        if (type==1) {
            mask=image(args[1],r18).asset;
            const auto& asset=content_.asset(mask);
            if (asset.width!=800 || asset.height!=600) throw std::runtime_error("Transition mask must use the 800x600 design canvas");
        }
        speed_=speed; transition_type_=type; transition_mask_=std::move(mask);
    } else if (cmd == ".stage") {
        count(instruction,4,26);
        std::vector<Layer> next;
        std::size_t cursor = 1;
        int frame_x{}, frame_y{};
        if (numeric(args[1]) && numeric(args[2])) {
            frame_x = number(args[1]); frame_y = number(args[2]); cursor += 2;
        }
        if (args.size() < cursor+3 || (args.size()-cursor-3)%2 != 0 || (args.size()-cursor-3)/2 > 10)
            throw std::runtime_error("Stage requires frame [x y] background crop_x crop_y [foreground center_x]...");
        std::optional<std::size_t> background;
        if (args[cursor] != "*") {
            auto layer = image(args[cursor],r18);
            layer.part=StagePart::background; layer.screen=screens_[1]; layer.overlay=overlays_[1]; layer.hsv=hsv_[0];
            layer.x = -std::clamp(static_cast<float>(number(args[cursor+1])),0.0f,std::max(0.0f,layer.width-800));
            layer.y = -std::clamp(static_cast<float>(number(args[cursor+2])),0.0f,std::max(0.0f,layer.height-600));
            background = next.size(); next.push_back(std::move(layer));
        } else { (void)number(args[cursor+1]); (void)number(args[cursor+2]); }
        for (cursor += 3; cursor < args.size(); cursor += 2) {
            const auto [center,trim] = placement(args[cursor+1]);
            if (args[cursor] == "*") continue;
            auto layer = image(args[cursor],r18);
            layer.part=StagePart::foreground; layer.screen=screens_[2]; layer.overlay=overlays_[2]; layer.hsv=hsv_[1];
            if (trim>=layer.height) throw std::runtime_error("Stage bottom trim must leave a positive image height");
            layer.trim_bottom=trim;layer.height-=static_cast<float>(trim);
            layer.x = static_cast<float>(center)-std::floor(layer.width/2); layer.y = 600-layer.height;
            next.push_back(std::move(layer));
        }
        if (args[0] != "*") {
            auto layer = image(args[0],r18);
            layer.part=StagePart::frame; layer.screen=screens_[0]; layer.overlay=overlays_[0];
            layer.x = static_cast<float>(frame_x); layer.y = static_cast<float>(frame_y);
            next.push_back(std::move(layer));
        }
        previous_ = current(); current_ = std::move(next); background_ = background;
        frame_=pending_frame_;finder_=pending_finder_;
        scroll_.reset(); wait_scroll_ = false;
        active_type_=transition_type_; active_speed_=speed_; active_mask_=transition_mask_;
        // Native main loop passes (speed > 255) to the transition dispatcher as "finish now",
        // for every type (0x4e11a0 case 0xe setg -> 0x53adc0 param_1 -> immediate new-frame copy).
        ++revision_; elapsed_ = 0; fading_ = active_speed_ <= 255;
        if (!fading_) previous_.clear();
        transition_frame_=fading_ ? frame_layers() : std::vector<Layer>{};
    } else if (cmd == ".vscroll" || cmd == ".hscroll" || cmd == ".scroll") {
        count(instruction,cmd == ".scroll" ? 3 : 2,cmd == ".scroll" ? 3 : 2);
        if (!background_) throw std::runtime_error("Scrolling requires a background");
        const auto& layer = current_.at(*background_);
        float x = -layer.x, y = -layer.y;
        int speed{};
        if (cmd == ".scroll") { x = static_cast<float>(number(args[0])); y = static_cast<float>(number(args[1])); speed = number(args[2]); }
        else { (cmd == ".hscroll" ? x : y) = static_cast<float>(number(args[0])); speed = number(args[1]); }
        x = std::clamp(x,0.0f,std::max(0.0f,layer.width-800));
        y = std::clamp(y,0.0f,std::max(0.0f,layer.height-600));
        const double dx = x+layer.x, dy = y+layer.y;
        const double major = std::abs(dx) >= std::abs(dy) ? dx : dy;
        // Two-axis scrolling derives each direction from the endpoints; its speed is a magnitude.
        if (speed == 0 || (cmd == ".scroll" ? speed < 0 : major != 0 && ((major > 0) != (speed > 0))))
            throw std::runtime_error("Invalid scroll speed or direction");
        scroll_ = Scroll{*background_,layer.x,layer.y,-x,-y,0,std::abs(static_cast<double>(speed))};
    } else if (cmd == ".endScroll") {
        count(instruction,1,1);
        if (args[0] != "f" && args[0] != "t") throw std::runtime_error("Expected endScroll f or t");
        if (args[0] == "t" && scroll_) {
            auto& layer = current_.at(scroll_->layer); layer.x = scroll_->target_x; layer.y = scroll_->target_y;
            scroll_.reset(); wait_scroll_ = false;
        } else wait_scroll_ = scroll_.has_value();
    } else if (cmd == ".panel") {
        count(instruction,1,1); const int mode = number(args[0]);
        if (mode < 0 || mode > 5) throw std::runtime_error("Unsupported panel mode");
        panel_ = mode; // Imported MessageUI owns panel geometry and transition timing.
    } else if (cmd == ".effect") {
        count(instruction,1,5);
        if (args[0] == "*" || args[0] == "end") { count(instruction,1,1); stop_walking(); snow_.reset(); cutin_.reset(); }
        else if (args[0] == "fadeout") { count(instruction,1,1); if (walking_) throw std::runtime_error("Walking fadeout behavior is not implemented; use effect end"); if (snow_) snow_->fade_out(); if (cutin_) cutin_->fade_out(); }
        else if (args[0] == "Cutin") {
            count(instruction,3,4);
            Cutin next(image(args[1],r18),number(args[2]),args.size()>3 ? number(args[3]) : -1);
            stop_walking(); cutin_=std::move(next); snow_.reset();
        } else if (args[0] == "WScroll2") {
            count(instruction,4,4);
            if (args[1]!="*") throw std::runtime_error("WScroll2 object-list variants are not implemented");
            Walking next(number(args[2]),number(args[3]));
            (void)next.layers(current_); // Validate without replacing an existing effect.
            if (!walking_) walking_=std::move(next);
            snow_.reset(); cutin_.reset();
        } else if (args[0] == "Snow" || args[0] == "SnowH") {
            count(instruction,1,1);
            std::array<Layer,3> sprites{image("snowS.png",r18),image("snowM.png",r18),image("snowL.png",r18)};
            stop_walking(); snow_.emplace(std::move(sprites),args[0] == "SnowH"); cutin_.reset();
        } else return false;
    } else if (cmd == ".playVoice") {
        // Native voice parser consumes only the first token; JSON already canonicalizes it.
        if (args.empty()) throw std::runtime_error("playVoice requires an asset or *");
        voice(args[0],r18);
    } else if (cmd == ".playBGM" || cmd == ".playSE" || cmd == ".playSE2") {
        count(instruction,1,4);
        const bool bgm = cmd == ".playBGM";
        const auto channel = bgm ? AudioChannel::bgm : cmd == ".playSE" ? AudioChannel::se : AudioChannel::se2;
        const bool stop = args[0] == "*";
        const int in_speed = optional(args,bgm ? 1 : 2,bgm ? 1 : 2);
        const int out_speed = optional(args,bgm ? 2 : 3,bgm ? 1 : 2);
        // Native SE loop parsing tests the first character; corpus also uses "true".
        const bool loop=bgm || (args.size()>1 && args[1].starts_with('t'));
        const int volume = bgm && args.size() > 3 ? number(args[3]) : 100;
        if (volume < 0 || volume > 100) throw std::runtime_error("BGM volume outside 0..100");
        const float gain = static_cast<float>(volume)/100;
        const auto channel_index=static_cast<std::size_t>(channel);
        const bool same_loop = !stop && loop && playing_loops_.at(channel_index) && mixer_.current_playing(channel) &&
            playing_ids_.at(channel_index) == content_.resource(bgm ? "bgm" : "se",args[0],r18).id;
        if (same_loop) {
            if (bgm) mixer_.set_gain(channel,gain,0,in_speed>0 ? in_speed/2.0 : 0);
            if (logging_) std::cout<<"[audio] reuse channel="<<channel_name(channel)<<" resource="<<playing_ids_.at(channel_index)
                                  <<" action="<<(bgm ? "set-gain" : "keep-loop")<<" gain="<<mixer_.gain(channel)
                                  <<" requested_gain="<<gain<<" gain_rate="<<(bgm && in_speed>0 ? in_speed/2.0 : 0)<<"\n\n"<<std::flush;
            return true;
        }
        if (stop) play(channel,bgm ? "bgm" : "se",args[0],r18,false,0,0,out_speed>0 ? out_speed/2.0 : 0);
        else play(channel,bgm ? "bgm" : "se",args[0],r18,loop,gain,
                  fade(in_speed,gain),out_speed>0 ? out_speed/2.0 : 0);
    } else return false;
    return true;
}
}
