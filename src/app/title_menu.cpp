#include "app/title_menu.hpp"
namespace ef {
TitleMenu::TitleMenu(const Content& c):content_(c) {
    // The fallback keeps older/minimal packages usable. Coordinates are the native title RECT table.
    main_={{{680,43,800,71},"新游戏",1},{{680,71,800,102},"读取存档",3},
           {{680,102,800,132},"系统设置",-2},{{680,132,800,157},"回忆鉴赏",-3},{{680,157,800,186},"退出游戏",4}};
    const auto index=!c.script_index().empty() && std::filesystem::exists(c.resolve(c.script_index())) ? c.read_json(c.script_index()) : nlohmann::json::object();
    if (index.contains("title_ui")) {
        const auto j=c.read_json(index.at("title_ui").get<std::string>());
        if (j.at("version")!=1 || j.at("design_size")!=nlohmann::json::array({800,600})) throw std::runtime_error("Invalid title UI layout");
        base_=j.at("base");hover_=j.at("hover");
        const auto& rows=j.at("buttons");
        if (rows.size()!=main_.size()) throw std::runtime_error("Title UI requires five buttons");
        for (std::size_t i=0;i<rows.size();++i) {
            const auto r=rows[i].at("rect").get<std::array<int,4>>();
            if (r[0]<0 || r[1]<0 || r[2]>800 || r[3]>600 || r[0]>=r[2] || r[1]>=r[3]) throw std::runtime_error("Invalid title button rectangle");
            main_[i].rect={r[0],r[1],r[2],r[3]};main_[i].label=rows[i].at("label");
        }
    }
    artwork_=c.assets().contains(base_) && c.assets().contains(hover_);
    if (artwork_) for (const auto& id:{base_,hover_}) {
        const auto& a=c.asset(id);if (a.width!=800 || a.height!=600) throw std::runtime_error("Title artwork must use an 800x600 design canvas");
    }
}
void TitleMenu::page(Page p) {page_=p;selected_=0;pressed_=-1;}
void TitleMenu::reset() {page_=Page::main;selected_=-1;pressed_=-1;}
std::vector<TitleMenu::Button> TitleMenu::buttons() const {
    if (page_==Page::main) return main_;
    std::vector<std::pair<std::string,int>> items;
    if (page_==Page::memories) items={{"章节选择 · 全部开放",-4},{"CG 图鉴 · 全部开放",5},{"继续最近存档",2},{"返回首页",-1}};
    if (page_==Page::chapter_select) {for (int i=0;i<4;++i) items.emplace_back(chapters[static_cast<std::size_t>(i)].title,10+i);items.emplace_back("返回鉴赏",-3);}
    if (page_==Page::settings) items={{"窗口 / 无边框全屏",20},{"缩小字号",21},{"放大字号",22},{"恢复默认字体",23},{"切换文字描边 / 阴影",24},{"降低文字速度",25},{"提高文字速度",26},{"返回首页",-1}};
    std::vector<Button> result;
    for (std::size_t i=0;i<items.size();++i) {const int y=175+static_cast<int>(i)*43;result.push_back({{220,y,580,y+36},items[i].first,items[i].second});}
    return result;
}
int TitleMenu::event(const SDL_Event& e,std::optional<Point> p) {
    const auto rows=buttons();const int count=static_cast<int>(rows.size());
    auto activate=[&]() {
        pressed_=-1;
        if (selected_<0 || selected_>=count) return 0;
        const auto action=rows[static_cast<std::size_t>(selected_)].action;
        if (action==-1) page(Page::main);
        if (action==-2) page(Page::settings);
        if (action==-3) page(Page::memories);
        if (action==-4) page(Page::chapter_select);
        return action>0 ? action : 0;
    };
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        pressed_=-1;
        if (e.key.key==SDLK_HOME) selected_=0;
        if (e.key.key==SDLK_END) selected_=count-1;
        if (e.key.key==SDLK_DOWN) selected_=selected_<0 ? 0 : (selected_+1)%count;
        if (e.key.key==SDLK_UP) selected_=selected_<0 ? count-1 : (selected_+count-1)%count;
        if (e.key.key==SDLK_RETURN || e.key.key==SDLK_SPACE) return activate();
        if (e.key.key==SDLK_C && page_==Page::main) return 2;
        if (e.key.key==SDLK_ESCAPE) {if (page_==Page::main) return 4;page(page_==Page::chapter_select ? Page::memories : Page::main);}
    }
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_RIGHT) {if (page_!=Page::main) page(page_==Page::chapter_select ? Page::memories : Page::main);return 0;}
    if (e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_WINDOW_MOUSE_LEAVE) {pressed_=-1;selected_=-1;}
    auto hit=[&] {if (p) for (int i=0;i<count;++i) if (rows[static_cast<std::size_t>(i)].rect.contains(*p)) return i;return -1;};
    if (e.type==SDL_EVENT_MOUSE_MOTION) selected_=hit();
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) pressed_=hit();
    if (e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) {
        const auto h=hit(),down=pressed_;pressed_=-1;
        if (h>=0 && h==down) {selected_=h;return activate();}
    }
    return 0;
}
void TitleMenu::draw(Renderer& r,Font& f,AssetTier tier,float scale,int font_percent,int text_speed,bool fullscreen) {
    auto text=[&](std::string value,float x,float y,float w,float size) {Layer l;l.kind=LayerKind::text;l.text=std::move(value);l.x=x;l.y=y;l.width=w;l.font_size=size;l.appearance.shadow_x=l.appearance.shadow_y=1;f.draw(r,l,scale);};
    auto box=[&](float x,float y,float w,float h,Color color) {Layer l;l.kind=LayerKind::rectangle;l.x=x;l.y=y;l.width=w;l.height=h;l.color=color;r.rectangle(l);};
    if (artwork_) {
        Layer l;l.kind=LayerKind::image;l.asset=base_;l.width=800;l.height=600;r.image(content_,l,tier);
        if (page_==Page::main && selected_>=0) {
            const auto a=main_[static_cast<std::size_t>(selected_)].rect;
            l.asset=hover_;l.x=static_cast<float>(a.left);l.y=static_cast<float>(a.top);l.width=static_cast<float>(a.right-a.left);l.height=static_cast<float>(a.bottom-a.top);
            l.source=std::array<float,4>{l.x,l.y,l.width,l.height};r.image(content_,l,tier);
        }
    } else {box(0,0,800,600,{.12f,.09f,.07f,1});text("ef - the first tale.",30,30,500,32);}
    if (page_==Page::main) {
        if (!artwork_) for (const auto& b:main_) text(b.label,static_cast<float>(b.rect.left),static_cast<float>(b.rect.top),150,20);
        if (selected_>=0) {box(250,550,300,32,{.12f,.07f,.03f,.8f});text(main_[static_cast<std::size_t>(selected_)].label,275,555,260,18);}
        return;
    }
    box(0,0,800,600,{.05f,.03f,.02f,.65f});box(195,90,410,460,{.16f,.11f,.08f,.95f});
    text(page_==Page::memories ? "回忆鉴赏" : page_==Page::chapter_select ? "章节选择 · 全部开放" : "系统设置",225,108,360,25);
    if (page_==Page::settings) text("字号 "+std::to_string(font_percent)+"%  文字 "+std::to_string(text_speed)+" 字/秒  "+(fullscreen ? "全屏" : "窗口"),225,145,370,15);
    const auto rows=buttons();
    for (std::size_t i=0;i<rows.size();++i) {
        const auto& b=rows[i];const auto& a=b.rect;
        if (static_cast<int>(i)==selected_) box(static_cast<float>(a.left),static_cast<float>(a.top),360,36,{.55f,.34f,.17f,.85f});
        text(b.label,static_cast<float>(a.left+12),static_cast<float>(a.top+5),336,20);
    }
    if (page_==Page::settings) text("拖入字体文件即可更换字体；设置即时生效。",220,525,380,15);
}
}
