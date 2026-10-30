#include "scene/animation_clock.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace ef;
void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
template<class F> void rejects(F f) {try {f();} catch (const std::exception&) {return;}throw std::runtime_error("Expected rejection");}
int main() {
    try {
        // Native e default (3): 5 + jitter(50..549), 5, 10, 5, 100...
        EyeClock e(3);require(e.update(100,0) && e.frame()==0 && e.remaining()==55,"initial eye delay");
        require(!e.update(154) && e.frame()==0,"eye boundary early");
        require(e.update(155) && e.frame()==1 && e.remaining()==5,"eye half close");
        require(e.update(160) && e.frame()==2 && e.remaining()==10,"eye fully closed");
        require(e.update(170) && e.frame()==1,"eye reopening");
        require(e.update(175) && e.frame()==0 && e.remaining()==100,"eye returns to base");
        require(!e.update(175),"same clock must not advance");
        require(e.update(10000) && e.frame()==1 && e.remaining()==5,"native discards timing debt");
        const std::uint32_t first_delay[]{40,10,10,5,3,30};
        for (int seq=0;seq<6;++seq) {
            EyeClock c(seq);c.update(500,499);
            require(c.remaining()==first_delay[seq]+(seq<5 ? 549U : 0U),"all six native table pointers/jitter bounds");
        }
        EyeClock fixed(5);fixed.update(0,499);require(fixed.remaining()==30,"sequence 5 has no jitter");
        for (int i=1;i<=4;++i) require(fixed.update(static_cast<std::uint32_t>(i*30)),"fixed cycle");
        require(fixed.frame()==0,"fixed sequence loops");
        EyeClock wrap(2);wrap.update(UINT32_MAX-10);require(!wrap.update(0),"wrapping clock elapsed 11");
        rejects([] {EyeClock bad(6);});
        LipState lip;
        require(!lip.update(799,0,true) && lip.frame()==0,"lip threshold below 800");
        require(lip.update(800,1,true) && lip.frame()==1,"lip threshold inclusive");
        require(!lip.update(2000,1,true),"lip duplicate envelope stamp");
        require(lip.update(900,2,true) && lip.frame()==2,"rising amplitude opens further");
        require(lip.update(0,3,false) && lip.frame()==1,"inactive lip closes in two stages");
        require(lip.update(0,4,false) && lip.frame()==0,"lip returns to base");
        lip.update(2000,5,true);
        require(!lip.update(1000,6,true) && lip.frame()==1,"half-amplitude holds middle state");
        require(lip.update(499,7,true) && lip.frame()==0,"below threshold closes");
        AnimationLoop tel(parse_animation_loop("@;*:13;B:13;*:13;C:13;~"));
        tel.update(0);require(tel.frame()=="*","TEL base");
        require(!tel.update(12),"TEL boundary early");
        require(tel.update(13) && tel.frame()=="B","TEL B");
        require(tel.update(1000) && tel.frame()=="*" && tel.remaining()==13,"TEL no catch up");
        require(tel.update(1013) && tel.frame()=="C","TEL C");
        require(tel.update(1026) && tel.frame()=="*","TEL loop");
        for (const auto* s:{"@;~","@;B:0;~","@;B:-1;~","@;B:13x;~","B:13","@;B:13;","@;B:4294967296;~","@;B:1:2;~"}) rejects([&] {parse_animation_loop(s);});
        std::cout<<"Animation clocks PASS\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
