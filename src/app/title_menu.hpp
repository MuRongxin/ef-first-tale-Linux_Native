#pragma once
#include "app/save_menu.hpp"
#include "app/chapters.hpp"
namespace ef {
class TitleMenu {
public:
    explicit TitleMenu(const Content&);
    // 1 new, 2 continue, 3 load, 4 exit, 5 gallery, 10..13 chapter, 20..26 settings.
    int event(const SDL_Event&,std::optional<Point>);
    void draw(Renderer&,Font&,AssetTier,float,int font_percent,int text_speed,bool fullscreen);
    void reset();
private:
    enum class Page { main, memories, chapter_select, settings };
    struct Button {SaveMenuRect rect;std::string label;int action;};
    const Content& content_;
    std::string base_{"sys/topmenu2.png"},hover_{"sys/topmenu2Over.png"};
    std::vector<Button> main_;
    Page page_{Page::main};
    int selected_{-1},pressed_{-1};
    bool artwork_{};
    std::vector<Button> buttons() const;
    void page(Page);
};
}
