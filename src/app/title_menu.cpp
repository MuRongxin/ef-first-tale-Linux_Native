#include "app/title_menu.hpp"
namespace ef {
int TitleMenu::event(const SDL_Event& e,std::optional<Point> p) {
    auto activate=[&] {
        if (selecting_chapters) {if (selected_==4) {selecting_chapters=false;selected_=0;return 0;}return 10+selected_;}
        if (selected_==3) {selecting_chapters=true;selected_=0;return 0;}
        if (selected_==4) return 5;
        return selected_==5 ? 4 : selected_+1;
    };
    const int count=selecting_chapters ? 5 : 6;
    if (e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key==SDLK_HOME) selected_=0;
        if (e.key.key==SDLK_END) selected_=count-1;
        if (e.key.key==SDLK_DOWN) selected_=(selected_+1)%count;
        if (e.key.key==SDLK_UP) selected_=(selected_+count-1)%count;
        if (e.key.key==SDLK_RETURN || e.key.key==SDLK_SPACE) return activate();
        if (e.key.key==SDLK_ESCAPE) {if (selecting_chapters) {selecting_chapters=false;selected_=0;}else return 4;}
    }
    auto hit=[&] {if (p && p->x>=260 && p->x<540 && p->y>=230 && p->y<230+static_cast<float>(count)*50) return static_cast<int>((p->y-230)/50);return -1;};
    if (e.type==SDL_EVENT_MOUSE_MOTION && p && hit()>=0) selected_=hit();
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) pressed_=hit();
    if (e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) {
        const auto h=hit(),down=pressed_;pressed_=-1;
        if (h>=0 && h==down) {selected_=h;return activate();}
    }
    return 0;
}
void TitleMenu::draw(Renderer& r,Font& f,float scale) {
    auto text=[&](std::string value,float x,float y,float w,float size) {Layer l;l.kind=LayerKind::text;l.text=std::move(value);l.x=x;l.y=y;l.width=w;l.font_size=size;f.draw(r,l,scale);};
    Layer bg;bg.kind=LayerKind::rectangle;bg.width=800;bg.height=600;bg.color={.035f,.055f,.10f,1};r.rectangle(bg);
    text("ef - the first tale.",180,100,600,38);
    text(selecting_chapters ? "章节选择 · 全部开放" : "",260,175,450,22);
    const std::array<const char*,6> labels={"新游戏","继续","读取存档","章节选择","CG 图鉴","退出"};
    for (int i=0;i<(selecting_chapters ? 5 : 6);++i) {
        Layer b;b.kind=LayerKind::rectangle;b.x=260;b.y=230+static_cast<float>(i)*50;b.width=280;b.height=44;
        b.color=i==selected_ ? Color{.22f,.32f,.47f,1} : Color{.08f,.12f,.20f,1};r.rectangle(b);
        text(selecting_chapters ? (i==4 ? "返回" : chapters[static_cast<std::size_t>(i)].title) : labels[static_cast<std::size_t>(i)],290,b.y+8,240,22);
    }
    text("方向键选择 / 回车确认 / 鼠标点击",220,560,520,16);
}
}
