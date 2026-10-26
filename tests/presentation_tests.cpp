#include "scene/presentation.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path()/("test-presentation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code error; std::filesystem::remove_all(path,error); }
};
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        nlohmann::json manifest{{"version",1},{"design_size",{800,600}},{"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},
            {"development",true},{"resources","catalog.json"},{"images",nlohmann::json::object()}};
        nlohmann::json catalog{{"version",1},{"entries",nlohmann::json::object()}};
        for (const auto* name : {"wide.png","actor.png","frame.png","mask.png","finder.png","batFull.png","batMid.png","batLow.png","batZero.png"}) {
            const std::string id = std::string("bg/")+name;
            const auto size = std::string(name)=="wide.png" ? nlohmann::json{1600,1800} : (std::string(name)=="mask.png" || std::string(name)=="finder.png") ? nlohmann::json{800,600} : std::string(name).starts_with("bat") ? nlohmann::json{59,35} : nlohmann::json{200,600};
            manifest["images"][id] = {{"source_sha256",std::string(64,'a')},{"design_size",size},{"variants",{{"1",{{"path","unused.png"},{"pixels",size}}}}}};
            catalog["entries"][id] = {{"kind","image"},{"name",name},{"tag","bg"},{"source_sha256",std::string(64,'a')}};
        }
        for (const auto* name : {"shade_a","shade_b","shade_c"}) {
            const std::string id=std::string("sys/")+name;
            manifest["images"][id]={{"source_sha256",std::string(64,'c')},{"design_size",{1,601}},
                {"variants",{{"1",{{"path","unused-strip.png"},{"pixels",{1,601}}}}}}};
            catalog["entries"][id]={{"kind","image"},{"name",name},{"tag","sys"},{"source_sha256",std::string(64,'c')}};
        }
        for (const auto* kind : {"voice","bgm","se"}) {
            const std::string id = std::string(kind)+"/tone";
            catalog["entries"][id] = {{"kind",kind},{"name","tone"},{"tag",kind},{"source_sha256",std::string(64,'b')},{"path","tone.ogg"}};
        }
        for (const auto* kind : {"bgm","se"}) {
            const std::string id=std::string(kind)+"/other";
            catalog["entries"][id]={{"kind",kind},{"name","other"},{"tag",kind},{"source_sha256",std::string(64,'b')},{"path","tone.ogg"}};
        }
        std::filesystem::copy_file(EF_AUDIO_FIXTURE,temp.path/"tone.ogg");
        std::ofstream(temp.path/"manifest.json") << manifest.dump();
        std::ofstream(temp.path/"catalog.json") << catalog.dump();
        Content content(temp.path);
        Presentation stage(content);
        const auto command = [&](const std::string& text) { const auto i = parse_script(text,"scr/test.sc").at(0); return stage.command(i); };
        stage.local_assignment("finderCounter",3599);
        stage.update(.999);require(stage.finder_counter()==3599,"Finder timer rounded up before a full second");
        stage.update(.001);require(stage.finder_counter()==3600,"Hidden finder timer must continue");
        command(".frame finder.png\n");require(stage.current().empty(),"Frame must only update pending state");
        command(".stage frame.png mask.png 0 0\n");
        auto finder_scene=stage.current();
        require(finder_scene.size()==12 && finder_scene[2].asset=="bg/finder.png","Finder must be above the stage frame");
        require(finder_scene[6].asset=="bg/batFull.png" && finder_scene[6].x==28 && finder_scene[6].y==12,"Finder full battery and coordinates");
        for (std::size_t i=0;i<5;++i) require(finder_scene[i+7].kind==LayerKind::text && finder_scene[i+7].text==(i==0 ? "1" : "0"),"Finder hour/minute/second digits must be vector layers");
        const std::array<float,5> digit_x{597,641,672,716,747};
        for (std::size_t i=0;i<5;++i) require(finder_scene[i+7].x==digit_x[i] && finder_scene[i+7].y==13,"Native finder digit positions");
        stage.local_assignment("finderCounter",120);stage.update(.75);
        command(".frame finder.png\n");stage.update(.25);
        require(stage.finder_counter()==121,"Repeated frame filename must preserve fractional clock");
        stage.local_assignment("finderCounter",18000);stage.update(.9);
        stage.local_assignment("finderCounter",18000);stage.update(.1);
        require(stage.finder_counter()==18000,"Repeated assignment of same value must reset fractional clock");
        for (const auto& [seconds,battery] : std::array<std::pair<int,const char*>,6>{{{18000,"batFull.png"},{18001,"batMid.png"},{25200,"batMid.png"},{25201,"batLow.png"},{32400,"batLow.png"},{32401,"batZero.png"}}}) {
            stage.local_assignment("finderCounter",seconds);
            require(stage.current()[6].asset==std::string("bg/")+battery,"Finder battery strict threshold");
        }
        stage.local_assignment("finderCounter",35999);stage.update(100000);
        require(stage.finder_counter()==35999 && stage.current()[7].text=="9" && stage.current()[8].text=="5" && stage.current()[9].text=="9","Finder clock clamps to 9:59:59");
        rejects([&] { stage.local_assignment("finderCounter",-1); });
        require(stage.finder_counter()==35999,"Invalid counter assignment mutated clock");
        stage.local_assignment("finderCounter",3600);
        command(".transition 0 * 10\n");command(".stage frame.png mask.png 0 0\n");stage.update(1);
        require(stage.finder_counter()==3601 && stage.current()[11].text=="0","Incoming finder snapshot must freeze while its timer continues");
        stage.update(25);
        require(!stage.busy() && stage.current()[11].text=="6","Finder must refresh from elapsed time after transition commits");
        stage.local_assignment("finderCounter",35999);
        rejects([&] { command(".frame missing.png\n"); });
        command(".transition 0 * 10\n");command(".frame *\n");
        require(stage.current().size()==12,"Frame wildcard cleared current scene prematurely");
        command(".stage * mask.png 0 0\n");
        require(stage.previous().size()==12 && stage.current().size()==1,"Finder clear lost outgoing snapshot");
        stage.local_assignment("finderCounter",0);stage.update(1);
        require(stage.previous()[7].text=="9","Outgoing finder digits must be frozen independently");
        stage.update(2);command(".transition 0 * 9999\n");
        command(".frame frame.png\n");command(".stage * mask.png 0 0\n");
        require(stage.current().size()==2 && stage.current().back().asset=="bg/frame.png","Generic frame must not gain finder digits");
        command(".frame *\n");command(".stage * mask.png 0 0\n");
        auto counter_program=std::make_shared<Program>();counter_program->id="counter";
        counter_program->instructions=parse_script(".set finderCounter = 10\n.frame finder.png\n.stage * mask.png 0 0\n.message 1   pause\n.set finderCounter = finderCounter + 1\n.message 2   next\n.end\n","counter");
        ScriptRuntime counter_runtime([&](const std::string&) {return counter_program;},
            [](const std::string&,const std::string& target) {return target;},"counter");
        counter_runtime.set_host({[&](const Instruction& i) {return stage.command(i);}, {},
            [&](double seconds) {stage.update(seconds);counter_runtime.update_local_variable("finderCounter",stage.finder_counter());},
            [&] {return stage.busy();}, {},
            [&](const std::string& name,std::int64_t value) {stage.local_assignment(name,value);}});
        counter_runtime.update(0);counter_runtime.update(1.5);
        require(counter_runtime.variable("finderCounter")==11 && stage.finder_counter()==11,"Finder clock must write back during paused dialogue");
        counter_runtime.advance();counter_runtime.update(0);
        require(counter_runtime.message()->id==2 && counter_runtime.variable("finderCounter")==12 && stage.finder_counter()==12,"Script arithmetic must see elapsed finder time");
        counter_runtime.update(.5);require(stage.finder_counter()==12,"Script assignment must reset fractional clock after arithmetic");
        command(".frame *\n");
        command(".stage * wide.png 200 100 actor.png 400\n");
        require(stage.current().size()==2 && stage.current()[0].x==-200 && stage.current()[0].y==-100,"background crop");
        require(stage.current()[1].x==300 && stage.current()[1].y==0,"center/bottom foreground");
        command(".transition 0 * 10\n");
        command(".stage * wide.png 0 0\n");
        require(stage.busy() && stage.progress()==0 && stage.previous().size()==2,"stage blocks during transition");
        stage.update(1.28);
        require(std::abs(stage.progress()-0.5f)<0.005f,"native crossfade clock");
        stage.update(1.28);
        require(!stage.busy() && stage.progress()==1 && stage.previous().empty(),"transition completes");
        command(".vscroll 1200 25\n"); command(".endScroll f\n");
        stage.update(2);
        require(stage.current()[0].y==-500 && stage.busy(),"scroll speed in logical pixels");
        stage.update(3);
        require(stage.current()[0].y==-1200 && !stage.busy(),"scroll clamps and wait releases");
        command(".hscroll 800 20\n"); command(".endScroll t\n");
        require(stage.current()[0].x==-800 && !stage.busy(),"forced scroll completion");
        rejects([&] { command(".hscroll 0 20\n"); });
        command(".hscroll 0 -20\n"); stage.update(1);
        require(stage.current()[0].x==-600,"negative scroll speed");
        command(".scroll 0 0 100\n"); stage.update(2);
        require(stage.current()[0].x==0 && stage.current()[0].y==0,"two-axis scroll derives direction from endpoints");
        command(".panel 0\n"); require(!stage.panel_visible(),"hidden panel");
        command(".panel 3\n"); require(stage.panel_visible(),"visible panel");
        rejects([&] { command(".transition 1 * 10\n"); });
        rejects([&] { command(".stage * wide.png 0 0 actor.png 400:1\n"); });
        rejects([&] { command(".stage * missing.png 0 0\n"); });
        require(!command(".effect Rain\n"),"unsupported effects are not swallowed");
        command(".playSE tone t 0 0\n");
        require(stage.mixer().playing(AudioChannel::se),"SE zero fade is audible playback");
        stage.update(30); require(stage.mixer().playing(AudioChannel::se),"SE loop");
        stage.message({"scr/test.sc","tone","","text",1,1});
        require(stage.mixer().playing(AudioChannel::voice),"message voice");
        stage.message({"scr/test.sc","","","text",2,2});
        require(!stage.mixer().playing(AudioChannel::voice),"empty voice stops prior voice");
        command(".playSE * f 0 0\n"); require(!stage.mixer().playing(AudioChannel::se),"SE stop");
        command(".playBGM tone 0 0 25\n");
        require(stage.mixer().playing(AudioChannel::bgm),"BGM starts");
        command(".playBGM tone 0 10 50\n");
        require(std::abs(stage.mixer().gain(AudioChannel::bgm)-0.5f)<1e-5f,"same BGM changes gain without replacing voice");
        command(".playBGM * 0 10 100\n"); stage.update(0.1);
        require(!stage.mixer().playing(AudioChannel::bgm),"BGM fade-out");
        command(".playBGM tone 0 0 100\n");
        command(".playBGM other 10 1 50\n");
        require(!stage.busy() && stage.mixer().gain(AudioChannel::bgm)==0,"Cross-track BGM starts immediately without script wait");
        stage.update(.025);
        require(stage.mixer().gain(AudioChannel::bgm)>0,"Incoming BGM fades in");
        command(".playBGM other 0 10 25\n");
        require(std::abs(stage.mixer().gain(AudioChannel::bgm)-.25f)<1e-5f,"Same incoming BGM changes gain without a third voice");
        command(".playBGM * 0 0\n");
        require(!stage.mixer().current_playing(AudioChannel::bgm) && stage.mixer().playing(AudioChannel::bgm),"Script stop leaves prior outgoing BGM envelope running");
        stage.update(2); require(!stage.mixer().playing(AudioChannel::bgm),"Old BGM envelope ends independently");
        command(".playBGM tone 0 0 100\n");
        command(".playBGM * 0 1\n");
        require(!stage.mixer().current_playing(AudioChannel::bgm),"Script fade-stop invalidates current BGM identity immediately");
        command(".playBGM tone 10 10 100\n");
        require(stage.mixer().gain(AudioChannel::bgm)==0,"Stopped BGM starts a new fade-in rather than resuming the outgoing clip");
        stage.update(2); stage.mixer().stop_all();
        command(".playSE tone true 0 0\n"); stage.update(.01);
        const auto unchanged_gain=stage.mixer().gain(AudioChannel::se);
        command(".playSE tone true 100 100\n");
        require(stage.mixer().gain(AudioChannel::se)==unchanged_gain,"Repeated looping SE does not restart or fade in again");
        command(".playSE other true 0 1\n"); command(".playSE * 20\n");
        stage.update(2); require(!stage.mixer().playing(AudioChannel::se),"Numeric loop parameter follows native false semantics");
        command(".playSE tone false 0 0\n"); stage.update(.025);
        command(".playSE tone false 0 0\n"); stage.update(.01);
        require(stage.mixer().current_playing(AudioChannel::se),"Repeated non-looping SE replays from start");
        command(".playSE2 tone true 0 0\n"); command(".playSE2 other true 0 2\n");
        require(stage.mixer().playing(AudioChannel::se2),"Second SE channel supports independent overlap");
        const auto unchanged_revision=stage.revision();
        command(".screen shade_a shade_b shade_c\n");
        require(stage.revision()==unchanged_revision && stage.current()[0].screen.empty(),"Screen only changes pending materials");
        command(".transition 0 * 9999\n");
        command(".stage frame.png wide.png 0 600 actor.png 400\n");
        require(stage.current()[0].screen=="sys/shade_b" && stage.current()[1].screen=="sys/shade_c" && stage.current()[2].screen=="sys/shade_a","Screen parameter order is frame/background/foreground");
        require(stage.current()[0].y==-600,"Screen preserves tall background crop geometry");
        command(".screen shade_c shade_a\n");
        require(stage.current()[0].screen=="sys/shade_b","Pending screen does not mutate displayed stage");
        rejects([&] { command(".screen shade_a missing shade_c\n"); });
        command(".transition 0 * 10\n");
        command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.previous()[0].screen=="sys/shade_b" && stage.current()[0].screen=="sys/shade_c","Old/new snapshots retain independent screen state");
        require(stage.current()[1].screen=="sys/shade_a" && stage.current()[2].screen.empty(),"Two screen arguments clear the omitted frame filter; invalid update was atomic");
        stage.update(3); command(".screen * * *\n");
        command(".transition 0 * 9999\n"); command(".stage frame.png wide.png 0 0 actor.png 400\n");
        for (const auto& layer : stage.current()) require(layer.screen.empty(),"Screen wildcard clears all scopes");
        rejects([&] { command(".screen shade_a\n"); });
        command(".overlay shade_a shade_b shade_c\n");
        require(stage.current()[0].overlay.empty(),"Overlay changed displayed material early");
        command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.current()[0].overlay=="sys/shade_b" && stage.current()[1].overlay=="sys/shade_c" && stage.current()[2].overlay=="sys/shade_a","Overlay scopes");
        command(".overlay shade_c shade_a\n");
        rejects([&] { command(".overlay shade_a missing\n"); });
        command(".screen shade_b shade_c shade_a\n");
        command(".transition 0 * 10\n"); command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.previous()[0].overlay=="sys/shade_b" && stage.current()[0].overlay=="sys/shade_c","Overlay snapshots must be independent");
        require(stage.current()[1].overlay=="sys/shade_a" && stage.current()[2].overlay.empty(),"Two overlay args clear frame; invalid update atomic");
        require(stage.current()[0].screen=="sys/shade_c","Overlay and screen state must coexist");
        stage.update(3); command(".overlay * * *\n"); command(".stage frame.png wide.png 0 0 actor.png 400\n");
        for (const auto& layer : stage.current()) require(layer.overlay.empty(),"Overlay wildcard clears all scopes");
        rejects([&] { command(".overlay shade_a\n"); });
        stage.update(3);
        const auto material_revision=stage.revision();
        command(".HSVfilter 9 -25 -8 5 -5 0\n");
        require(stage.current()[0].hsv==std::array<int,3>{} && stage.revision()==material_revision,"HSV mutated displayed scene");
        command(".transition 1 mask.png 20\n");
        command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.current()[0].hsv==std::array<int,3>{9,-25,-8} && stage.current()[1].hsv==std::array<int,3>{5,-5,0} && stage.current()[2].hsv==std::array<int,3>{},"HSV background/foreground/frame scopes");
        require(stage.busy() && stage.transition_mask()=="bg/mask.png","Masked transition did not block");
        stage.update(1.28); require(std::abs(stage.progress()-.5f)<.001f && stage.busy(),"Masked transition must use 512 threshold");
        command(".transition 1 mask.png 30\n");
        require(std::abs(stage.progress()-.5f)<.001f,"Pending speed changed active transition");
        stage.update(1.27); require(stage.busy(),"Masked transition finished early");
        stage.update(.01); require(!stage.busy(),"Masked transition did not finish at 512");
        rejects([&] { command(".HSVfilter 0 0 0 bad 0 0\n"); });
        rejects([&] { command(".transition 1 wide.png 20\n"); });
        rejects([&] { command(".transition 1 missing 20\n"); });
        rejects([&] { command(".transition 3 * 20\n"); });
        command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.transition_mask()=="bg/mask.png" && stage.current()[0].hsv==std::array<int,3>{9,-25,-8},"Invalid pending updates must be atomic");
        stage.update(3); command(".HSVfilter 999 -999 999 -999 999 -999\n"); command(".transition 0 * 9999\n");
        command(".stage frame.png wide.png 0 0 actor.png 400\n");
        require(stage.current()[0].hsv==std::array<int,3>{180,-100,100} && stage.current()[1].hsv==std::array<int,3>{-180,100,-100},"Native HSV clamping");
        require(stage.transition_mask().empty() && !stage.busy(),"Type zero should clear masked transition");
        command(".HSVfilter 0 0 0 0 0 0\n"); command(".stage frame.png wide.png 0 0 actor.png 400\n");
        for (const auto& layer : stage.current()) require(layer.hsv==std::array<int,3>{},"Zero HSV reset");
        command(".effect Cutin mask.png -10\n");
        require(!stage.busy() && stage.effects().size()==1 && stage.effects()[0].x==800,"Cutin must be nonblocking");
        rejects([&] { command(".effect Cutin missing -1\n"); });
        require(stage.effects().size()==1,"Failed effect replaced a valid effect");
        stage.update(2);require(stage.effects()[0].x==0,"Cutin did not enter");
        command(".stage frame.png mask.png 0 0\n");require(stage.effects().size()==1,"Stage removed a held Cutin");
        const auto cut_scene=stage.current();
        require(cut_scene.size()==3 && cut_scene[0].part==StagePart::background && cut_scene[1].part==StagePart::none && cut_scene[2].part==StagePart::frame,"Cutin must be below the foreground frame");
        command(".effect fadeout\n");stage.update(2);require(stage.effects().empty(),"Cutin did not leave after fadeout");
        command(".stage frame.png mask.png 0 0 mask.png 400\n");
        command(".effect WScroll2 * 60 -2\n");stage.update(.3);
        const auto moving=stage.current();
        require(!stage.busy() && moving[0].x==-799,"Walking must run while script can advance");
        command(".effect WScroll2 * 60 -2\n");require(stage.current()[0].x==moving[0].x,"Repeated walking restarted the phase");
        command(".transition 0 * 20\n");
        command(".stage frame.png mask.png 0 0 mask.png 400\n");
        require(stage.previous()[0].x==moving[0].x && stage.current()[0].x==moving[0].x,"Stage snapshot or walking rebinding lost motion");
        command(".effect end\n");const auto stopped=stage.current();stage.update(2);
        require(stage.current()[0].x==stopped[0].x,"Walking end should retain the last canvas");
        require(!command(".effect Unknown * 60 -2\n"),"Unknown multi-argument effect was swallowed");
        rejects([&] { command(".effect Cutin mask.png bad\n"); });
        rejects([&] { command(".effect WScroll2 * 0 -2\n"); });
        std::cout << "Presentation tests PASS: scene placement, crossfade, scroll waits, panel, voiced messages and audio commands\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
