#include "scene/transition.hpp"
#include "scene/presentation.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
struct Temporary {
    std::filesystem::path path=std::filesystem::current_path()/("test-transition-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored;std::filesystem::remove_all(path,ignored); }
};
}
int main() {
    try {
        using namespace ef;
        require(transition_region(14,400)==std::array<int,4>{0,0,400,300},"Aspect-preserving expansion");
        require(transition_region(16,200)==std::array<int,4>{0,0,800,200},"Vertical expansion");
        require(transition_region(17,200)==std::array<int,4>{0,0,200,600},"Horizontal expansion");
        require(zoom_transition_region(0)==std::array<int,4>{0,0,800,600},"Zoom start");
        require(zoom_transition_region(10)==std::array<int,4>{80,60,640,480},"Zoom lookup reconstruction");
        require(zoom_transition_region(100)==std::array<int,4>{385,288,30,23},"Zoom crop truncation");
        require(zoom_transition_region(199)==std::array<int,4>{396,297,8,6},"Maximum zoom");
        require(zoom_transition_region(250)==std::array<int,4>{393,295,14,10},"Reverse zoom lookup");
        require(zoom_transition_region(399)==zoom_transition_region(0),"Zoom end");
        require(transition_completion(0)==256 && transition_completion(1)==512 && transition_completion(2)==400
            && transition_completion(14)==801 && transition_completion(16)==601 && transition_completion(17)==801,
            "Completion counters");
        require(supported_transition(0) && supported_transition(1) && supported_transition(2)
            && supported_transition(14) && supported_transition(16) && supported_transition(17)
            && !supported_transition(3) && !supported_transition(15) && !supported_transition(18),
            "Supported transition set");
        rejects([] { (void)transition_completion(3); });
        rejects([] { (void)transition_region(2,10); });
        require(transition_region(14,17)==std::array<int,4>{0,0,17,12},"Aspect expansion integer rounding");
        require(transition_region(16,599)==std::array<int,4>{0,0,800,599},"Vertical expansion short of canvas");
        require(transition_region(14,801)==std::array<int,4>{0,0,800,600},"Clamped aspect expansion");
        require(transition_region(16,601)==std::array<int,4>{0,0,800,600},"Clamped vertical expansion");
        require(transition_region(17,801)==std::array<int,4>{0,0,800,600},"Clamped horizontal expansion");
        Temporary temp;
        nlohmann::json manifest{{"version",1},{"design_size",{800,600}},{"entry_scene","unused"},
            {"font",{{"path","unused.ttf"}}},{"development",true},{"resources","catalog.json"},{"images",nlohmann::json::object()}};
        manifest["images"]["bg/image"]={{"source_sha256",std::string(64,'a')},{"design_size",{800,600}},
            {"variants",{{"1",{{"path","unused.png"},{"pixels",{800,600}}}}}}};
        std::ofstream(temp.path/"manifest.json") << manifest.dump();
        nlohmann::json catalog{{"version",1},{"entries",nlohmann::json::object()}};
        catalog["entries"]["bg/image"]={{"kind","image"},{"name","image"},{"tag","bg"},{"source_sha256",std::string(64,'a')}};
        std::ofstream(temp.path/"catalog.json") << catalog.dump();
        Content content(temp.path);Presentation stage(content);
        const auto command=[&](const std::string& text) { return stage.command(parse_script(text+"\n","scr/test.sc").at(0)); };
        for (const int type : {14,16,17}) {
            command(".transition "+std::to_string(type)+" * 400");command(".stage * image 0 0");
            require(stage.busy(),"Speed400 incorrectly bypassed special transition");
            const int ticks=type==16 ? 15 : 20;
            stage.update(static_cast<double>(ticks)/100);
            require(stage.busy() && stage.progress()<1,"Boundary equality must remain busy");
            command(".transition 0 * 9999");
            require(stage.transition_type()==type,"Pending type changed active state");
            stage.update(.01);require(!stage.busy(),"Strictly beyond edge did not complete");
        }
        command(".transition 14 * 12");command(".stage * image 0 0");stage.update(6.67);
        require(stage.busy(),"Integer dispatcher truncation lost the edge tick");stage.update(.01);require(!stage.busy(),"Type14 final tick");
        command(".transition 2 * 20");command(".stage * image 0 0");stage.update(1.99);
        require(stage.busy(),"Zoom finished before400");stage.update(.01);require(!stage.busy(),"Zoom did not finish at400");
        const auto exact_edge=[&](int type,double before,double after,const char* message) {
            command(".transition "+std::to_string(type)+" * 1");command(".stage * image 0 0");
            stage.update(before);require(stage.busy() && stage.progress()<1,message);
            stage.update(after-before);require(!stage.busy(),"Completion counter did not finish exactly");
        };
        // speed1 advances one counter per 100 ms: at the canvas edge the native loop still runs one more counter.
        exact_edge(14,80.0,80.1,"Type14 exact edge 800");
        exact_edge(16,60.0,60.1,"Type16 exact edge 600");
        exact_edge(17,80.0,80.1,"Type17 exact edge 800");
        rejects([&] { command(".transition 14 * 0"); });rejects([&] { command(".transition 17 * -4"); });
        rejects([&] { command(".transition 14 bg/mask.png 400"); });
        rejects([&] { command(".transition 3 * 20"); });rejects([&] { command(".transition 2 * 10"); });
        rejects([&] { command(".transition 16 image 400"); });
        auto program=std::make_shared<Program>();program->id="scr/ordering.sc";
        program->instructions=parse_script(".transition 17 * 400\n.stage * image 0 0\n.message 1   after\n.end\n",program->id);
        ScriptRuntime runtime([&](const std::string&) { return program; },[](const std::string&,const std::string& s) { return s; },program->id);
        runtime.set_host({[&](const Instruction& i) { return stage.command(i); },{},[&](double dt) { stage.update(dt); },[&] { return stage.busy(); },{}});
        runtime.update(0);runtime.advance();runtime.update(.2);
        require(runtime.message_serial()==0 && stage.busy(),"Input or message bypassed transition");
        runtime.update(.01);require(runtime.message_serial()==1 && runtime.diagnostics().empty(),"Message did not follow strict boundary");
        std::cout << "Transition clocks PASS: expansion directions, zoom crops, integer dispatch, speed400, exact edges, strict boundaries and message ordering\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
