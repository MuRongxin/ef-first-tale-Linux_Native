#pragma once
#include "app/save_menu.hpp"
#include "app/chapters.hpp"
namespace ef {
class TitleMenu {
public:
    // 1 new, 2 continue, 3 load, 4 exit, 5 gallery, 10..13 chapter.
    int event(const SDL_Event&,std::optional<Point>);
    void draw(Renderer&,Font&,float);
    bool selecting_chapters{};
private:
    int selected_{},pressed_{-1};
};
}
