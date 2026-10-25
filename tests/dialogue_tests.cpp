#include "script/dialogue.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected dialogue rejection");
}
}
int main() {
    try {
        using namespace ef;
        Dialogue unicode("中A😀文",10);
        unicode.update(.05,false);
        require(unicode.text().empty(),"partial character interval");
        unicode.update(.05,false);
        require(unicode.text()=="中","UTF-8 character costs one interval, not three bytes");
        unicode.update(.2,false);
        require(unicode.text()=="中A😀","supplementary code point remains complete UTF-8");
        unicode.update(.1,false);
        require(unicode.complete() && unicode.text()=="中A😀文","typed text completes");

        Dialogue controls("前\\n后\\\\终\\a",0);
        require(!controls.automatic(),"automatic flag is encountered during playback");
        controls.update(0,false);
        require(controls.text()=="前\n后\\终" && controls.complete() && controls.automatic(),"line break, literal backslash, automatic completion");
        Dialogue automatic_only("\\a",30);
        automatic_only.update(0,false);
        require(automatic_only.complete() && automatic_only.automatic() && automatic_only.text().empty(),"empty automatic message needs no click");

        Dialogue delay("一\\w二\\a",0);
        delay.reveal();
        require(delay.text()=="一" && !delay.complete() && !delay.automatic(),"reveal stops at fixed wait");
        delay.reveal(); delay.update(.04,false); delay.reveal();
        require(delay.text()=="一","repeated reveal does not erase the fixed pause");
        delay.update(.06,false);
        require(delay.text()=="一二" && delay.complete() && delay.automatic(),"wait resumes after 100ms");
        Dialogue trailing_wait("甲\\w",0);
        trailing_wait.update(0,false);
        require(!trailing_wait.complete(),"trailing wait delays completion");
        trailing_wait.update(.1,false);
        require(trailing_wait.complete(),"trailing wait completes at its deadline");

        Dialogue voice("前\\v后\\a",0);
        voice.update(0,true);
        voice.reveal(); voice.update(100,true); voice.reveal();
        require(voice.text()=="前" && !voice.complete() && !voice.automatic(),"playing voice remains a reveal barrier");
        voice.update(0,false);
        require(voice.text()=="前后" && voice.complete() && voice.automatic(),"voice finish resumes text and automatic control");
        Dialogue silent("前\\v后",0);
        silent.update(0,false);
        require(silent.complete() && silent.text()=="前后","missing voice imposes no delay");
        Dialogue conservative("\\v尾",0);
        conservative.reveal();
        require(conservative.text().empty(),"reveal before first update conservatively waits for voice");
        conservative.update(0,false);
        require(conservative.complete(),"initial voice status can be supplied without elapsed time");

        Dialogue fast("一二\\w三四\\v五六\\a",10);
        fast.update(.05,true); fast.reveal();
        require(fast.text()=="一二","fast reveal stops before wait");
        fast.update(.1,true); fast.reveal();
        require(fast.text()=="一二三四" && !fast.complete(),"fast reveal can reach but not cross a playing voice");
        fast.update(0,false); fast.reveal();
        require(fast.text()=="一二三四五六" && fast.complete() && fast.automatic(),"reveal after voice release");
        Dialogue partitioned("甲乙\\w丙丁",10), whole("甲乙\\w丙丁",10);
        for (int i=0; i<50; ++i) partitioned.update(.01,false);
        whole.update(.5,false);
        require(partitioned.text()==whole.text() && partitioned.complete() && whole.complete(),"frame partition does not change the timeline");
        Dialogue large("a\\wb\\wc",0);
        large.update(std::numeric_limits<double>::max(),false);
        require(large.complete() && large.text()=="abc","large finite delta stays bounded");
        Dialogue extreme_speed("a",1e15);
        extreme_speed.update(0,false);
        require(extreme_speed.text().empty(),"nonzero character intervals are not rounded into instant text");
        extreme_speed.update(1e-15,false);
        require(extreme_speed.complete(),"very high finite speed remains meaningful");
        Dialogue slow("a",1e-308);
        slow.update(5e307,false); slow.update(5e307,false);
        require(slow.complete(),"large accumulated intervals do not overflow or lose credit");
        Dialogue empty("",0);
        require(empty.complete() && !empty.automatic(),"empty plain dialogue");
        for (const auto* text : {"\\q","\\","bad\\x{1}","\xc0\xaf"}) rejects([&] { Dialogue invalid(text,10); });
        rejects([] { Dialogue invalid(std::string("a\0b",3),10); });
        for (const double value : {-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            rejects([&] { Dialogue invalid("a",value); });
            rejects([&] { empty.update(value,false); });
        }
        rejects([] { Dialogue invalid("a",std::numeric_limits<double>::denorm_min()); });
        rejects([] { Dialogue invalid(std::string(65537,'a'),0); });
        rejects([] { Dialogue invalid(std::string(1024*1024+1,'a'),0); });
        Dialogue limit(std::string(65536,'a'),0);
        limit.update(0,false);
        require(limit.complete(),"maximum token count terminates without an off-by-one budget failure");
        Dialogue plain("前\\w中\\v后\\a\\n\\\\",10);
        require(plain.text().empty() && plain.full_text()=="前中后\n\\","full plaintext remains available before reveal for stable layout");
        std::cout << "Dialogue tests PASS: Unicode clock, reveal barriers, fixed pauses, voice synchronization, automatic messages and bounds\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
