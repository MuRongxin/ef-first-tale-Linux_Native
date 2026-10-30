#include "scene/image_animation.hpp"
#include "data/content.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace ef;
void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
template<class F> void rejects(F f) {try {f();} catch (const std::exception&) {return;}throw std::runtime_error("Expected animation rejection");}
int main(int argc,char** argv) {
    try {
        AnimationSpec spec{std::string(64,'a'),{{"景","中目","half.png",2,2,1,1},{"景","閉じ目","closed.png",2,2,1,1},
            {"TEL","B","tel.png",2,2,4,4},{"TEL","@;*:2;B:2;~","",0,0,0,0}}};
        ImageAnimation player(spec,5);
        require(player.frames().empty(),"Initial ANI base");
        player.update(.02);require(player.frames()==std::vector<std::size_t>{2},"Explicit ANI loop");
        const auto saved=player.checkpoint();auto restored=ImageAnimation::restore(spec,saved);
        require(restored.checkpoint()==saved,"Animation round trip");
        player.update(0);require(player.checkpoint()==saved,"Zero delta must not consume random state");
        for (int i=0;i<100;++i) {player.update(.01);restored.update(.01);require(player.checkpoint()==restored.checkpoint(),"Animation lost continuation after restore");}
        ImageAnimation eye(spec,5);eye.update(.3);const auto middle=eye.frames();require(std::find(middle.begin(),middle.end(),0)!=middle.end(),"Native eye name mapping middle");
        eye.update(.3);require(eye.frames()[0]==1,"Native eye name mapping closed");
        auto old=spec;old.source_sha256=std::string(64,'b');rejects([&] {ImageAnimation::restore(old,saved);});
        for (int n=0;n<6;++n) {
            auto bad=saved;
            if (n==0) bad["sequence"]=true;
            if (n==1) bad["elapsed"]=-1;
            if (n==2) bad["tracks"][1]["eye"][2]=999;
            if (n==3) bad["tracks"][0]["loop"][0]="absent";
            if (n==4) bad["tracks"][0]["loop"][1]=900;
            if (n==5) bad["tracks"][1]["eye"][0]=3;
            rejects([&] {ImageAnimation::restore(spec,bad);});
        }
        auto missing=spec;missing.patches[2].state="C";rejects([&] {ImageAnimation p(missing);});
        AnimationSpec mouths{std::string(64,'c'),{{"景","中口","kei-mid.png",2,2,1,1},{"景","開き口","kei-open.png",2,2,1,1},
            {"みやこ","中口","miy-mid.png",2,2,4,4},{"みやこ","開き口","miy-open.png",2,2,4,4}}};
        ImageAnimation speech(mouths);
        speech.voice("景",1000,0,true);require(speech.frames()==std::vector<std::size_t>{0},"Only named actor should move its mouth");
        speech.voice("景",2000,1,true);require(speech.frames()==std::vector<std::size_t>{1},"Native open mouth name mapping");
        auto speech_save=speech.checkpoint();auto speech_load=ImageAnimation::restore(mouths,speech_save);
        require(speech_load.checkpoint()==speech_save,"Lip envelope state lost on restore");
        speech.voice("景",0,2,false);speech_load.voice("景",0,2,false);require(speech.frames()==speech_load.frames() && speech.frames()==std::vector<std::size_t>{0},"Restored closing phase");
        speech.reset_voice();speech.voice("みやこ",1000,0,true);require(speech.frames()==std::vector<std::size_t>{2},"Voice replacement must reset previous speaker");
        speech.voice("unknown",1000,1,true);require(speech.frames().empty(),"Unknown speaker must not animate unrelated actors");
        speech.voice("#宫 子",1000,2,true);require(speech.frames()==std::vector<std::size_t>{2},"Localized hidden-name identity");
        speech.reset_voice();speech.voice("@Miyako",1000,3,true);require(speech.frames()==std::vector<std::size_t>{2},"Native unknown-name identity");
        speech.reset_voice();speech.voice("*",1000,4,true);require(speech.frames()==std::vector<std::size_t>({2,0}),"Native wildcard actor");
        auto old_speech=speech_save;old_speech["tracks"]=nlohmann::json::array();
        require(ImageAnimation::restore(mouths,old_speech).frames().empty(),"Previous ANI saves had no lip-only tracks");
        if (argc==2) {
            Content content(argv[1]);std::size_t count{};
            for (const auto& [id,asset]:content.assets()) {
                (void)asset;
                if (const auto* animation=content.animation(id)) {
                    ImageAnimation state(*animation,2);
                    for (int n=0;n<80;++n) state.update(.1);
                    const auto before=state.checkpoint();auto after=ImageAnimation::restore(*animation,before);
                    require(before==after.checkpoint(),"Real ANI save round trip");
                    for (const auto& p:animation->patches) if (!p.path.empty()) require(std::filesystem::is_regular_file(content.resolve(p.path)),"Missing animation PNG");
                    ++count;
                }
            }
            std::cout<<"Real animation definitions PASS: "<<count<<"\n";
        }
        std::cout<<"Image animation state, mappings, loops and persistence PASS\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
