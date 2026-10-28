#include "save/menu_layout.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ef {
namespace {
void require(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
int integer(const nlohmann::json& j,int low,int high) {
    require(j.is_number_integer() && (!j.is_number_unsigned() || j.get<std::uint64_t>()<=static_cast<std::uint64_t>(high)),"Invalid save UI integer");
    const auto value=j.get<std::int64_t>();require(value>=low && value<=high,"Save UI integer out of range");return static_cast<int>(value);
}
SaveMenuRect rect(const nlohmann::json& j) {
    require(j.is_array() && j.size()==4,"Invalid save UI rectangle");
    SaveMenuRect r{integer(j[0],0,799),integer(j[1],0,599),integer(j[2],1,800),integer(j[3],1,600)};
    require(r.left<r.right && r.top<r.bottom,"Empty save UI rectangle");return r;
}
bool overlaps(const SaveMenuRect& a,const SaveMenuRect& b) {return std::max(a.left,b.left)<std::min(a.right,b.right) && std::max(a.top,b.top)<std::min(a.bottom,b.bottom);}
std::array<int,10> navigation(const nlohmann::json& j) {
    require(j.is_array() && j.size()==10,"Invalid save UI navigation");std::array<int,10> result{};std::array<bool,10> seen{};
    for (std::size_t n=0;n<10;++n) {result[n]=integer(j[n],0,9);require(!seen[static_cast<std::size_t>(result[n])],"Duplicate save UI navigation entry");seen[static_cast<std::size_t>(result[n])]=true;}
    return result;
}
}
bool SaveMenuRect::contains(Point p) const {return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=static_cast<float>(left) && p.x<static_cast<float>(right) && p.y>=static_cast<float>(top) && p.y<static_cast<float>(bottom);}
SaveMenuLayout::SaveMenuLayout(const Content& c,const std::string& path) : SaveMenuLayout(c.read_json(path)) {
    for (const auto& [role,id]:images_) {
        const auto& asset=c.asset(id);
        if (role=="base" || role=="quick_base") require(asset.width==800 && asset.height==600,"Save UI base size mismatch");
        if (role=="selection") require(asset.width==344 && asset.height==98,"Save UI selection size mismatch");
        if (role=="empty_thumbnail") require(asset.width==thumbnail_size_[0] && asset.height==thumbnail_size_[1],"Save UI empty thumbnail size mismatch");
    }
}
SaveMenuLayout::SaveMenuLayout(const nlohmann::json& j) {
    require(j.at("format")=="ef.save_ui" && integer(j.at("version"),1,1)==1 && j.at("design_size")==nlohmann::json{800,600},"Unsupported save UI contract");
    const auto& cells=j.at("slots");require(cells.is_array() && cells.size()==10,"Save UI needs ten cells");
    for (std::size_t n=0;n<10;++n) cells_[n]=rect(cells[n]);
    const auto& buttons=j.at("controls");controls_={rect(buttons.at("previous")),rect(buttons.at("next")),rect(buttons.at("return"))};
    for (std::size_t n=0;n<cells_.size();++n) {
        for (std::size_t m=0;m<n;++m) require(!overlaps(cells_[n],cells_[m]),"Overlapping save UI cells");
        for (const auto& button:controls_) require(!overlaps(cells_[n],button),"Save UI cell overlaps control");
    }
    for (std::size_t n=0;n<controls_.size();++n) for (std::size_t m=0;m<n;++m) require(!overlaps(controls_[n],controls_[m]),"Overlapping save UI controls");
    const auto& thumb=j.at("thumbnail");
    require(thumb.at("offset").is_array() && thumb.at("offset").size()==2 && thumb.at("size").is_array() && thumb.at("size").size()==2,"Invalid thumbnail geometry");
    for (std::size_t n=0;n<2;++n) {thumbnail_offset_[n]=integer(thumb.at("offset")[n],0,800);thumbnail_size_[n]=integer(thumb.at("size")[n],1,800);}
    for (int n=0;n<10;++n) {const auto t=thumbnail(n);const auto& c=cells_[static_cast<std::size_t>(n)];require(t.right<=c.right && t.bottom<=c.bottom,"Thumbnail outside save cell");}
    manual_pages_=integer(j.at("manual_pages"),1,10);
    const auto& native=j.at("native_pages");require(native.at("first")==0 && native.at("last")==9 && native.at("first_manual")==1,"Unsupported native page contract");
    left_=navigation(j.at("navigation").at("left"));right_=navigation(j.at("navigation").at("right"));
    for (std::size_t n=0;n<10;++n) require(left_[static_cast<std::size_t>(right_[n])]==static_cast<int>(n),"Save UI left/right navigation is not reversible");
    for (const auto* role:{"base","quick_base","selection","empty_thumbnail"}) {const auto id=j.at("images").at(role).get<std::string>();require(!id.empty() && id.size()<=1024,"Invalid save UI image ID");images_.emplace(role,id);}
}
void SaveMenuLayout::validate_page(int page) const {require(page>=0 && page<=manual_pages_,"Invalid save UI page");}
SaveMenuRect SaveMenuLayout::thumbnail(int cell) const {
    require(cell>=0 && cell<10,"Invalid save UI cell");const auto& c=cells_[static_cast<std::size_t>(cell)];
    const auto x=c.left+thumbnail_offset_[0],y=c.top+thumbnail_offset_[1];return {x,y,x+thumbnail_size_[0],y+thumbnail_size_[1]};
}
int SaveMenuLayout::cell_at(Point p) const {for (std::size_t n=0;n<cells_.size();++n) if (cells_[n].contains(p)) return static_cast<int>(n);return -1;}
SaveMenuControl SaveMenuLayout::control_at(Point p) const {
    for (std::size_t n=0;n<controls_.size();++n) if (controls_[n].contains(p)) return static_cast<SaveMenuControl>(n+1);
    return SaveMenuControl::none;
}
std::optional<SaveSlot> SaveMenuLayout::slot(int page,int cell) const {
    validate_page(page);require(cell>=0 && cell<10,"Invalid save UI cell");
    if (page==0) {if (cell==0) return std::nullopt;return SaveSlot{true,cell};}
    return SaveSlot{false,(page-1)*10+cell+1};
}
int SaveMenuLayout::turn_page(int page,int delta,bool saving) const {
    validate_page(page);const auto target=static_cast<std::int64_t>(page)+delta;
    return static_cast<int>(std::clamp(target,static_cast<std::int64_t>(saving ? 1 : 0),static_cast<std::int64_t>(manual_pages_)));
}
bool SaveMenuLayout::writable(int page) const {validate_page(page);return page>0;}
int SaveMenuLayout::navigate(int selected,SaveMenuDirection direction) const {
    require(selected>=-1 && selected<10,"Invalid selected save cell");
    if (selected<0) return direction==SaveMenuDirection::left || direction==SaveMenuDirection::up ? 9 : 0;
    const auto n=static_cast<std::size_t>(selected);
    switch (direction) {
    case SaveMenuDirection::left:return left_[n];
    case SaveMenuDirection::right:return right_[n];
    case SaveMenuDirection::up:return selected%5==0 ? selected+4 : selected-1;
    case SaveMenuDirection::down:return selected%5==4 ? selected-4 : selected+1;
    }
    throw std::runtime_error("Invalid save menu direction");
}
const std::string& SaveMenuLayout::image(const std::string& role) const {return images_.at(role);}
}
