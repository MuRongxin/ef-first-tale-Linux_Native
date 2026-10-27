#include "scene/presentation.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using Json=nlohmann::json;
struct Options {
    std::filesystem::path content="content",report;
    std::string entry="scr/test.sc",mode="control";
    char variant='e';
    bool complete_movies{},overlay_patches{};
    std::size_t max_paths=256,max_depth=16,max_updates=1000000;
};
std::size_t positive(const std::string& s) {
    std::size_t value{};const auto r=std::from_chars(s.data(),s.data()+s.size(),value);
    if (r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || !value || value>10000000) throw std::runtime_error("Invalid audit budget");
    return value;
}
Options options(int argc,char** argv) {
    Options o;
    for (int n=1;n<argc;++n) {
        const std::string arg=argv[n];
        const auto value=[&]() {if (++n==argc) throw std::runtime_error("Missing value: "+arg);return std::string(argv[n]);};
        if (arg=="--content") o.content=value();else if (arg=="--report") o.report=value();
        else if (arg=="--entry") o.entry=value();else if (arg=="--mode") o.mode=value();
        else if (arg=="--variant") {const auto v=value();if (v!="e" && v!="j") throw std::runtime_error("Invalid variant");o.variant=v[0];}
        else if (arg=="--max-paths") o.max_paths=positive(value());
        else if (arg=="--max-depth") o.max_depth=positive(value());
        else if (arg=="--max-updates") o.max_updates=positive(value());
        else if (arg=="--complete-movies") o.complete_movies=true;
        else if (arg=="--overlay-patches") o.overlay_patches=true;
        else throw std::runtime_error("Unknown argument: "+arg);
    }
    if (o.report.empty() || (o.mode!="control" && o.mode!="presentation")) throw std::runtime_error("Require --report FILE and --mode control|presentation");
    return o;
}
// A bounded audit-only cache. Production playback retains its existing decoder.
class Clips {
    struct Item {std::shared_ptr<const ef::AudioClip> clip;std::size_t bytes{},used{};};
    std::map<std::filesystem::path,Item> items_;
    std::size_t bytes_{},clock_{};
    static constexpr std::size_t limit=128*1024*1024;
public:
    std::size_t decodes{},hits{};
    std::shared_ptr<const ef::AudioClip> get(const std::filesystem::path& p) {
        if (auto it=items_.find(p);it!=items_.end()) {++hits;it->second.used=++clock_;return it->second.clip;}
        auto clip=ef::decode_audio(p);++decodes;const auto bytes=clip->samples.size()*sizeof(float);
        if (bytes<=limit) {
            while (bytes_+bytes>limit && !items_.empty()) {
                auto oldest=std::min_element(items_.begin(),items_.end(),[](const auto& a,const auto& b) {return a.second.used<b.second.used;});
                bytes_-=oldest->second.bytes;items_.erase(oldest);
            }
            items_.emplace(p,Item{clip,bytes,++clock_});bytes_+=bytes;
        }
        return clip;
    }
};
struct State {
    ef::ScriptRuntime vm;
    std::optional<ef::Presentation> host;
    Json choices=Json::array(),scripts=Json::array(),movies=Json::array();
    std::optional<ef::Instruction> last;
    std::size_t messages{},updates{};
    std::map<std::string,std::size_t> messages_by_script;
    State(ef::ScriptRuntime runtime,const ef::Content& content,Clips& clips,bool strict) : vm(std::move(runtime)) {
        if (strict) host.emplace(content,[&clips](const auto& p) {return clips.get(p);});
    }
};
class Audit {
    Options o_;
    ef::Content content_;
    ef::ScriptLibrary library_;
    Clips clips_;
    std::map<std::string,std::shared_ptr<const ef::Program>> programs_;
    std::set<std::string> locations_,edges_,entered_;
    std::ofstream progress_;
    std::size_t observed_messages_{};
    Json choices_=Json::object(),paths_=Json::array();
    bool limited_{};
    std::string select(std::string id) const {
        if (o_.overlay_patches && id.starts_with("scr/")) {
            const auto patch="r18_"+id;if (library_.entries().contains(patch)) return patch;
        }
        return id;
    }
    std::string resolve(const std::string& from,const std::string& to) const {return select(library_.resolve(from,to));}
    std::shared_ptr<const ef::Program> load(const std::string& requested) {
        const auto id=select(requested);
        if (const auto it=programs_.find(id);it!=programs_.end()) return it->second;
        auto p=library_.compile(id,o_.variant);programs_.emplace(id,p);return p;
    }
    void attach(State& s) {
        if (s.host) s.host->set_movie_supported(o_.complete_movies);
        s.vm.set_host({
            [&](const ef::Instruction& i) {
                if (!s.host) return true; // Explicit control-only scope; never reported as presentation validation.
                const bool handled=s.host->command(i);
                if (s.host->movie_active()) {
                    const auto& m=*s.host->movie_request();
                    s.movies.push_back({{"source",i.location()},{"asset",m.asset},{"completion","simulated"},{"skippable",m.skippable}});
                    s.host->finish_movie();
                }
                return handled;
            },
            [&](const ef::Message& m) {if (s.host) s.host->message(m);},
            [&](double seconds) {if (s.host) {s.host->update(seconds);s.vm.update_local_variable("finderCounter",s.host->finder_counter());}},
            [&] {return s.host && s.host->busy();},
            [&] {return s.host && s.host->mixer().playing(ef::AudioChannel::voice);},
            [&](const std::string& name,std::int64_t value) {if (s.host) s.host->local_assignment(name,value);},
            [&](const ef::Instruction& i) {
                s.last=i;locations_.insert(i.location());entered_.insert(i.script);
                if (s.scripts.empty() || s.scripts.back()!=i.script) s.scripts.push_back(i.script);
                if (i.command==".message") {
                    ++s.messages;++s.messages_by_script[i.script];
                    if (++observed_messages_%2000==0) std::cerr<<"progress messages="<<observed_messages_<<" "<<i.location()<<std::endl;
                }
                if (i.command==".chain") edges_.insert(i.script+" -> "+resolve(i.script,i.args[0]));
                if (i.command==".select") {
                    Json opts=Json::array();for (const auto& a:i.args) {const auto p=a.rfind(':');opts.push_back({{"text",a.substr(0,p)},{"label",a.substr(p+1)}});}
                    choices_[i.location()]=opts;
                }
                if (i.command==".if") {
                    const auto left=s.vm.variable(i.args[0]);const auto right=std::stoll(i.args[2]);const auto& op=i.args[1];
                    const bool taken=op=="==" ? left==right : op=="!=" ? left!=right : op=="<" ? left<right : op=="<=" ? left<=right : op==">" ? left>right : left>=right;
                    edges_.insert(i.location()+" -> "+(taken ? "taken:"+i.args[3] : "fallthrough"));
                }
            }});
    }
    void finish(const State& s,const std::string& status,const std::string& error={}) {
        paths_.push_back({{"status",status},{"choices",s.choices},{"messages",s.messages},{"updates",s.updates},
            {"scripts",s.scripts},{"messages_by_script",s.messages_by_script},{"last",s.last ? s.last->location() : ""},{"state",s.vm.inspect()},
            {"movies",s.movies},{"error",error},{"diagnostics",s.vm.diagnostics()}});
        progress_<<paths_.back().dump()<<std::endl;
        if (!progress_) throw std::runtime_error("Cannot write route progress");
        std::cerr<<"path "<<paths_.size()<<" "<<status<<" messages="<<s.messages<<" "<<(error.empty() ? paths_.back().at("last").get<std::string>() : error)<<std::endl;
    }
public:
    explicit Audit(Options o) : o_(std::move(o)),content_(o_.content),library_(content_) {
        if (!o_.report.parent_path().empty()) std::filesystem::create_directories(o_.report.parent_path());
        progress_.open(o_.report.string()+".paths.jsonl");
        if (!progress_) throw std::runtime_error("Cannot write route progress");
    }
    int run() {
        ef::ScriptRuntime root([this](const auto& id) {return load(id);},[this](const auto& from,const auto& to) {return resolve(from,to);},select(o_.entry),{o_.variant,false,0,0});
        std::vector<std::unique_ptr<State>> pending;
        pending.push_back(std::make_unique<State>(std::move(root),content_,clips_,o_.mode=="presentation"));
        while (!pending.empty()) {
            if (paths_.size()>=o_.max_paths) {limited_=true;break;}
            auto state=std::move(pending.back());pending.pop_back();auto& s=*state;attach(s);
            try {
                while (true) {
                    if (++s.updates>o_.max_updates) {limited_=true;finish(s,"budget","Per-path update budget exhausted");break;}
                    s.vm.update(1);
                    if (s.host) {(void)s.host->current();(void)s.host->effects();}
                    if (s.vm.state()==ef::ScriptState::ended) {finish(s,"ended");break;}
                    if (s.vm.state()==ef::ScriptState::choices) {
                        if (s.choices.size()>=o_.max_depth) {limited_=true;finish(s,"budget","Choice depth budget exhausted");break;}
                        for (std::size_t n=s.vm.choices().size();n>0;--n) {
                            auto child=std::make_unique<State>(s);const auto& choice=s.vm.choices()[n-1];
                            child->choices.push_back({{"source",s.last->location()},{"index",n-1},{"text",choice.text},{"label",choice.label}});
                            child->vm.choose(n-1);pending.push_back(std::move(child));
                        }
                        break;
                    }
                    if (s.vm.state()==ef::ScriptState::message) {s.vm.advance();s.vm.advance();}
                }
            } catch (const std::exception& e) {finish(s,"blocked",e.what());}
        }
        Json unreachable=Json::array();for (const auto& [id,path]:library_.entries()) {(void)path;if (!entered_.contains(id)) unreachable.push_back(id);}
        const auto blocked=std::count_if(paths_.begin(),paths_.end(),[](const Json& p) {return p.at("status")!="ended";});
        Json report{{"version",1},{"mode",o_.mode},{"entry",o_.entry},{"variant",std::string(1,o_.variant)},
            {"script_index",content_.script_index()},{"overlay_patches",o_.overlay_patches},{"movie_completion",o_.complete_movies ? "simulated" : "unsupported"},
            {"scope",o_.mode=="presentation" ? "Native VM + Presentation + decoded audio; no GPU/image pixels, UI or movie decoding" : "Native VM only; presentation commands mocked"},
            {"verdict",limited_ ? "INCOMPLETE" : blocked ? "BLOCKED" : "PASS"},{"limits",{{"paths",o_.max_paths},{"depth",o_.max_depth},{"updates_per_path",o_.max_updates}}},
            {"pending_states",pending.size()},{"compiled_scripts",programs_.size()},{"entered_scripts",entered_},{"unentered_scripts",unreachable},{"attempted_locations",locations_},
            {"edges",edges_},{"choice_sites",choices_},{"paths",paths_},{"decoded_audio",clips_.decodes},{"audio_cache_hits",clips_.hits}};
        if (!o_.report.parent_path().empty()) std::filesystem::create_directories(o_.report.parent_path());
        std::ofstream file(o_.report);if (!file) throw std::runtime_error("Cannot write route audit report");file<<report.dump(2)<<'\n';file.close();if (!file) throw std::runtime_error("Cannot finish route audit report");
        std::cout<<report.at("verdict")<<" paths="<<paths_.size()<<" blocked="<<blocked<<" locations="<<locations_.size()<<" report="<<o_.report<<'\n';
        return limited_ ? 3 : blocked ? 2 : 0;
    }
};
}
int main(int argc,char** argv) {
    try {return Audit(options(argc,argv)).run();}
    catch (const std::exception& e) {std::cerr<<"route audit: "<<e.what()<<'\n';return 1;}
}
