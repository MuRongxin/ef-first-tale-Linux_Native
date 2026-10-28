#include "save/checkpoint.hpp"
#include "save/json_file.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>

namespace ef {
namespace {
using J=nlohmann::json;
constexpr std::size_t max_bytes=16*1024*1024;
void require(bool v,const char* message) {if (!v) throw std::runtime_error(message);}
std::int64_t integer(const J& j,std::int64_t low=std::numeric_limits<std::int64_t>::min(),std::int64_t high=std::numeric_limits<std::int64_t>::max()) {
    require(j.is_number_integer() && (!j.is_number_unsigned() || j.get<std::uint64_t>()<=static_cast<std::uint64_t>(INT64_MAX)),"Save requires a signed integer");
    const auto v=j.get<std::int64_t>();require(v>=low && v<=high,"Save integer out of range");return v;
}
double number(const J& j,double low=-1e12,double high=1e12) {
    require(j.is_number(),"Save requires a number");const auto v=j.get<double>();require(std::isfinite(v) && v>=low && v<=high,"Save number out of range");return v;
}
std::string string(const J& j,std::size_t budget=1024*1024) {
    require(j.is_string(),"Save requires a string");auto v=j.get<std::string>();
    require(v.size()<=budget && v.find('\0')==std::string::npos,"Save text budget/NUL");(void)decode_utf8(v);return v;
}
bool boolean(const J& j) {require(j.is_boolean(),"Save requires a boolean");return j.get<bool>();}
void array(const J& j,std::size_t max) {require(j.is_array() && j.size()<=max,"Save array budget/type");}
void object(const J& j,std::initializer_list<const char*> keys) {
    require(j.is_object(),"Save requires an object");std::set<std::string> allowed;for (const auto* k:keys) allowed.insert(k);
    require(j.size()==allowed.size(),"Save has missing/unknown fields");for (const auto& [k,v]:j.items()) {(void)v;require(allowed.contains(k),"Unknown save field");}
}
J variables(const J& j) {
    require(j.is_object() && j.size()<=4096,"Save variable budget/type");
    for (const auto& [k,v]:j.items()) {(void)string(J(k),1024);(void)integer(v);}return j;
}
J binding(const Content& c) {
    const auto index=c.read_json(c.script_index());J scripts=J::object();
    for (const auto& [id,entry]:index.at("scripts").items()) {
        const auto hash=entry.at("json_sha256").get<std::string>();
        require(hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos,"Reimport scripts with JSON hashes before saving");scripts[id]=hash;
    }
    return {{"scripts",scripts},{"view",index.at("view")},{"message_ui",index.value("message_ui",J())}};
}
J message(const std::optional<Message>& m) {
    if (!m) return nullptr;
    return {{"script",m->script},{"voice",m->voice},{"name",m->name},{"text",m->text},{"id",m->id},{"source_line",m->source_line},{"plain_text",m->plain_text}};
}
std::optional<Message> message(const J& j) {
    if (j.is_null()) return {};
    object(j,{"script","voice","name","text","id","source_line","plain_text"});
    return Message{string(j.at("script"),1024),string(j.at("voice"),1024),string(j.at("name")),string(j.at("text")),
        static_cast<int>(integer(j.at("id"),0,65535)),static_cast<std::size_t>(integer(j.at("source_line"),1,INT64_MAX)),boolean(j.at("plain_text"))};
}
J layer(const Layer& l) {
    // Presentation layers do not contain message-layout flows or font overrides.
    return {{"kind",static_cast<int>(l.kind)},{"part",static_cast<int>(l.part)},{"asset",l.asset},{"text",l.text},{"slot",l.slot},
        {"screen",l.screen},{"overlay",l.overlay},{"hsv",l.hsv},{"trim",l.trim_bottom},{"geometry",{l.x,l.y,l.width,l.height,l.font_size}},
        {"color",l.color},{"source",l.source ? J(*l.source) : J()}};
}
Layer layer(const J& j,const Content& c) {
    object(j,{"kind","part","asset","text","slot","screen","overlay","hsv","trim","geometry","color","source"});Layer l;
    l.kind=static_cast<LayerKind>(integer(j.at("kind"),0,2));l.part=static_cast<StagePart>(integer(j.at("part"),0,3));
    l.asset=string(j.at("asset"),1024);l.text=string(j.at("text"));l.slot=string(j.at("slot"),1024);
    l.screen=string(j.at("screen"),1024);l.overlay=string(j.at("overlay"),1024);
    if (l.kind==LayerKind::image) (void)c.asset(l.asset);
    for (const auto& id:{l.screen,l.overlay}) if (!id.empty()) (void)c.asset(id);
    const auto& hsv=j.at("hsv");array(hsv,3);require(hsv.size()==3,"Invalid saved HSV");for (int n=0;n<3;++n) l.hsv[static_cast<std::size_t>(n)]=static_cast<int>(integer(hsv[n],n==0 ? -180 : -100,n==0 ? 180 : 100));
    l.trim_bottom=static_cast<int>(integer(j.at("trim"),0,1000000));
    const auto& g=j.at("geometry");array(g,5);require(g.size()==5,"Invalid saved geometry");
    l.x=static_cast<float>(number(g[0],-1e8,1e8));l.y=static_cast<float>(number(g[1],-1e8,1e8));
    l.width=static_cast<float>(number(g[2],0,1e8));l.height=static_cast<float>(number(g[3],0,1e8));l.font_size=static_cast<float>(number(g[4],1,1024));
    const auto& color=j.at("color");array(color,4);require(color.size()==4,"Invalid saved color");for (int n=0;n<4;++n) l.color[static_cast<std::size_t>(n)]=static_cast<float>(number(color[n],0,1));
    if (!j.at("source").is_null()) {const auto& a=j.at("source");array(a,4);require(a.size()==4,"Invalid saved atlas");std::array<float,4> region{};for (int n=0;n<4;++n) region[static_cast<std::size_t>(n)]=static_cast<float>(number(a[n],0,1e8));l.source=region;}
    if (l.kind==LayerKind::image) {
        const auto& a=c.asset(l.asset);require(l.trim_bottom<a.height && !(l.trim_bottom && l.source),"Invalid saved image trim");
        if (l.source) {const auto& r=*l.source;require(r[2]>0 && r[3]>0 && r[0]+r[2]<=static_cast<float>(a.width) && r[1]+r[3]<=static_cast<float>(a.height),"Saved image atlas outside canvas");}
    }
    return l;
}
J layers(const std::vector<Layer>& ls) {auto a=J::array();for (const auto& l:ls) a.push_back(layer(l));return a;}
std::vector<Layer> layers(const J& j,const Content& c) {array(j,512);std::vector<Layer> ls;for (const auto& l:j) ls.push_back(layer(l,c));return ls;}
void header(const J& j) {
    object(j,{"format","version","created_unix","binding","vm","presentation","message_ui"});
    require(j.at("format")=="ef.save" && integer(j.at("version"))==1,"Unsupported save format/version");
    (void)integer(j.at("created_unix"),0,INT64_MAX);
}
}

struct CheckpointCodec {
    static J mixer(const AudioMixer& m,const Content& c) {
        const std::lock_guard lock(m.mutex_);auto tracks=J::array();
        for (std::size_t n=0;n<m.slots_.size();++n) {const auto& s=m.slots_[n];if (!s.active) continue;
            require(!s.resource_id.empty(),"Active audio has no resource identity");
            tracks.push_back({{"slot",n},{"resource",s.resource_id},{"source_sha256",c.resources().at(s.resource_id).source_sha256},{"channel",static_cast<int>(s.channel)},{"position",s.position},
                {"gain",s.gain},{"target",s.target},{"gain_step",s.gain_step},{"pan",s.pan},{"ramp_left",s.ramp_left},{"loop",s.loop},{"stop_after_ramp",s.stop_after_ramp}});
        }
        return {{"rate",m.output_rate_},{"tracks",tracks}};
    }
    static void mixer(const J& j,Presentation& p,const Content& c) {
        object(j,{"rate","tracks"});require(integer(j.at("rate"))==p.mixer_.output_rate_,"Incompatible audio output rate");
        const auto& tracks=j.at("tracks");array(tracks,10);std::set<int> used;
        for (const auto& t:tracks) {
            object(t,{"slot","resource","source_sha256","channel","position","gain","target","gain_step","pan","ramp_left","loop","stop_after_ramp"});
            const auto index=static_cast<int>(integer(t.at("slot"),0,13));require(used.insert(index).second,"Duplicate saved audio slot");
            auto& s=p.mixer_.slots_[static_cast<std::size_t>(index)];s.channel=static_cast<AudioChannel>(integer(t.at("channel"),0,3));
            require(index>=4 || index==static_cast<int>(s.channel),"Saved audio slot/channel mismatch");
            s.resource_id=string(t.at("resource"),1024);const auto& r=c.resources().at(s.resource_id);
            const std::string kind=s.channel==AudioChannel::bgm ? "bgm" : s.channel==AudioChannel::voice ? "voice" : "se";
            require(r.kind==kind,"Saved audio resource kind mismatch");require(t.at("source_sha256")==r.source_sha256,"Saved audio resource has changed");s.clip=p.decoder_(c.resolve(r.path));
            require(s.clip && s.clip->frames()>0,"Saved audio is empty");s.position=number(t.at("position"),0,static_cast<double>(s.clip->frames()));
            require(s.position<static_cast<double>(s.clip->frames()),"Saved audio position past EOF");
            s.gain=number(t.at("gain"),0,1);s.target=number(t.at("target"),0,1);s.gain_step=number(t.at("gain_step"),-1,1);
            s.pan=static_cast<float>(number(t.at("pan"),-1,1));s.ramp_left=static_cast<std::uint64_t>(integer(t.at("ramp_left"),0,192000000000000LL));
            s.loop=boolean(t.at("loop"));s.stop_after_ramp=boolean(t.at("stop_after_ramp"));s.active=true;
            if (s.ramp_left) require(std::abs(s.gain+s.gain_step*static_cast<double>(s.ramp_left)-s.target)<1e-4,"Inconsistent saved audio envelope");
        }
    }
    static J effects(const Presentation& p) {
        J j={{"walking",p.walking_ ? J(p.walking_->elapsed_) : J()},{"snow",nullptr},{"cutin",nullptr}};
        if (p.cutin_) {const auto& c=*p.cutin_;j["cutin"]={{"image",layer(c.image_)},{"hold",c.hold_},{"state",c.state_},{"x",c.x_},{"rightward",c.rightward_},{"exiting",c.exiting_},{"factor",c.factor_},{"velocity",c.velocity_},{"elapsed",c.elapsed_},{"next_tick",c.next_tick_}};}
        if (p.snow_) {const auto& s=*p.snow_;J sprites=J::array(),particles=J::array();for (const auto& l:s.sprites_) sprites.push_back(layer(l));for (const auto& t:s.particles_) particles.push_back({t.x,t.y,t.vx,t.vy,t.sprite,t.active});
            j["snow"]={{"sprites",sprites},{"particles",particles},{"seed",s.seed_},{"horizontal",s.horizontal_},{"fading",s.fading_},{"finished",s.finished_},{"opacity",s.opacity_},{"time",s.time_},{"fade_ticks",s.fade_ticks_},{"ticks",s.ticks_}};
        }
        return j;
    }
    static void effects(const J& j,Presentation& p,const Content& c) {
        object(j,{"walking","snow","cutin"});
        if (!j.at("walking").is_null()) {p.walking_.emplace(60,-2);p.walking_->elapsed_=number(j.at("walking"),0,1e9);(void)p.walking_->layers(p.current_);}
        if (!j.at("cutin").is_null()) {const auto& a=j.at("cutin");object(a,{"image","hold","state","x","rightward","exiting","factor","velocity","elapsed","next_tick"});
            p.cutin_.emplace(layer(a.at("image"),c),0);auto& v=*p.cutin_;v.hold_=static_cast<int>(integer(a.at("hold"),-1000000,1000000));v.state_=static_cast<int>(integer(a.at("state"),0,2));v.x_=static_cast<int>(integer(a.at("x"),-800,800));
            v.rightward_=boolean(a.at("rightward"));v.exiting_=boolean(a.at("exiting"));v.factor_=number(a.at("factor"),1,100);v.velocity_=number(a.at("velocity"),-1e8,1e8);v.elapsed_=number(a.at("elapsed"),0,1e9);
            v.next_tick_=static_cast<std::uint64_t>(integer(a.at("next_tick"),1,100000000004LL));require(v.next_tick_>static_cast<std::uint64_t>(std::floor(v.elapsed_*100+1e-9)),"Inconsistent Cutin clock");
        }
        if (!j.at("snow").is_null()) {const auto& a=j.at("snow");object(a,{"sprites","particles","seed","horizontal","fading","finished","opacity","time","fade_ticks","ticks"});
            const auto sprites=layers(a.at("sprites"),c);require(sprites.size()==3,"Snow requires three sprites");p.snow_.emplace(std::array<Layer,3>{sprites[0],sprites[1],sprites[2]},boolean(a.at("horizontal")));auto& v=*p.snow_;
            v.seed_=static_cast<std::uint32_t>(integer(a.at("seed"),0,UINT32_MAX));v.fading_=boolean(a.at("fading"));v.finished_=boolean(a.at("finished"));v.opacity_=static_cast<int>(integer(a.at("opacity"),0,256));
            v.time_=number(a.at("time"),0,1e9);v.fade_ticks_=number(a.at("fade_ticks"),0,1e11);v.ticks_=static_cast<std::uint64_t>(integer(a.at("ticks"),0,100000000000LL));
            require(v.ticks_==static_cast<std::uint64_t>(std::floor(v.time_*100)),"Inconsistent snow clock");const auto& ts=a.at("particles");array(ts,50);require(ts.size()==50,"Snow requires 50 particles");
            for (std::size_t n=0;n<50;++n) {const auto& t=ts[n];array(t,6);require(t.size()==6,"Invalid snow particle");auto& q=v.particles_[n];q.x=number(t[0]);q.y=number(t[1]);q.vx=number(t[2],-100,100);q.vy=number(t[3],-100,100);q.sprite=static_cast<int>(integer(t[4],0,2));q.active=boolean(t[5]);}
        }
        require(static_cast<int>(p.walking_.has_value())+static_cast<int>(p.cutin_.has_value())+static_cast<int>(p.snow_.has_value())<=1,"Conflicting saved effects");
    }
    static J presentation(const Presentation& p) {
        auto batteries=J::array();for (const auto& l:p.finder_batteries_) batteries.push_back(l.asset.empty() ? J() : layer(l));
        J scroll=nullptr;if (p.scroll_) {const auto& s=*p.scroll_;scroll={{"layer",s.layer},{"geometry",{s.x,s.y,s.target_x,s.target_y}},{"elapsed",s.elapsed},{"speed",s.speed}};}
        return {{"layers",layers(p.current_)},{"pending_frame",p.pending_frame_ ? layer(*p.pending_frame_) : J()},{"frame",p.frame_ ? layer(*p.frame_) : J()},
            {"finder_batteries",batteries},{"pending_finder",p.pending_finder_},{"finder",p.finder_},{"finder_ticks",p.finder_ticks_},
            {"speed",p.speed_},{"transition_type",p.transition_type_},{"transition_mask",p.transition_mask_},{"panel",p.panel_},
            {"hsv",p.hsv_},{"screens",p.screens_},{"overlays",p.overlays_},{"background",p.background_ ? J(*p.background_) : J()},
            {"scroll",scroll},{"effects",effects(p)},{"playing_ids",p.playing_ids_},{"playing_loops",p.playing_loops_},{"audio",mixer(p.mixer_,p.content_)}};
    }
    static void presentation(const J& j,Presentation& p,const Content& c) {
        object(j,{"layers","pending_frame","frame","finder_batteries","pending_finder","finder","finder_ticks","speed","transition_type","transition_mask","panel","hsv","screens","overlays","background","scroll","effects","playing_ids","playing_loops","audio"});
        p.current_=layers(j.at("layers"),c);if (!j.at("pending_frame").is_null()) p.pending_frame_=layer(j.at("pending_frame"),c);if (!j.at("frame").is_null()) p.frame_=layer(j.at("frame"),c);
        const auto& bs=j.at("finder_batteries");array(bs,4);require(bs.size()==4,"Invalid finder batteries");for (std::size_t n=0;n<4;++n) if (!bs[n].is_null()) p.finder_batteries_[n]=layer(bs[n],c);
        p.pending_finder_=boolean(j.at("pending_finder"));p.finder_=boolean(j.at("finder"));if (p.finder_ || p.pending_finder_) for (const auto& b:bs) require(!b.is_null(),"Missing finder battery");
        p.finder_ticks_=number(j.at("finder_ticks"),0,3599900);p.speed_=static_cast<int>(integer(j.at("speed"),1,INT32_MAX));p.transition_type_=static_cast<int>(integer(j.at("transition_type"),0,17));
        require(p.transition_type_==0 || p.transition_type_==1 || p.transition_type_==2 || p.transition_type_==14 || p.transition_type_==16 || p.transition_type_==17,"Unsupported saved transition");
        p.transition_mask_=string(j.at("transition_mask"),1024);
        require((p.transition_type_==1)==!p.transition_mask_.empty(),"Saved transition mask/type disagree");
        require(p.transition_type_!=2 || p.speed_==20,"Unsupported saved type-2 speed");
        if (!p.transition_mask_.empty()) {const auto& mask=c.asset(p.transition_mask_);require(p.speed_<=255 && mask.width==800 && mask.height==600,"Invalid saved masked transition");}
        p.panel_=static_cast<int>(integer(j.at("panel"),0,5));
        const auto& hsv=j.at("hsv");array(hsv,2);require(hsv.size()==2,"Invalid stage HSV");for (std::size_t a=0;a<2;++a) {array(hsv[a],3);require(hsv[a].size()==3,"Invalid stage HSV");for (std::size_t b=0;b<3;++b) p.hsv_[a][b]=static_cast<int>(integer(hsv[a][b],b==0 ? -180 : -100,b==0 ? 180 : 100));}
        for (const auto* name:{"screens","overlays","playing_ids","playing_loops"}) {array(j.at(name),name[0]=='p' ? 4 : 3);require(j.at(name).size()==(name[0]=='p' ? 4U : 3U),"Invalid stage array");}
        for (std::size_t n=0;n<3;++n) {p.screens_[n]=string(j.at("screens")[n],1024);p.overlays_[n]=string(j.at("overlays")[n],1024);for (const auto& id:{p.screens_[n],p.overlays_[n]}) if (!id.empty()) (void)c.asset(id);}
        for (std::size_t n=0;n<4;++n) {p.playing_ids_[n]=string(j.at("playing_ids")[n],1024);p.playing_loops_[n]=boolean(j.at("playing_loops")[n]);if (!p.playing_ids_[n].empty()) (void)c.resources().at(p.playing_ids_[n]);}
        if (!j.at("background").is_null()) {require(!p.current_.empty(),"Background without stage layers");p.background_=static_cast<std::size_t>(integer(j.at("background"),0,static_cast<std::int64_t>(p.current_.size()-1)));}
        if (!j.at("scroll").is_null()) {const auto& s=j.at("scroll");object(s,{"layer","geometry","elapsed","speed"});require(!p.current_.empty(),"Scroll without stage layers");array(s.at("geometry"),4);require(s.at("geometry").size()==4,"Invalid scroll geometry");
            Presentation::Scroll v;v.layer=static_cast<std::size_t>(integer(s.at("layer"),0,static_cast<std::int64_t>(p.current_.size()-1)));const auto& g=s.at("geometry");v.x=static_cast<float>(number(g[0],-1e8,1e8));v.y=static_cast<float>(number(g[1],-1e8,1e8));v.target_x=static_cast<float>(number(g[2],-1e8,1e8));v.target_y=static_cast<float>(number(g[3],-1e8,1e8));v.elapsed=number(s.at("elapsed"),0,1e9);v.speed=number(s.at("speed"),1,2147483648.0);p.scroll_=v;
        }
        effects(j.at("effects"),p,c);mixer(j.at("audio"),p,c);
        for (std::size_t n=0;n<4;++n) if (p.mixer_.slots_[n].active) require(p.playing_ids_[n]==p.mixer_.slots_[n].resource_id && p.playing_loops_[n]==p.mixer_.slots_[n].loop,"Saved channel state disagrees with mixer");
        ++p.revision_;
    }
    static std::optional<Message> checked_message(const J& j,const Content& c,char variant) {
        auto saved=message(j);if (!saved) return {};
        const auto instructions=ScriptLibrary(c).parse(saved->script);bool found=false;
        for (const auto& i:instructions) if (i.line==saved->source_line && i.command==".message" && (!i.variant || i.variant==variant)) {
            const Message expected{i.script,i.args[1],i.args[2],i.args[3],std::stoi(i.args[0]),i.line,i.events.has_value()};
            require(message(std::optional<Message>(expected))==j,"Saved dialogue differs from packaged script");found=true;
        }
        require(found,"Saved dialogue anchor no longer exists");return saved;
    }
    static J runtime(const ScriptRuntime& r) {
        require(r.pc_>0 && r.pc_<=r.program_->instructions.size(),"Save has no resume anchor");const auto& anchor=r.program_->instructions[r.pc_-1];
        return {{"program",r.program_->id},{"variant",std::string(1,r.options_.variant)},{"state",r.state_==ScriptState::choices ? "choices" : "message"},
            {"anchor",{{"script",anchor.script},{"source_line",anchor.line},{"dialogue_id",r.state_==ScriptState::message ? J(r.message_->id) : J()}}},
            {"locals",r.variables_},{"globals",r.globals_},{"controls",r.controls_enabled_},{"text_speed",r.options_.characters_per_second},
            {"message",message(r.message_)}};
    }
    static void runtime(const J& j,ScriptRuntime& r,const Content& c) {
        object(j,{"program","variant","state","anchor","locals","globals","controls","text_speed","message"});
        const auto variant=string(j.at("variant"),1);require(variant=="e" || variant=="j","Invalid saved timing variant");
        require(r.program_->id==string(j.at("program"),1024) && r.options_.variant==variant[0],"Candidate session does not match save metadata");
        const auto state=string(j.at("state"),16);require(state=="message" || state=="choices","Invalid saved pause state");r.state_=state=="message" ? ScriptState::message : ScriptState::choices;
        const auto& a=j.at("anchor");object(a,{"script","source_line","dialogue_id"});const auto script=string(a.at("script"),1024);const auto line=static_cast<std::size_t>(integer(a.at("source_line"),1,INT64_MAX));
        std::optional<std::size_t> index;
        for (std::size_t n=0;n<r.program_->instructions.size();++n) {const auto& i=r.program_->instructions[n];
            if (i.script!=script || i.line!=line) continue;
            if (state=="message") {if (i.command!=".message" || std::stoll(i.args[0])!=integer(a.at("dialogue_id"),0,65535)) continue;}
            else if (i.command!=".select" || !a.at("dialogue_id").is_null()) continue;
            require(!index.has_value(),"Ambiguous saved script anchor");index=n;
        }
        require(index.has_value(),"Saved script anchor no longer exists");r.pc_=*index+1;r.variables_=variables(j.at("locals")).get<std::map<std::string,std::int64_t>>();r.globals_=variables(j.at("globals")).get<std::map<std::string,std::int64_t>>();
        r.controls_enabled_=boolean(j.at("controls"));require(state!="message" || r.controls_enabled_,"Invalid saved control state");r.options_.characters_per_second=number(j.at("text_speed"),0,1000);
        r.message_=checked_message(j.at("message"),c,variant[0]);r.message_serial_=r.message_ ? 1 : 0;r.dialogue_.reset();r.message_plain_=r.message_ && r.message_->plain_text;
        if (r.message_) {
            if (r.message_plain_) r.dialogue_.emplace(std::vector<DialogueEvent>{{DialogueKind::text,r.message_->text}},r.options_.characters_per_second);
            else r.dialogue_.emplace(r.message_->text,r.options_.characters_per_second);
            auto& d=*r.dialogue_;d.cursor_=d.tokens_.size();d.displayed_=d.full_text_;d.pause_=d.credit_=0;d.voice_wait_=d.automatic_=false;
        }
        r.choices_.clear();const auto& instruction=r.program_->instructions[*index];
        if (state=="message") require(r.message_ && r.message_->script==script && r.message_->source_line==line,"Saved message and resume anchor disagree");
        else for (const auto& option:instruction.args) {const auto split=option.rfind(':');r.choices_.push_back({option.substr(0,split),option.substr(split+1)});}
    }
    static J ui(const MessageUI* p) {
        if (!p) return nullptr;
        auto labels=J::array();for (const auto& l:p->page_labels_) labels.push_back({{"text",l.text},{"byte_offset",l.byte_offset}});
        return {{"mode",p->mode_},{"page_message",message(p->page_message_)},{"active_message",message(p->active_message_)},
            {"prefix",p->prefix_},{"current_text",p->current_text_},{"labels",labels}};
    }
    static void ui(const J& j,MessageUI* p,const Content& c,char variant) {
        require(j.is_null()==(p==nullptr),"Saved message layout is unavailable");if (!p) return;
        object(j,{"mode","page_message","active_message","prefix","current_text","labels"});const auto mode=static_cast<int>(integer(j.at("mode"),0,5));p->set_mode(mode);
        p->page_message_=checked_message(j.at("page_message"),c,variant);p->active_message_=checked_message(j.at("active_message"),c,variant);
        p->prefix_=string(j.at("prefix"));p->current_text_=string(j.at("current_text"));const auto prefix=decode_utf8(p->prefix_);p->prefix_characters_=prefix.size();
        require(p->prefix_.size()+p->current_text_.size()<=1024*1024 && prefix.size()+decode_utf8(p->current_text_).size()<=65536,"Saved NVL page budget");
        if (p->page_message_) require(p->current_text_==(p->page_message_->plain_text ? p->page_message_->text : Dialogue(p->page_message_->text,0).full_text()),"Saved NVL text and message differ");
        const auto& labels=j.at("labels");array(labels,1024);std::size_t last=0,bytes=0;std::set<std::size_t> boundaries{p->prefix_.size()};
        for (std::size_t n=0;n<p->prefix_.size();++n) if ((static_cast<unsigned char>(p->prefix_[n])&0xc0)!=0x80) boundaries.insert(n);
        p->page_labels_.clear();for (const auto& l:labels) {object(l,{"text","byte_offset"});auto text=string(l.at("text"));bytes+=text.size();require(bytes<=1024*1024,"Saved speaker budget");const auto offset=static_cast<std::size_t>(integer(l.at("byte_offset"),0,static_cast<std::int64_t>(p->prefix_.size())));
            require(offset>=last && boundaries.contains(offset),"Invalid saved speaker anchor");last=offset;p->page_labels_.push_back({std::move(text),offset});}
    }
    static J capture(const Content& c,const ScriptRuntime& r,const Presentation& p,const MessageUI* ui_ptr) {
        require(!r.options_.text_preview,"Text preview sessions cannot be saved");
        require((r.state_==ScriptState::message && r.controls_enabled_ && r.message_ && r.message_complete() && !r.message_automatic()) || r.state_==ScriptState::choices,"Wait for a complete dialogue or choice before saving");
        require(!p.busy() && (!ui_ptr || !ui_ptr->busy()),"Wait for the current transition or movie before saving");
        return {{"format","ef.save"},{"version",1},{"created_unix",std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"binding",binding(c)},{"vm",runtime(r)},{"presentation",presentation(p)},{"message_ui",ui(ui_ptr)}};
    }
    static void restore(const Content& c,const J& j,ScriptRuntime& r,Presentation& p,MessageUI* u) {
        header(j);require(j.at("binding")==binding(c),"Save is incompatible with this script package or message layout");
        // This API is used only for fresh candidates, never for the currently attached mixer.
        require(r.state_==ScriptState::running && r.pc_==0 && !p.realtime_ && p.current_.empty(),"Restore requires a fresh, unattached candidate");
        runtime(j.at("vm"),r,c);presentation(j.at("presentation"),p,c);ui(j.at("message_ui"),u,c,r.options_.variant);
        if (u) {
            require(u->mode_==p.panel_,"Saved panel/layout mode disagree");
            if (r.state_==ScriptState::message) require(u->active_message_.has_value(),"Saved dialogue has no active message UI");
            if (u->active_message_) require(message(u->active_message_)==message(r.message_),"Saved UI/runtime messages disagree");
            if (u->mode_==2) require(u->page_message_.has_value()==u->active_message_.has_value() && message(u->page_message_)==message(u->active_message_),"Saved NVL page/message disagree");
            if (!u->page_message_) require(u->prefix_.empty() && u->current_text_.empty() && u->page_labels_.empty(),"Saved page text has no page anchor");
        }
        (void)p.current();(void)p.effects();
    }
};

nlohmann::json capture_checkpoint(const Content& c,const ScriptRuntime& r,const Presentation& p,const MessageUI* u) {return CheckpointCodec::capture(c,r,p,u);}
void restore_checkpoint(const Content& c,const nlohmann::json& j,ScriptRuntime& r,Presentation& p,MessageUI* u) {CheckpointCodec::restore(c,j,r,p,u);}
std::filesystem::path default_checkpoint_path() {
    if (const auto* data=std::getenv("XDG_DATA_HOME");data && std::filesystem::path(data).is_absolute()) return std::filesystem::path(data)/"ef-native/saves/quick.json";
    if (const auto* home=std::getenv("HOME");home && std::filesystem::path(home).is_absolute()) return std::filesystem::path(home)/".local/share/ef-native/saves/quick.json";
    throw std::runtime_error("Cannot locate save directory; use --save-file FILE");
}
nlohmann::json read_checkpoint(const std::filesystem::path& path) {
    auto result=read_json_file(path,max_bytes);header(result);return result;
}
void write_checkpoint(const std::filesystem::path& path,const nlohmann::json& j) {
    header(j);atomic_write_json(path,j,max_bytes);
}
nlohmann::json checkpoint_metadata(const Content& c,const nlohmann::json& j) {
    header(j);const auto& vm=j.at("vm");
    const auto program=string(vm.at("program"),1024);require(!program.empty(),"Missing saved program");
    const auto variant=string(vm.at("variant"),1);require(variant=="e" || variant=="j","Invalid saved timing variant");
    const auto state=string(vm.at("state"),16);require(state=="message" || state=="choices","Invalid saved pause state");
    const auto& a=vm.at("anchor");object(a,{"script","source_line","dialogue_id"});
    (void)string(a.at("script"),1024);(void)integer(a.at("source_line"),1,INT64_MAX);
    if (state=="message") (void)integer(a.at("dialogue_id"),0,65535);else require(a.at("dialogue_id").is_null(),"Invalid choice anchor");
    const auto m=message(vm.at("message"));std::string excerpt=m ? m->text : "";
    if (excerpt.size()>192) {std::size_t n=192;while (n && (static_cast<unsigned char>(excerpt[n])&0xc0)==0x80) --n;excerpt.resize(n);}
    return {{"created_unix",j.at("created_unix")},{"program",program},{"variant",variant},{"pause",state},{"anchor",a},
        {"speaker",m ? MessageUI::display_name(m->name) : ""},{"excerpt",excerpt},{"compatible",j.at("binding")==binding(c)}};
}
}
