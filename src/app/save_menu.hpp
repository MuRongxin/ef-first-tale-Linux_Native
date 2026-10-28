#pragma once
#include "save/menu_layout.hpp"
#include "render/renderer.hpp"
#include "text/font.hpp"
#include <SDL3/SDL.h>
namespace ef {
class SaveMenu {
public:
    SaveMenu(const Content&,SaveSlots&,bool saving,nlohmann::json snapshot,bool allow_saving=true);
    // Result: -1 close, 0 consumed, 1 commit selected slot, 2 switch save/load.
    int event(const SDL_Event&,std::optional<Point>);
    void draw(Renderer&,Font&,AssetTier,float scale);
    [[nodiscard]] SaveSlot selected_slot() const { return *layout_.slot(page_,selected_); }
    [[nodiscard]] bool saving() const { return saving_; }
    nlohmann::json snapshot;
private:
    const Content& content_;
    SaveSlots& slots_;
    SaveMenuLayout layout_;
    bool saving_{};
    bool allow_saving_{true};
    int page_{1},selected_{-1},pressed_{-1};
    bool confirming_{};
    std::string note_;
    std::array<nlohmann::json,10> rows_;
    std::array<std::unique_ptr<Texture>,10> thumbnails_;
    void refresh();
    void page(int);
    int activate();
};
}
