#include "save/menu_layout.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
namespace {
void require(bool v,const char* message) {if (!v) throw std::runtime_error(message);}
template<class F> void rejects(F f) {try {f();} catch(const std::exception&) {return;}throw std::runtime_error("Expected layout rejection");}
}
int main() {
    try {
        using namespace ef;nlohmann::json document;std::ifstream(EF_SAVE_UI_FIXTURE)>>document;const SaveMenuLayout layout(document);
        require(layout.cells()[0]==SaveMenuRect{31,27,375,126} && layout.cells()[9]==SaveMenuRect{425,451,769,549},"Native cell table changed");
        for (int n=0;n<10;++n) {
            const auto& r=layout.cells()[static_cast<std::size_t>(n)];
            require(layout.cell_at({static_cast<float>(r.left),static_cast<float>(r.top)})==n,"Cell left/top edge excluded");
            require(layout.cell_at({static_cast<float>(r.right),static_cast<float>(r.top)})==-1,"Cell right edge included");
            require(layout.cell_at({static_cast<float>(r.left),static_cast<float>(r.bottom)})==-1,"Cell bottom edge included");
            require(layout.navigate(layout.navigate(n,SaveMenuDirection::left),SaveMenuDirection::right)==n,"Horizontal keyboard cycle lost inverse");
            require(layout.navigate(layout.navigate(n,SaveMenuDirection::up),SaveMenuDirection::down)==n,"Vertical keyboard cycle lost inverse");
            const auto t=layout.thumbnail(n);require(t.left==r.left+12 && t.top==r.top+12 && t.right-t.left==100 && t.bottom-t.top==75,"Thumbnail geometry drift");
        }
        require(layout.cell_at({400,300})==-1 && layout.cell_at({-1,0})==-1 && layout.cell_at({std::numeric_limits<float>::quiet_NaN(),0})==-1,"Gap/nonfinite hit accepted");
        require(layout.control_at({431,562})==SaveMenuControl::previous && layout.control_at({544,562})==SaveMenuControl::none,"Previous button boundary drift");
        require(layout.control_at({562,562})==SaveMenuControl::next && layout.control_at({700,562})==SaveMenuControl::back && layout.control_at({770,562})==SaveMenuControl::none,"Next/return boundary drift");
        require(!layout.slot(0,0) && !layout.writable(0) && layout.slot(0,1)==SaveSlot{true,1} && layout.slot(0,9)==SaveSlot{true,9},"Quick/auto page mapping");
        std::set<int> manual;
        for (int page=1;page<=10;++page) for (int cell=0;cell<10;++cell) {const auto id=*layout.slot(page,cell);require(!id.quick && layout.writable(page),"Manual cell mapped to quick");manual.insert(id.number);}
        require(manual.size()==100 && *manual.begin()==1 && *manual.rbegin()==100,"Manual extension lost slots");
        require(layout.turn_page(0,-1,false)==0 && layout.turn_page(1,-1,true)==1 && layout.turn_page(10,1,false)==10,"Page boundaries changed");
        require(layout.turn_page(10,std::numeric_limits<int>::max(),true)==10 && layout.turn_page(0,std::numeric_limits<int>::min(),false)==0,"Page delta overflow");
        require(layout.navigate(5,SaveMenuDirection::right)==1 && layout.navigate(0,SaveMenuDirection::left)==9,"Native asymmetric horizontal order simplified");
        require(layout.navigate(-1,SaveMenuDirection::left)==9 && layout.navigate(-1,SaveMenuDirection::down)==0,"Initial keyboard selection");
        rejects([&]{(void)layout.slot(11,0);});rejects([&]{(void)layout.slot(1,10);});rejects([&]{(void)layout.navigate(10,SaveMenuDirection::up);});
        for (int kind=0;kind<9;++kind) {auto bad=document;
            if (kind==0) bad["version"]=2;
            if (kind==1) bad["slots"][0][2]=801;
            if (kind==2) bad["slots"][1]=bad["slots"][0];
            if (kind==3) bad["thumbnail"]["size"]={100,100};
            if (kind==4) bad["controls"]["next"]=bad["controls"]["previous"];
            if (kind==5) bad["navigation"]["left"][0]=0;
            if (kind==6) bad["manual_pages"]=11;
            if (kind==7) bad["manual_pages"]=true;
            if (kind==8) bad["slots"][0][0]=31.5;
            rejects([&]{SaveMenuLayout invalid(bad);});
        }
        std::cout<<"Save menu layout PASS: native geometry, half-open hits, asymmetric keyboard navigation, quick/manual page mapping and validation\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
