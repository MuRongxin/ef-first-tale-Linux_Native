#include "save/checkpoint.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace ef;using J=nlohmann::json;
void require(bool v,const char* m) {if (!v) throw std::runtime_error(m);}
template<class F> void rejects(F f) {try {f();} catch (const std::exception&) {return;}throw std::runtime_error("Expected save rejection");}
struct Temporary {std::filesystem::path path=std::filesystem::current_path()/("test-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temporary(){std::filesystem::create_directories(path);}~Temporary(){std::error_code e;std::filesystem::remove_all(path,e);}};
void put(const std::filesystem::path& p,const J& j) {std::ofstream(p)<<j;}
J node(int line,const char* op,J params) {return {{"source_line",line},{"op",op},{"params",std::move(params)}};}
J dialogue(int line,int id,const std::string& text) {return node(line,"dialogue",{{"id",id},{"voice",""},{"speaker","名字"},{"events",J::array({{{"type","text"},{"value",text}}})}});}
struct Fixture {
    Temporary temp;
    Fixture() {
        J manifest={{"version",1},{"design_size",{800,600}},{"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"development",true},{"resources","resources.json"},{"images",J::object()},{"script_index","index.json"}};
        J catalog={{"version",1},{"entries",J::object()}};
        for (const std::string name:{"bg.png","tall.png","actor.png","finder.png","batFull.png","batMid.png","batLow.png","batZero.png","snowS.png","snowM.png","snowL.png","msgPanel.png","fullPanel.png","BLACK800x75.png","pauseAnime.png"}) {
            const std::string tag=(name=="msgPanel.png" || name=="fullPanel.png" || name=="BLACK800x75.png" || name=="pauseAnime.png") ? "sys" : "bg";
            const auto size=name.starts_with("bat") ? J{59,35} : name=="tall.png" ? J{800,1200} : name=="actor.png" ? J{200,600} : name=="pauseAnime.png" ? J{264,22} : name=="BLACK800x75.png" ? J{800,75} : name=="msgPanel.png" ? J{800,247} : J{800,600};
            const auto id=tag+"/"+name;manifest["images"][id]={{"source_sha256",std::string(64,'a')},{"design_size",size},{"variants",{{"1",{{"path","unused.png"},{"pixels",size}}}}}};
            catalog["entries"][id]={{"kind","image"},{"name",name},{"tag",tag},{"source_sha256",std::string(64,'a')}};
        }
        catalog["entries"]["bgm/other"]={{"kind","bgm"},{"name","other"},{"tag","bgm"},{"path","tone.ogg"},{"source_sha256",std::string(64,'b')}};
        catalog["entries"]["bgm/tone"]={{"kind","bgm"},{"name","tone"},{"tag","bgm"},{"path","tone.ogg"},{"source_sha256",std::string(64,'b')}};
        std::filesystem::copy_file(EF_AUDIO_FIXTURE,temp.path/"tone.ogg");
        std::filesystem::copy_file(EF_UI_FIXTURE,temp.path/"message.ui.json");
        J doc={{"format","ef.script"},{"version",1},{"id","scr/start.sc"},{"instructions",J::array({
            node(1,"set",{{"name","route"},{"value",1}}),
            node(2,"stage",{{"frame",{{"asset",nullptr}}},{"background",{{"asset","bg.png"},{"crop_x",0},{"crop_y",0}}},{"actors",J::array({{{"asset","actor.png"},{"center_x",600},{"bottom_trim",75}}})}}),
            node(3,"bgm",{{"asset","tone"},{"fade_in",0},{"fade_out",0}}),node(4,"panel",{{"mode",2}}),dialogue(5,100,"第一页\\w字面"),dialogue(6,100,"第二段"),
            node(7,"choices",{{"options",J::array({{{"text","左"},{"label","left"}},{{"text","右"},{"label","right"}}})}}),
            node(8,"label",{{"name","left"}}),node(9,"set",{{"name","route"},{"value",10}}),node(10,"chain",{{"target","end.sc"}}),
            node(11,"label",{{"name","right"}}),node(12,"set",{{"name","route"},{"value",20}}),node(13,"chain",{{"target","end.sc"}})})}};
        J end={{"format","ef.script"},{"version",1},{"id","scr/end.sc"},{"instructions",J::array({dialogue(1,200,"完成"),node(2,"end",J::object())})}};
        put(temp.path/"start.json",doc);put(temp.path/"end.json",end);
        put(temp.path/"index.json",{{"version",2},{"format","ef.script.v1"},{"encoding","utf-8"},{"view","unused.json"},{"message_ui","message.ui.json"},{"scripts",{{"scr/start.sc",{{"path","start.json"},{"json_sha256",std::string(64,'a')}}},{"scr/end.sc",{{"path","end.json"},{"json_sha256",std::string(64,'b')}}}}}});
        put(temp.path/"resources.json",catalog);put(temp.path/"manifest.json",manifest);
    }
};
struct Session {
    const Content& c;ScriptLibrary library;Presentation host;ScriptRuntime vm;MessageUI ui;
    explicit Session(const Content& content,const std::string& entry="scr/start.sc") : c(content),library(c),host(c),vm([&](const auto& id){return library.compile(id,'e');},[&](const auto& from,const auto& to){return library.resolve(from,to);},entry,{'e',false,0,0}),ui(c,"message.ui.json") {
        vm.set_host({[&](const Instruction& i){const bool v=host.command(i);if (i.command==".panel") ui.transition_to(host.panel_mode(),{});return v;},
            [&](const Message& m){host.message(m);ui.begin_message(host.panel_mode(),m);},[&](double dt){host.update(dt);vm.update_local_variable("finderCounter",host.finder_counter());ui.update(dt);},[&]{return host.busy() || ui.busy();},[&]{return host.mixer().playing(AudioChannel::voice);},[&](const auto& k,auto v){host.local_assignment(k,v);}});
    }
    J save() {return capture_checkpoint(c,vm,host,&ui);}
    void load(const J& j) {restore_checkpoint(c,j,vm,host,&ui);}
    void pump(ScriptState state) {for (int n=0;n<1000;++n) {vm.update(.02);if (vm.state()==state && (state!=ScriptState::message || vm.message_complete())) return;}throw std::runtime_error("Session pump budget");}
    void command(const char* op,J params) {const J doc={{"format","ef.script"},{"version",1},{"id","scr/start.sc"},{"instructions",J::array({node(1,op,std::move(params))})}};require(host.command(script_json(doc,"scr/start.sc")[0]),"Unimplemented fixture command");}
};
J normalized(J j) {j.erase("created_unix");return j;}
}
int main() {
    try {
        Fixture f;Content c(f.temp.path);Session original(c);rejects([&]{original.save();});original.pump(ScriptState::message);
        require(original.vm.full_message_text()=="第一页\\w字面","Literal slash was reparsed before saving");
        original.vm.advance();original.pump(ScriptState::message);require(original.vm.message()->source_line==6,"Repeated L0 fixture did not advance");
        original.command("bgm",{{"asset","other"},{"fade_in",10},{"fade_out",10},{"volume",50}});
        const auto saved=original.save();const auto file=f.temp.path/"save.json";write_checkpoint(file,saved);
        Session restored(c);restored.load(read_checkpoint(file));require(normalized(saved)==normalized(restored.save()),"Checkpoint did not restore full state/NVL page");
        std::array<float,32> a{},b{};original.host.mixer().mix(a);restored.host.mixer().mix(b);require(a==b,"Restored audio did not retain sample position/envelope");
        original.vm.advance();restored.vm.advance();original.pump(ScriptState::choices);restored.pump(ScriptState::choices);
        const auto choice=restored.save();Session branch(c);branch.load(choice);branch.vm.choose(1);branch.pump(ScriptState::message);
        original.vm.choose(1);original.pump(ScriptState::message);require(original.vm.inspect()==branch.vm.inspect() && branch.vm.variable("route")==20,"Restored choice lost variables or chain continuity");
        const auto stable=normalized(restored.save());rejects([&]{restored.load(choice);});require(normalized(restored.save())==stable,"Live restore attempt mutated current state");
        for (int kind=0;kind<10;++kind) {auto bad=saved;
            if (kind==0) bad["version"]=2;else if (kind==1) bad["vm"]["anchor"]["dialogue_id"]=999;
            else if (kind==2) bad["binding"]["scripts"]["scr/start.sc"]=std::string(64,'c');
            else if (kind==3) bad["vm"]["locals"]["route"]=true;else if (kind==4) bad["presentation"]["audio"]["tracks"][0]["position"]=1e15;
            else if (kind==5) bad["presentation"]["audio"]["tracks"][0]["source_sha256"]=std::string(64,'c');
            else if (kind==6) bad["message_ui"]["labels"][0]["byte_offset"]=999999;
            else if (kind==7) bad["message_ui"]["page_message"]=nullptr;
            else if (kind==8) bad["presentation"]["layers"][0]["trim"]=999999;
            else bad["presentation"]["speed"]=0;
            Session candidate(c);rejects([&]{candidate.load(bad);});
        }
        std::ofstream(f.temp.path/"duplicate.json")<<"{\"version\":1,\"version\":1}";rejects([&]{read_checkpoint(f.temp.path/"duplicate.json");});
        auto oversized=saved;oversized["message_ui"]["prefix"]=std::string(17*1024*1024,'x');rejects([&]{write_checkpoint(file,oversized);});require(read_checkpoint(file)==saved,"Failed write destroyed old save");
        for (const auto* effect:{"Snow","Cutin","WScroll2"}) {
            Session source(c);source.pump(ScriptState::message);
            if (std::string(effect)=="WScroll2") {source.command("stage",{{"frame",{{"asset",nullptr}}},{"background",{{"asset","bg.png"},{"crop_x",0},{"crop_y",0}}},{"actors",J::array()}});source.host.update(1);}
            J params={{"name",effect}};if (std::string(effect)=="Cutin") params.update({{"asset","bg.png"},{"hold_ticks",100}});if (std::string(effect)=="WScroll2") params.update({{"objects",nullptr},{"period",60},{"speed",-2}});
            source.command("effect",params);source.host.update(.05);Session target(c);target.load(source.save());
            for (int n=0;n<10;++n) {source.host.update(.04);target.host.update(.04);require(source.save()["presentation"]==target.save()["presentation"],"Effect continuity changed after restore");}
        }
        {
            Session source(c);source.pump(ScriptState::message);
            source.command("frame",{{"asset","finder.png"}});
            const J stage={{"frame",{{"asset",nullptr}}},{"background",{{"asset","tall.png"},{"crop_x",0},{"crop_y",0}}},{"actors",J::array()}};
            source.command("stage",stage);source.host.local_assignment("finderCounter",1234);source.host.update(.11);
            source.command("scroll_y",{{"target",600},{"speed",5}});source.host.update(.21);
            Session target(c);target.load(source.save());
            for (int n=0;n<10;++n) {source.host.update(.04);target.host.update(.04);require(source.save()["presentation"]==target.save()["presentation"],"Finder/scroll continuity changed after restore");}
            source.command("transition",{{"type",0},{"mask",nullptr},{"speed",10}});source.command("stage",stage);
            rejects([&]{source.save();});
        }
        std::cout<<"Save PASS: typed version/binding, repeated L0/NVL/literal text, audio position, choices/chain, effects, corruption and atomic write\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
