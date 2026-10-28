#pragma once
#include "save/slots.hpp"
#include <array>
#include <optional>
namespace ef {
struct SaveMenuRect {
    int left{},top{},right{},bottom{};
    [[nodiscard]] bool contains(Point) const;
    bool operator==(const SaveMenuRect&) const = default;
};
enum class SaveMenuControl { none,previous,next,back };
enum class SaveMenuDirection { left,right,up,down };
// Coordinates and native navigation come from the imported UI document.
// Page 0 has an unavailable auto-save cell plus q1..q9. Pages 1..10 map
// to manual slots 1..100; page 10 extends the original nine manual pages.
class SaveMenuLayout {
public:
    explicit SaveMenuLayout(const nlohmann::json&);
    SaveMenuLayout(const Content& content,const std::string& path);
    [[nodiscard]] const std::array<SaveMenuRect,10>& cells() const { return cells_; }
    [[nodiscard]] SaveMenuRect thumbnail(int cell) const;
    [[nodiscard]] int cell_at(Point) const;
    [[nodiscard]] SaveMenuControl control_at(Point) const;
    [[nodiscard]] std::optional<SaveSlot> slot(int page,int cell) const;
    [[nodiscard]] int turn_page(int page,int delta,bool saving) const;
    [[nodiscard]] int navigate(int selected,SaveMenuDirection) const;
    [[nodiscard]] bool writable(int page) const;
    [[nodiscard]] const std::string& image(const std::string& role) const;
private:
    std::array<SaveMenuRect,10> cells_;
    std::array<SaveMenuRect,3> controls_;
    std::array<int,10> left_,right_;
    std::array<int,2> thumbnail_offset_,thumbnail_size_;
    std::map<std::string,std::string> images_;
    int manual_pages_{};
    void validate_page(int) const;
};
}
