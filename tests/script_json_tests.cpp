#include "script/script.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool v,const char* m) {if (!v) throw std::runtime_error(m);}
template<class F> void rejects(F f) {try {f();} catch (const std::exception&) {return;}throw std::runtime_error("Expected JSON rejection");}
}
int main() {
    try {
        using namespace ef;using J=nlohmann::json;
        J doc=J::parse(R"json({"format": "ef.script", "version": 1, "id": "scr/test.sc", "instructions": [{"source_line": 1, "op": "stage", "params": {"frame": {"asset": null}, "background": {"asset": "bg.png", "crop_x": 0, "crop_y": 0}, "actors": [{"asset": "actor.png", "center_x": 600, "bottom_trim": 75}]}}, {"source_line": 2, "op": "dialogue", "params": {"id": 100, "voice": "", "speaker": "名字", "events": [{"type": "text", "value": "字面\\w\n中"}, {"type": "pause", "ticks": 10}, {"type": "text", "value": "后"}, {"type": "wait_voice"}, {"type": "auto_advance"}]}}, {"source_line": 3, "op": "end", "params": {}}]})json");
        auto instructions=script_json(doc,"scr/test.sc");
        require(instructions[0].args==std::vector<std::string>{"*","bg.png","0","0","actor.png","600,75"},"Typed stage fields lost geometry");
        require(instructions[1].events.has_value(),"JSON dialogue must carry events directly");
        Dialogue dialogue(*instructions[1].events,0);
        dialogue.update(0,true);require(dialogue.text()=="字面\\w\n中" && !dialogue.complete(),"Literal backslash was parsed as a control or pause ignored");
        dialogue.update(.1,true);require(dialogue.text()=="字面\\w\n中后" && !dialogue.complete(),"Voice barrier ignored");
        dialogue.update(0,false);require(dialogue.complete() && dialogue.automatic(),"Structured voice/auto events changed timing");
        auto p=std::make_shared<Program>();p->id="scr/test.sc";p->instructions=instructions;
        ScriptRuntime runtime([&](const std::string&) {return p;},[](const std::string&,const std::string& s) {return s;},p->id,{'e',true});
        runtime.update(0);require(runtime.full_message_text()=="字面\\w\n中后","Preview reparsed literal JSON text as SC escapes");
        auto bad=doc;bad["version"]=2;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["id"]="wrong";rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][0]["params"]["actors"][0]["center_x"]=true;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][0]["params"]["actors"][0]["center_x"]=600.0;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][0]["params"]["extra"]=0;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][0]["op"]="typo";rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][1]["source_line"]=1;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][1]["params"]["events"][0]["value"]=std::string("x\0y",3);rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][1]["params"]["events"][1]["ticks"]=-1;rejects([&] {script_json(bad,p->id);});
        bad=doc;bad["instructions"][0]["source_line"]=std::numeric_limits<std::uint64_t>::max();rejects([&] {script_json(bad,p->id);});
        rejects([&] {read_script_json("{\"version\":1,\"version\":1}",p->id);});
        J unsupported=J::parse(R"json({"format": "ef.script", "version": 1, "id": "scr/test.sc", "instructions": [{"source_line": 1, "op": "unsupported", "params": {"command": ".effect", "arguments": ["Rain"], "raw": ".effect Rain", "reason": "unknown effect"}}]})json");
        auto q=std::make_shared<Program>();q->id=p->id;q->instructions=script_json(unsupported,p->id);
        ScriptRuntime strict([&](const std::string&) {return q;},[](const std::string&,const std::string& s) {return s;},q->id);
        strict.set_host({[](const Instruction&) {return true;},{},{},{},{},{}});
        rejects([&] {strict.update(0);}); // Even a permissive host cannot silently execute an unsupported node.
        const auto root=std::filesystem::current_path()/("test-json-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{root};
        std::filesystem::create_directories(root/"scripts");
        std::ofstream(root/"manifest.json")<<J{{"version",1},{"design_size",{800,600}},{"entry_scene","unused"},{"font",{{"path","unused.ttf"}}},{"images",J::object()}};
        std::ofstream(root/"scripts/index.json")<<J{{"version",1},{"encoding","utf-8"},{"scripts",J::object()}};
        Content content(root);rejects([&] {ScriptLibrary legacy_rejected(content);});
        std::ofstream(root/"test.script.json")<<doc;
        std::ofstream(root/"scripts/index.json")<<J{{"version",2},{"format","ef.script.v1"},{"encoding","utf-8"},{"scripts",{{p->id,{{"path","test.script.json"}}}}}};
        require(ScriptLibrary(content).compile(p->id,'e')->instructions.size()==3,"JSON-only package failed to compile without SC");
        std::cout<<"Script JSON PASS: typed fields/events, literal escapes, strict schema, unsupported nodes, JSON-only package and legacy refusal\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
