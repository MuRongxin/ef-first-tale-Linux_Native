#include "scene/snow.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
}
int main() {
    try {
        using namespace ef;
        std::array<Layer,3> sprites;
        for (std::size_t n=0;n<sprites.size();++n) {
            sprites[n].kind=LayerKind::image; sprites[n].asset=std::to_string(n);
            sprites[n].width=sprites[n].height=static_cast<float>((n+1)*8);
        }
        Snow first(sprites,false), same(sprites,false), horizontal(sprites,true);
        require(first.layers().empty(),"snow fades in from transparent");
        for (int n=0;n<120;++n) { first.update(1.0/60); same.update(1.0/60); horizontal.update(1.0/60); }
        const auto layers=first.layers(), repeat=same.layers(), sideways=horizontal.layers();
        require(first.opacity()==1 && layers.size()>40 && layers.size()<=50,"fifty-particle bounded effect reaches opacity");
        require(layers.size()==repeat.size() && layers.size()==sideways.size(),"deterministic particle count");
        bool different=false;
        for (std::size_t n=0;n<layers.size();++n) {
            const auto& p=layers[n];
            require(p.x==repeat[n].x && p.y==repeat[n].y && p.asset==repeat[n].asset,"fixed seed is reproducible");
            require(std::isfinite(p.x) && std::isfinite(p.y) && p.color[3]==1,"finite coordinates and alpha");
            different = different || p.x!=sideways[n].x || p.y!=sideways[n].y;
        }
        require(different,"horizontal variant has different motion");
        first.fade_out();
        for (int n=0;n<180;++n) { first.fade_out(); first.update(1.0/60); }
        require(first.finished() && first.layers().empty(),"fadeout is idempotent and eventually clears particles");
        first.update(86400); require(first.layers().empty(),"finished effect cannot restart");
        horizontal.update(86400); require(horizontal.layers().size()<=50,"large headless delta has bounded work");
        rejects([&] { same.update(-1); });
        rejects([&] { same.update(std::numeric_limits<double>::infinity()); });
        rejects([&] { same.update(std::numeric_limits<double>::quiet_NaN()); });
        std::cout << "Snow tests PASS: sprite selection, bounded particles, deterministic motion, horizontal variant and fade lifecycle\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
