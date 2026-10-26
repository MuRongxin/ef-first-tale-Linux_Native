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
        for (const auto* name : {"wide.png","actor.png","frame.png"}) {
            const std::string id = std::string("bg/")+name;
            const auto size = std::string(name)=="wide.png" ? nlohmann::json{1600,1800} : nlohmann::json{200,600};
            manifest["images"][id] = {{"source_sha256",std::string(64,'a')},{"design_size",size},{"variants",{{"1",{{"path","unused.png"},{"pixels",size}}}}}};
            catalog["entries"][id] = {{"kind","image"},{"name",name},{"tag","bg"},{"source_sha256",std::string(64,'a')}};
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
        std::cout << "Presentation tests PASS: scene placement, crossfade, scroll waits, panel, voiced messages and audio commands\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
