#include "app/save_menu.hpp"
#include "save/thumbnail.hpp"
#include <ctime>
namespace ef {
SaveMenu::SaveMenu(const Content& c,SaveSlots& slots,bool saving,nlohmann::json data,bool allow_saving)
    : snapshot(std::move(data)),content_(c),slots_(slots),layout_(c,c.read_json(c.script_index()).at("save_ui").get<std::string>()),saving_(saving),allow_saving_(allow_saving) {refresh();}
void SaveMenu::refresh() {
    const auto listing=slots_.list(content_);
    for (int cell=0;cell<10;++cell) {
        auto& row=rows_[static_cast<std::size_t>(cell)];row=nullptr;thumbnails_[static_cast<std::size_t>(cell)].reset();
        const auto id=layout_.slot(page_,cell);if (!id) continue;
        for (const auto& entry:listing.at("slots")) if (entry.at("slot")==save_slot_name(*id)) {row=entry;break;}
        if (!row.is_null() && row.at("status")=="compatible") {
            try {const auto data=slots_.load(*id);if (data.contains("thumbnail")) thumbnails_[static_cast<std::size_t>(cell)]=std::make_unique<Texture>(decode_save_thumbnail(data.at("thumbnail")));} catch (const std::exception&) {}
        }
    }
}
void SaveMenu::page(int delta) {page_=layout_.turn_page(page_,delta,saving_);selected_=-1;pressed_=-1;refresh();}
int SaveMenu::activate() {
    if (selected_<0 || !layout_.slot(page_,selected_)) return 0;
    const auto& row=rows_[static_cast<std::size_t>(selected_)];
    if (!saving_ && (row.is_null() || row.at("status")!="compatible")) return 0;
    if (saving_) {confirming_=true;note_=row.is_null() || row.at("status")=="empty" ? "保存到此槽位？" : "此槽位已有存档。覆盖保存？";return 0;}
    return 1;
}
int SaveMenu::event(const SDL_Event& e,std::optional<Point> p) {
    if (confirming_) {
        if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.key==SDLK_ESCAPE || e.key.key==SDLK_N) {confirming_=false;return 0;}
            if (e.key.key==SDLK_RETURN || e.key.key==SDLK_Y) return 1;
        }
        if (e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT && p) {
            if (SaveMenuRect{240,310,390,355}.contains(*p)) return 1;
            if (SaveMenuRect{410,310,560,355}.contains(*p)) confirming_=false;
        }
        return 0;
    }
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        switch(e.key.key) {
        case SDLK_TAB:return allow_saving_ ? 2 : 0;
        case SDLK_ESCAPE:return -1;
        case SDLK_RETURN:return activate();
        case SDLK_PAGEUP:page(-1);break;
        case SDLK_PAGEDOWN:page(1);break;
        case SDLK_HOME:page(-100);break;
        case SDLK_END:page(100);break;
        case SDLK_LEFT:selected_=layout_.navigate(selected_,SaveMenuDirection::left);break;
        case SDLK_RIGHT:selected_=layout_.navigate(selected_,SaveMenuDirection::right);break;
        case SDLK_UP:selected_=layout_.navigate(selected_,SaveMenuDirection::up);break;
        case SDLK_DOWN:selected_=layout_.navigate(selected_,SaveMenuDirection::down);break;
        default:break;
        }
    }
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_RIGHT) return -1;
    auto hit=[&] {if (!p) return -1;if (SaveMenuRect{20,562,180,590}.contains(*p)) return 14;const auto cell=layout_.cell_at(*p);if (cell>=0) return cell;const auto control=layout_.control_at(*p);return control==SaveMenuControl::none ? -1 : 10+static_cast<int>(control);};
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) pressed_=hit();
    if (e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) {
        const auto h=hit();const auto down=pressed_;pressed_=-1;if (h<0 || h!=down) return 0;
        if (h==14) return allow_saving_ ? 2 : 0;
        if (h<10) {const bool was_selected=selected_==h;selected_=h;if (was_selected || e.button.clicks>=2) return activate();}
        else switch(static_cast<SaveMenuControl>(h-10)) {case SaveMenuControl::previous:page(-1);break;case SaveMenuControl::next:page(1);break;case SaveMenuControl::back:return -1;default:break;}
    }
    return 0;
}
void SaveMenu::draw(Renderer& renderer,Font& font,AssetTier tier,float scale) {
    auto image=[&](const std::string& id,float x,float y,float w,float h) {Layer l;l.kind=LayerKind::image;l.asset=id;l.x=x;l.y=y;l.width=w;l.height=h;renderer.image(content_,l,tier);};
    auto box=[&](float x,float y,float w,float h,Color color) {Layer l;l.kind=LayerKind::rectangle;l.x=x;l.y=y;l.width=w;l.height=h;l.color=color;renderer.rectangle(l);};
    auto text=[&](std::string s,float x,float y,float w,float size=16) {Layer l;l.kind=LayerKind::text;l.text=std::move(s);l.x=x;l.y=y;l.width=w;l.font_size=size;l.color={.95f,.95f,1,1};font.draw(renderer,l,scale);};
    image(layout_.image(page_==0 ? "quick_base" : "base"),0,0,800,600);
    for (int cell=0;cell<10;++cell) {
        const auto n=static_cast<std::size_t>(cell);const auto& r=layout_.cells()[n];const auto t=layout_.thumbnail(cell);
        if (selected_==cell) image(layout_.image("selection"),static_cast<float>(r.left),static_cast<float>(r.top),344,98);
        if (thumbnails_[n]) renderer.quad(*thumbnails_[n],static_cast<float>(t.left),static_cast<float>(t.top),100,75,{1,1,1,1});
        else image(layout_.image("empty_thumbnail"),static_cast<float>(t.left),static_cast<float>(t.top),100,75);
        const auto id=layout_.slot(page_,cell);const auto& row=rows_[n];
        const float x=static_cast<float>(r.left+122),y=static_cast<float>(r.top+9);
        box(x-2,y-2,216,83,{0,0,.08f,.72f});
        std::string label=id ? (id->quick ? "快速 " : "存档 ")+std::to_string(id->number) : "自动存档（未启用）";
        text(label,x,y,210);
        if (row.is_null() || row.at("status")=="empty") text("空槽位",x,y+25,210);
        else if (row.at("status")!="compatible") text("存档损坏或版本不兼容",x,y+25,210,14);
        else {
            const auto& metadata=row.at("metadata");const auto stamp=metadata.at("created_unix").get<std::time_t>();std::tm tm{};char date[32]{};localtime_r(&stamp,&tm);std::strftime(date,sizeof(date),"%Y-%m-%d %H:%M",&tm);
            text(date,x,y+22,210,14);
            auto excerpt=metadata.at("excerpt").get<std::string>();const auto newline=excerpt.find('\n');if (newline!=std::string::npos) excerpt.resize(newline);
            // Keep the two-line preview within the native cell.
            std::size_t end=0;int chars=0;for (;end<excerpt.size() && chars<22;++end) if ((static_cast<unsigned char>(excerpt[end])&0xc0)!=0x80) ++chars;
            while (end<excerpt.size() && (static_cast<unsigned char>(excerpt[end])&0xc0)==0x80) ++end;
            if (end<excerpt.size()) excerpt=excerpt.substr(0,end)+"…";
            text(excerpt,x,y+44,210,14);
        }
    }
    box(0,553,800,47,{0,0,.08f,.95f});
    text(allow_saving_ ? (saving_ ? "保存 → 读取" : "读取 → 保存") : "读取存档",28,565,160,18);
    text(page_==0 ? "快速页" : "第 "+std::to_string(page_)+" / 10 页",205,565,200,18);
    text("上一页",438,565,106,18);text("下一页",570,565,124,18);text("返回",710,565,70,18);
    if (confirming_) {box(0,0,800,600,{0,0,0,.65f});box(200,235,400,140,{.06f,.08f,.18f,1});text(note_,225,260,355,20);text("确定（Enter）",245,320,150,18);text("取消（Esc）",415,320,150,18);}
}
}
