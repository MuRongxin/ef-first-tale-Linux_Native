#include "scene/cutin.hpp"
#include "scene/walking.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
ef::Layer sprite() { ef::Layer l; l.kind=ef::LayerKind::image;l.width=800;l.height=600;return l; }
}
int main() {
    try {
        using namespace ef;
        Cutin cut(sprite(),-10);
        require(cut.layer().x==800,"Cutin should begin offscreen on the right");
        cut.update(.009);require(cut.layer().x==800,"Cutin began early");
        cut.update(.001);require(cut.layer().x==798,"Cutin initial velocity");
        cut.update(.04);require(cut.layer().x==795,"Cutin first acceleration");
        cut.update(.04);require(cut.layer().x==791,"Cutin truncation");
        cut.update(2);require(cut.layer().x==0 && !cut.finished(),"Negative hold must wait indefinitely");
        cut.fade_out();cut.update(2);require(cut.finished(),"Cutin fadeout should slide out");
        Cutin left(sprite(),-1,1000);left.update(.01);require(left.layer().x==-798,"Positive acceleration should enter from the left");
        left.update(.04);require(left.layer().x==-778,"Cutin acceleration magnitude");
        left.fade_out();left.update(10);require(left.finished(),"Fadeout requested during entry was lost");
        Cutin automatic(sprite(),100,-1000);
        automatic.update(.01);
        while (automatic.layer().x!=0) automatic.update(.04);
        automatic.update(.96);require(automatic.layer().x==0,"Cutin hold counted entry time");
        automatic.update(.04);require(automatic.layer().x==-2,"Cutin hold should use 10ms ticks");
        automatic.update(86400);require(automatic.finished(),"Large delta should finish a timed Cutin");
        Cutin bulk(sprite(),1000000);bulk.update(86400);require(bulk.finished(),"Long hold fast-forward failed");
        Cutin continuous(sprite(),-1),partitioned(sprite(),-1);
        continuous.update(.37);for (int n=0;n<37;++n) partitioned.update(.01);
        require(continuous.layer().x==partitioned.layer().x,"Cutin phase depends on render frequency");
        rejects([&] { continuous.update(-1); });
        auto invalid=sprite();invalid.width=400;rejects([&] { Cutin bad(invalid,-1); });
        Walking walking(60,-2);
        auto bg=sprite();bg.width=3200;bg.part=StagePart::background;bg.asset="far";bg.screen="shade";
        auto fg=bg;fg.part=StagePart::foreground;fg.asset="near";fg.x=-1600;fg.hsv={0,-25,-8};
        auto frame=sprite();frame.part=StagePart::frame;frame.asset="frame";frame.width=810;frame.height=620;
        const std::vector<Layer> base{bg,fg,frame};
        walking.update(.099);require(walking.layers(base)[2].y==0,"Walking began before its draw interval");
        walking.update(.001);auto layers=walking.layers(base);
        require(layers.size()==3 && layers[0].x==0 && layers[1].x==-1598 && layers[2].y==-18,"Walking first native sample");
        require(layers[0].screen=="shade" && layers[1].hsv==fg.hsv,"Walking lost material state");
        walking.update(.2);layers=walking.layers(base);
        require(layers.size()==4 && layers[0].x==-3199 && layers[1].x==1 && layers[2].x==-1594,"Walking parallax and negative wrap");
        const auto frozen=layers;walking.update(1);require(frozen[0].x==-3199,"Walking mutated an old snapshot");
        Walking single(60,-2),many(60,-2);single.update(1.5);for (int n=0;n<150;++n) many.update(.01);
        const auto a=single.layers(base),b=many.layers(base);require(a.size()==b.size(),"Walking sample count mismatch");
        for (std::size_t n=0;n<a.size();++n) require(a[n].x==b[n].x && a[n].y==b[n].y,"Walking depends on frame rate");
        walking.update(86400);require(!walking.layers(base).empty(),"Large walking delta failed");
        rejects([] { Walking bad(0,-2); });rejects([&] { Walking ok(60,-2);auto bad=base;bad[0].width=400;(void)ok.layers(bad); });
        std::cout << "Effect motion PASS: native Cutin phases/timing/directions/acceleration and walking wrap/parallax/bob/materials/snapshots\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
