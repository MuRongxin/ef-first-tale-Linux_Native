#include "scene/shake.hpp"
#include "scene/presentation.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
struct Temporary {
    std::filesystem::path path=std::filesystem::current_path()/("test-shake-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path,ignored); }
};
}
int main() {
    try {
        using namespace ef;
        Shake h("H",100,100); h.update(0); require(h.frame().x==-100,"First horizontal displacement");
        h.update(.029); require(h.frame().x==-100,"Horizontal interval too short");
        h.update(.001); require(h.frame().x==100,"Horizontal alternation");
        h.update(.969); require(!h.finished(),"Duration ended early"); h.update(.001); require(h.finished(),"Duration is not 10ms units");
        Shake v("V",40,20); v.update(0); require(v.frame().y==-40,"Vertical displacement"); v.update(.03); require(v.frame().y==40,"Vertical alternation");
        Shake z("Z",100,100); z.update(0); require(z.frame().crop==100 && z.frame().kind==ShakeKind::zoom,"Zoom crop");
        z.update(.03); require(z.frame().crop==0,"Zoom original alternation");
        Shake r("R",40,200),numeric("2",40,200),o("O",30,200);
        for (int n=0; n<60; ++n) {
            r.update(.03); numeric.update(.03); o.update(.03);
            const auto f=r.frame(),g=numeric.frame();
            require((f.x==0)!=(f.y==0),"Random shake must use cardinal directions");
            require(std::abs(f.x)+std::abs(f.y)==40,"Random amplitude changed");
            require(f.x==g.x && f.y==g.y,"Numeric mode must follow native default");
            require(o.frame().angle>=-30 && o.frame().angle<30,"Rotation bounds");
        }
        Shake zero("R",0,0); require(zero.finished(),"Zero duration should complete");
        Shake large("R",1,10); large.update(86400); require(large.finished(),"Large delta did not finish");
        rejects([] { Shake bad("T",1,10); }); rejects([] { Shake bad("R",1,-1); });
        rejects([] { Shake bad("Z",300,10); }); rejects([&] { large.update(-1); });
        // Exercise real ScriptRuntime busy handling with consecutive commands, input and a following wait/message.
        Temporary temp;
        std::ofstream(temp.path/"manifest.json") << nlohmann::json{{"version",1},{"design_size",{800,600}},
            {"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"development",true},{"images",nlohmann::json::object()}};
        Content content(temp.path); Presentation presentation(content);
        for (const int repeats : {3,6}) {
            auto program=std::make_shared<Program>(); program->id="scr/test.sc";
            std::string script;
            for (int n=0;n<repeats;++n) script+=".shakeScreen R 100 100\n";
            script+=".wait 5\n.message 1   done\n.end\n";
            program->instructions=parse_script(script,program->id);
            ScriptRuntime runtime([&](const std::string&) { return program; },[](const std::string&,const std::string& s) { return s; },program->id);
            runtime.set_host({[&](const Instruction& i) { return presentation.command(i); },{},
                [&](double dt) { presentation.update(dt); },[&] { return presentation.busy(); },{}});
            runtime.update(0);
            const auto first_serial=presentation.shake_serial();
            for (int n=0;n<repeats;++n) {
                const auto serial=first_serial+static_cast<std::uint64_t>(n);
                require(presentation.shake_serial()==serial && presentation.busy(),"Consecutive shake was skipped");
                runtime.advance(); runtime.update(.999);
                require(presentation.shake_serial()==serial && runtime.message_serial()==0,"Input or clock bypassed active shake");
                runtime.update(.001);
            }
            require(!presentation.busy() && runtime.state()==ScriptState::waiting,"Wait did not follow all shakes");
            runtime.update(.049); require(runtime.message_serial()==0,"Wait consumed shake time");
            runtime.update(.001); require(runtime.message_serial()==1,"Message not released after wait");
            require(runtime.diagnostics().empty(),"Unexpected strict diagnostic");
        }
        std::cout << "Shake PASS: modes, timing, bounds, three/six sequential waits and ignored input\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
