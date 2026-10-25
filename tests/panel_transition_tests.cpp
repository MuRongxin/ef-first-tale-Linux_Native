#include "app/script_view.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool has_text(const ef::Scene& scene,const std::string& text) {
    for (const auto& layer : scene.layers) if (layer.text==text) return true;
    return false;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected synthetic content");
        ef::Content content(argv[1]); ef::Options options;
        options.script="scr/timing.sc"; options.text_speed=0; options.silent=true;
        ef::ScriptView view(content,options);
        view.update(0);
        require(view.runtime().message_serial()==0,"Message ran before panel appeared");
        view.advance(); view.update(.15);
        require(view.runtime().message_serial()==0,"Input skipped active panel transition");
        view.update(.01); view.update(0);
        require(view.runtime().message_serial()==1 && has_text(view.scene(),"旧正文"),"First message did not start after 160ms");
        const auto revision=view.presentation().revision();
        view.advance(); view.update(0);
        require(view.runtime().message_serial()==1 && has_text(view.scene(),"旧正文"),"Old text was not retained at switch start");
        view.update(.08);
        require(view.presentation().revision()==revision,"Stage command ran during panel transition");
        require(!view.presentation().mixer().playing(ef::AudioChannel::voice),"Next voice started during panel transition");
        const auto midpoint=view.scene();
        require(midpoint.layers.size()==4 && std::abs(midpoint.layers[0].color[3]-127.0f/255)<1e-6f,"Midpoint old-layer alpha");
        view.update(.08); view.update(0);
        require(view.runtime().message_serial()==2 && view.presentation().revision()>revision,"Next stage/message not released after transition");
        require(view.presentation().mixer().playing(ef::AudioChannel::voice),"Next message voice did not start");
        require(!has_text(view.scene(),"旧正文") && has_text(view.scene(),"新正文"),"Completed mode change left old text");
        view.advance(); view.update(0); view.update(0);
        require(view.runtime().message_serial()==3 && has_text(view.scene(),"新正文\n续写正文"),"Repeated panel waited or cleared NVL history");
        view.advance(); view.update(0); view.update(.15);
        require(view.runtime().state()==ef::ScriptState::running && has_text(view.scene(),"新正文\n续写正文"),"Hide transition discarded text early or started wait early");
        view.update(.01);
        require(view.runtime().state()==ef::ScriptState::waiting && view.scene().layers.empty(),"Hide did not complete before script wait");
        view.update(.049); require(view.runtime().state()==ef::ScriptState::waiting,"Script wait counted transition time");
        view.update(.001);
        require(view.runtime().state()==ef::ScriptState::running && view.runtime().message_serial()==3,"Panel 3 did not block final message");
        view.update(86400); view.update(0);
        require(view.runtime().message_serial()==4 && has_text(view.scene(),"最终正文"),"Large headless delta left panel busy");
        require(!has_text(view.scene(),"新正文\n续写正文"),"Old NVL page returned after reentry");
        std::cout << "Panel transition integration PASS: message/stage/voice/wait ordering, ignored input, frozen old text, NVL retention and large delta\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
