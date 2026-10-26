#include "script/script.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
using Json=nlohmann::json;
std::string canonical(std::string s) {
    std::int64_t n{};const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    return r.ec==std::errc{} && r.ptr==s.data()+s.size() ? std::to_string(n) : s;
}
Json signature(const ef::Instruction& i) {
    auto args=i.args;
    if (i.command==".playSE" || i.command==".playSE2") {
        if (args.size()>1) args[1]=args[1].starts_with('t') ? "t" : "f";
    } else if (i.command==".movie" && args.size()>4) args[4]=!args[4].empty() && (args[4][0]=='f' || args[4][0]=='F' || args[4][0]=='0') ? "f" : "t";
    if (i.command==".playBGM" || i.command==".playSE" || i.command==".playSE2") {
        const std::size_t first=i.command==".playBGM" ? 1 : 2;
        for (std::size_t n=first;n<std::min(args.size(),first+2);++n) if (args[n]=="*") args[n]="2";
    }
    if (i.command==".effect" && !args.empty() && args[0]=="*") args[0]="end";
    Json events=Json::array();
    if (i.command==".message" && args.size()==4 && !i.unsupported) {
        const auto parsed=i.events ? *i.events : ef::legacy_dialogue_events(args[3]);
        for (const auto& e:parsed) events.push_back({{"kind",static_cast<int>(e.kind)},{"text",e.text},{"ticks",e.ticks}});
        args[3]="";
    }
    for (auto& s:args) s=canonical(s);
    return {{"script",i.script},{"line",i.line},{"variant",static_cast<int>(i.variant)},{"command",i.command},{"args",args},{"events",events},
        {"colon_variant",i.colon_variant || i.raw.find("[e]:.")!=std::string::npos || i.raw.find("[j]:.")!=std::string::npos}};
}
Json trace(const ef::ScriptLibrary& library,const std::string& entry,char variant,bool last) {
    ef::ScriptRuntime runtime([&](const std::string& id) {return library.compile(id,variant);},
        [&](const std::string& current,const std::string& target) {return library.resolve(current,target);},entry,{variant,false,0,0});
    Json out=Json::array();
    runtime.set_host({[&](const ef::Instruction& i) {
        if ((i.command==".playVoice" && i.args.size()!=1) || i.command=="<unparsed>" || i.command.starts_with("..")) return false;
        out.push_back({{"host",signature(i)}});return true;
    }, {}, {}, {}, [] {return false;}, [&](const std::string& n,std::int64_t v) {out.push_back({{"set",n},{"value",v}});}});
    std::size_t messages=0;std::uint64_t serial=0;
    for (int step=0;step<20000 && messages<250;++step) {
        try {runtime.update(.11);}
        catch (const std::exception&) {out.push_back({{"stop","strict_error"}});return out;}
        out.push_back({{"state",static_cast<int>(runtime.state())},{"shown",runtime.displayed_text()},{"complete",runtime.message_complete()},{"controls",runtime.controls_enabled()}});
        if (runtime.message_serial()!=serial) {
            serial=runtime.message_serial();++messages;const auto& m=*runtime.message();
            out.push_back({{"message",m.id},{"script",m.script},{"line",m.source_line},{"voice",m.voice},{"speaker",m.name},{"text",runtime.full_message_text()}});
        }
        if (runtime.state()==ef::ScriptState::choices) {
            Json choices=Json::array();for (const auto& c:runtime.choices()) choices.push_back({c.text,c.label});
            out.push_back({{"choices",choices}});runtime.choose(last ? runtime.choices().size()-1 : 0);
        } else if (runtime.state()==ef::ScriptState::message) runtime.advance();
        else if (runtime.state()==ef::ScriptState::ended) return out;
    }
    out.push_back({{"stop","bounded_trace"},{"messages",messages}});return out;
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=5 && argc!=3) throw std::runtime_error("Usage: ef_script_verify CONTENT LEGACY_INDEX JSON_INDEX REPORT | ef_script_verify CONTENT JSON_INDEX");
        ef::Content content(argv[1]);
        if (argc==3) {
            ef::ScriptLibrary scripts(content,argv[2]);const auto report=scripts.audit();
            if (content.has_resource_catalog()) {
                const auto resources=scripts.audit_resources();
                if (resources.at("missing")!=0 || resources.at("ambiguous")!=0 || !resources.at("unavailable_files").empty()) throw std::runtime_error("JSON package resource audit failed");
            }
            std::cout<<report.dump(2)<<'\n';return 0;
        }
        ef::ScriptLibrary legacy(content,argv[2],ef::parse_script),modern(content,argv[3]);
        if (legacy.entries().size()!=modern.entries().size()) throw std::runtime_error("Script count changed");
        std::size_t physical=0,compiled=0,messages=0,unsupported=0;
        for (const auto& [id,path]:legacy.entries()) {
            (void)path;const auto a=legacy.parse(id),b=modern.parse(id);
            if (a.size()!=b.size()) throw std::runtime_error(id+": instruction count changed");
            for (std::size_t n=0;n<a.size();++n) {
                if (signature(a[n])!=signature(b[n])) throw std::runtime_error(a[n].location()+": JSON migration changed an instruction\n"+signature(a[n]).dump()+"\n"+signature(b[n]).dump());
                ++physical;messages+=a[n].command==".message";unsupported+=b[n].unsupported;
            }
            for (const char variant:{'e','j'}) {
                const auto x=legacy.compile(id,variant),y=modern.compile(id,variant);
                if (x->labels!=y->labels || x->instructions.size()!=y->instructions.size()) throw std::runtime_error(id+": expanded labels/count changed");
                for (std::size_t n=0;n<x->instructions.size();++n) {
                    if (signature(x->instructions[n])!=signature(y->instructions[n])) throw std::runtime_error(id+": expanded program changed");
                    ++compiled;
                }
            }
        }
        std::size_t traces=0;
        for (const std::string id:{"scr/test.sc","scr/000_01mugai.sc","scr/106_01.sc","scr/216_04.sc","r18_scr/216_04.sc"}) if (legacy.entries().contains(id))
            for (const char v:{'e','j'}) for (const bool last:{false,true}) {
                if (trace(legacy,id,v,last)!=trace(modern,id,v,last)) throw std::runtime_error(id+": VM trace changed");
                ++traces;
            }
        Json report{{"verdict","PASS"},{"scripts",modern.entries().size()},{"instructions",physical},{"messages",messages},{"expanded_instructions_both_variants",compiled},
            {"preserved_unsupported",unsupported},{"bounded_vm_traces",traces},{"trace_message_limit",250},{"trace_scope","VM/control/dialogue and recorded host commands; no audio/video decoding"}};
        if (content.has_resource_catalog()) {
            const auto x=legacy.audit_resources(),y=modern.audit_resources();
            if (x!=y) throw std::runtime_error("Resource reference audit changed");
            report["resource_references"]=y.at("references");report["missing"]=y.at("missing");report["ambiguous"]=y.at("ambiguous");
        }
        std::ofstream file(argv[4]);if (!file) throw std::runtime_error("Cannot write migration report");file<<report.dump(2)<<'\n';
        std::cout<<report.dump(2)<<'\n';return 0;
    } catch (const std::exception& e) {std::cerr<<"script migration: "<<e.what()<<'\n';return 1;}
}
