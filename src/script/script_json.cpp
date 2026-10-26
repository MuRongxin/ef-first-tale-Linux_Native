#include "script/script.hpp"
#include "script_ops.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace ef {
namespace {
using Json=nlohmann::json;
const Json& operations() { static const auto value=Json::parse(script_operations);return value; }
void fields(const Json& j,const std::set<std::string>& allowed) {
    if (!j.is_object()) throw std::runtime_error("Expected JSON object");
    for (const auto& [name,value] : j.items()) { (void)value;if (!allowed.contains(name)) throw std::runtime_error("Unknown JSON field: "+name); }
}
std::int64_t int64(const Json& j) {
    if (!j.is_number_integer() || (j.is_number_unsigned() && j.get<std::uint64_t>()>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
        throw std::runtime_error("Expected signed 64-bit JSON integer");
    return j.get<std::int64_t>();
}
std::string string(const Json& j) {
    if (!j.is_string()) throw std::runtime_error("Expected JSON string");
    auto s=j.get<std::string>();(void)decode_utf8(s);
    if (s.find('\0')!=std::string::npos) throw std::runtime_error("NUL in script JSON string");
    return s;
}
bool boolean(const Json& j) { if (!j.is_boolean()) throw std::runtime_error("Expected JSON boolean");return j.get<bool>(); }
std::string atom(const Json& j,const std::string& type) {
    if (j.is_null() && type=="asset") return "*";
    if (type=="integer" || type=="default_integer") return std::to_string(int64(j));
    if (type=="flag" || type=="loop" || type=="movie_flag") return boolean(j) ? "t" : "f";
    return string(j);
}
std::string text(const Json& p,const char* key) { return string(p.at(key)); }
std::string num(const Json& p,const char* key) { return std::to_string(int64(p.at(key))); }
std::string asset(const Json& p,const char* key) { return atom(p.at(key),"asset"); }
}
std::vector<Instruction> script_json(const nlohmann::json& document,const std::string& id) {
    fields(document,{"format","version","id","instructions"});
    if (document.at("format")!="ef.script" || int64(document.at("version"))!=1 || string(document.at("id"))!=id)
        throw std::runtime_error("Unsupported script JSON format/version or mismatched ID");
    const auto& nodes=document.at("instructions");
    if (!nodes.is_array() || nodes.size()>200000) throw std::runtime_error("Invalid JSON instruction array/budget");
    std::vector<Instruction> out;std::size_t last_line=0;
    for (const auto& node : nodes) {
        fields(node,{"op","params","source_line","variant","colon_variant"});
        const auto line=int64(node.at("source_line"));
        if (line<=0 || static_cast<std::uint64_t>(line)<=last_line) throw std::runtime_error("Script source lines must increase");
        Instruction i;i.script=id;i.line=static_cast<std::size_t>(line);last_line=i.line;
        const auto variant=node.contains("variant") ? string(node.at("variant")) : "";
        if (!variant.empty() && variant!="e" && variant!="j") throw std::runtime_error("Invalid JSON timing variant");
        i.variant=variant.empty() ? 0 : variant[0];
        i.colon_variant=node.contains("colon_variant") && boolean(node.at("colon_variant"));
        if (i.colon_variant && variant.empty()) throw std::runtime_error("Colon provenance requires a timing variant");
        const auto op=string(node.at("op"));const auto& p=node.at("params");
        if (!p.is_object()) throw std::runtime_error("Script params must be an object");
        if (op=="unsupported") {
            fields(p,{"command","arguments","raw","reason"});i.command=text(p,"command");i.raw=text(p,"raw");
            (void)text(p,"reason");
            if (!p.at("arguments").is_array()) throw std::runtime_error("Unsupported arguments must be an array");
            for (const auto& a:p.at("arguments")) i.args.push_back(string(a));
            i.unsupported=true;out.push_back(std::move(i));continue;
        }
        const Json* rule=nullptr;
        for (const auto& [command,spec]:operations().items()) if (spec.at("op")==op) {i.command=command;rule=&spec;break;}
        if (!rule) throw std::runtime_error("Unknown script JSON operation: "+op);
        auto& a=i.args;
        if (!rule->value("special",false)) {
            const auto& definitions=rule->at("fields");std::set<std::string> allowed;
            auto count=rule->at("minimum").get<std::size_t>();
            for (std::size_t n=0;n<definitions.size();++n) {
                const auto name=definitions[n].at("name").get<std::string>();allowed.insert(name);
                if (p.contains(name)) count=std::max(count,n+1);
            }
            fields(p,allowed);
            for (std::size_t n=0;n<count;++n) {
                const auto& f=definitions[n];const auto name=f.at("name").get<std::string>();
                if (!p.contains(name) && n<rule->at("minimum").get<std::size_t>()) throw std::runtime_error("Missing JSON parameter: "+name);
                a.push_back(atom(p.contains(name) ? p.at(name) : f.at("default"),f.at("type").get<std::string>()));
            }
        } else if (op=="dialogue") {
            fields(p,{"id","voice","speaker","events"});
            const auto l0=int64(p.at("id"));if (l0<0 || l0>65535) throw std::runtime_error("Dialogue L0 out of range");
            a={num(p,"id"),text(p,"voice"),text(p,"speaker"),""};
            if (!p.at("events").is_array() || p.at("events").size()>65536) throw std::runtime_error("Invalid dialogue event budget");
            i.events.emplace();
            for (const auto& e:p.at("events")) {
                const auto kind=text(e,"type");DialogueEvent event;
                if (kind=="text") {fields(e,{"type","value"});event.kind=DialogueKind::text;event.text=text(e,"value");a[3]+=event.text;}
                else if (kind=="pause") {
                    fields(e,{"type","ticks"});const auto ticks=int64(e.at("ticks"));
                    if (ticks<0 || ticks>1000000) throw std::runtime_error("Dialogue pause out of range");
                    event.kind=DialogueKind::pause;event.ticks=static_cast<int>(ticks);
                } else {
                    fields(e,{"type"});
                    if (kind=="auto_advance") event.kind=DialogueKind::automatic;
                    else if (kind=="wait_voice") event.kind=DialogueKind::voice;
                    else throw std::runtime_error("Unknown dialogue event: "+kind);
                }
                i.events->push_back(std::move(event));
            }
            // Validate text size, Unicode and glyph/event budget before execution.
            (void)Dialogue(*i.events,0);
        } else if (op=="stage") {
            fields(p,{"frame","background","actors"});const auto& f=p.at("frame");const auto& b=p.at("background");
            fields(f,{"asset","x","y"});fields(b,{"asset","crop_x","crop_y"});a.push_back(asset(f,"asset"));
            if (f.contains("x") || f.contains("y")) {a.push_back(num(f,"x"));a.push_back(num(f,"y"));}
            a.push_back(asset(b,"asset"));a.push_back(num(b,"crop_x"));a.push_back(num(b,"crop_y"));
            if (!p.at("actors").is_array() || p.at("actors").size()>10) throw std::runtime_error("Stage actor budget exceeded");
            for (const auto& actor:p.at("actors")) {
                fields(actor,{"asset","center_x","bottom_trim"});a.push_back(asset(actor,"asset"));
                a.push_back(num(actor,"center_x")+(actor.contains("bottom_trim") ? ","+num(actor,"bottom_trim") : ""));
            }
        } else if (op=="screen" || op=="overlay") {
            fields(p,{"frame","background","foreground"});if (p.contains("frame")) a.push_back(asset(p,"frame"));
            a.push_back(asset(p,"background"));a.push_back(asset(p,"foreground"));
        } else if (op=="color_filter") {
            fields(p,{"background","foreground"});
            for (const auto* key:{"background","foreground"}) {
                const auto& v=p.at(key);if (!v.is_array() || v.size()!=3) throw std::runtime_error("HSV needs three integers");
                for (const auto& n:v) a.push_back(std::to_string(int64(n)));
            }
        } else if (op=="set" || op=="set_global") {
            fields(p,{"name","value","from_variable","operator"});a={text(p,"name"),"="};
            if (p.contains("from_variable") || p.contains("operator")) {a.push_back(text(p,"from_variable"));a.push_back(text(p,"operator"));}
            a.push_back(num(p,"value"));
        } else if (op=="branch") {
            fields(p,{"variable","comparison","value","label"});a={text(p,"variable"),text(p,"comparison"),num(p,"value"),text(p,"label")};
        } else if (op=="choices") {
            fields(p,{"options"});if (!p.at("options").is_array() || p.at("options").size()>256) throw std::runtime_error("Invalid choices array");
            for (const auto& choice:p.at("options")) {fields(choice,{"text","label"});const auto label=text(choice,"label");if (label.find(':')!=std::string::npos) throw std::runtime_error("Choice label cannot contain a colon");a.push_back(text(choice,"text")+":"+label);}
        } else if (op=="effect") {
            const auto name=text(p,"name");a={name};
            if (p.size()==1) fields(p,{"name"});
            else if (name=="Cutin") {
                fields(p,{"name","asset","hold_ticks","rate"});a.push_back(asset(p,"asset"));a.push_back(num(p,"hold_ticks"));if (p.contains("rate")) a.push_back(num(p,"rate"));
            } else if (name=="WScroll2") {fields(p,{"name","objects","period","speed"});a.push_back(asset(p,"objects"));a.push_back(num(p,"period"));a.push_back(num(p,"speed"));}
            else throw std::runtime_error("Unmapped effect JSON fields");
        } else throw std::runtime_error("Unmapped script JSON operation");
        out.push_back(std::move(i));
    }
    return out;
}
std::vector<Instruction> read_script_json(std::string_view text,const std::string& id) {
    if (text.size()>16*1024*1024) throw std::runtime_error("Script JSON exceeds 16 MiB");
    std::vector<std::set<std::string>> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json& value) {
        if (depth>64) throw std::runtime_error("Script JSON nesting exceeds 64");
        if (event==Json::parse_event_t::object_start) keys.emplace_back();
        else if (event==Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate JSON key");
        else if (event==Json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    return script_json(Json::parse(text,callback),id);
}
}
