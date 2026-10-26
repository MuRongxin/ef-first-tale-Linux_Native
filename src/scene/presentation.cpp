#include "scene/presentation.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace ef {
namespace {
int number(const std::string& value) {
    int result{};
    const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size()) throw std::runtime_error("Expected integer: " + value);
    return result;
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
}
Layer Presentation::image(const std::string& name, bool r18) const {
    const auto& resource = content_.resource("image", name, r18);
    const auto& asset = content_.asset(resource.id);
    Layer result;
    result.kind = LayerKind::image; result.asset = resource.id;
    result.width = static_cast<float>(asset.width); result.height = static_cast<float>(asset.height);
    return result;
}
void Presentation::play(AudioChannel channel, const std::string& kind, const std::string& name, bool r18, bool loop, float gain, double fade_seconds, double fade_out_rate) {
    if (name.empty() || name == "*") {
        mixer_.stop_current(channel, fade_seconds, fade_out_rate);
        playing_ids_.at(static_cast<std::size_t>(channel)).clear();
        playing_loops_.at(static_cast<std::size_t>(channel))=false;
        return;
    }
    const auto& resource = content_.resource(kind, name, r18);
    // Decode only on the control thread. The mixer retains a bounded pool of current/outgoing clips.
    auto clip = decode_audio(content_.resolve(resource.path));
    mixer_.play(channel, std::move(clip), loop, gain, 0, fade_seconds, 0, fade_out_rate);
    playing_ids_.at(static_cast<std::size_t>(channel)) = resource.id;
    playing_loops_.at(static_cast<std::size_t>(channel))=loop;
}
void Presentation::voice(const std::string& name, bool r18) { play(AudioChannel::voice,"voice",name,r18,false,1,0); }
void Presentation::message(const Message& value) { voice(value.voice, value.script.starts_with("r18_scr/")); }
float Presentation::progress() const {
    if (!fading_) return 1;
    return static_cast<float>(std::min(1.0, std::floor(elapsed_*100) * speed_ / 2560.0));
}
bool Presentation::busy() const { return fading_ || (wait_scroll_ && scroll_.has_value()); }
void Presentation::update(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid presentation delta time");
    if (!realtime_) mixer_.advance(seconds);
    mixer_.collect_finished();
    if (fading_) { elapsed_ += seconds; if (progress() >= 1) { fading_ = false; previous_.clear(); } }
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
    if (cmd == ".screen") {
        count(instruction,2,3);
        std::array<std::string,3> next;
        const std::size_t first=args.size()==2 ? 1 : 0;
        for (std::size_t n=0; n<args.size(); ++n)
            if (!args[n].empty() && args[n]!="*") next[first+n]=image(args[n],r18).asset;
        screens_=std::move(next); // .screen updates pending state, never the displayed snapshots.
    } else if (cmd == ".transition") {
        count(instruction,3,3);
        if (number(args[0]) != 0 || args[1] != "*" || number(args[2]) <= 0)
            throw std::runtime_error("Only unmasked type-0 crossfade with positive speed is implemented");
        speed_ = number(args[2]);
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
            layer.screen=screens_[1];
            layer.x = -std::clamp(static_cast<float>(number(args[cursor+1])),0.0f,std::max(0.0f,layer.width-800));
            layer.y = -std::clamp(static_cast<float>(number(args[cursor+2])),0.0f,std::max(0.0f,layer.height-600));
            background = next.size(); next.push_back(std::move(layer));
        } else { (void)number(args[cursor+1]); (void)number(args[cursor+2]); }
        for (cursor += 3; cursor < args.size(); cursor += 2) {
            const int center = number(args[cursor+1]); // annotated placement flags deliberately rejected
            if (args[cursor] == "*") continue;
            auto layer = image(args[cursor],r18);
            layer.screen=screens_[2];
            layer.x = static_cast<float>(center)-layer.width/2; layer.y = 600-layer.height;
            next.push_back(std::move(layer));
        }
        if (args[0] != "*") {
            auto layer = image(args[0],r18);
            layer.screen=screens_[0];
            layer.x = static_cast<float>(frame_x); layer.y = static_cast<float>(frame_y);
            next.push_back(std::move(layer));
        }
        previous_ = std::move(current_); current_ = std::move(next); background_ = background;
        scroll_.reset(); wait_scroll_ = false;
        ++revision_; elapsed_ = 0; fading_ = speed_ <= 255;
        if (!fading_) previous_.clear();
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
        count(instruction,1,1);
        if (args[0] == "*") snow_.reset();
        else if (args[0] == "fadeout") { if (snow_) snow_->fade_out(); }
        else if (args[0] == "Snow" || args[0] == "SnowH") {
            std::array<Layer,3> sprites{image("snowS.png",r18),image("snowM.png",r18),image("snowL.png",r18)};
            snow_.emplace(std::move(sprites),args[0] == "SnowH");
        } else return false;
    } else if (cmd == ".playVoice") {
        count(instruction,1,1); voice(args[0],r18);
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
            return true;
        }
        if (stop) play(channel,bgm ? "bgm" : "se",args[0],r18,false,0,0,out_speed>0 ? out_speed/2.0 : 0);
        else play(channel,bgm ? "bgm" : "se",args[0],r18,loop,gain,
                  fade(in_speed,gain),out_speed>0 ? out_speed/2.0 : 0);
    } else return false;
    return true;
}
}
