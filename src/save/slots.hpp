#pragma once
#include "save/checkpoint.hpp"
namespace ef {
struct SaveSlot {bool quick{};int number{};bool operator==(const SaveSlot&) const = default;};
SaveSlot parse_save_slot(const std::string&); // 1..100 or q1..q9; latest is resolved by the store.
std::string save_slot_name(SaveSlot);
std::filesystem::path default_save_directory();
class SaveSlots {
public:
    explicit SaveSlots(std::filesystem::path root);
    void save_manual(int number,const nlohmann::json&);
    SaveSlot save_quick(const nlohmann::json&);
    [[nodiscard]] nlohmann::json load(SaveSlot) const;
    [[nodiscard]] nlohmann::json load_latest_quick() const;
    [[nodiscard]] nlohmann::json list(const Content&) const;
private:
    std::filesystem::path root_;
    SaveSlot publish(SaveSlot,const nlohmann::json&,bool rotate);
};
}
