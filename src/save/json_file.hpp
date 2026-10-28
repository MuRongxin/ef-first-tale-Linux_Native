#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace ef {
nlohmann::json read_json_file(const std::filesystem::path&,std::size_t max_bytes);
void atomic_write_json(const std::filesystem::path&,const nlohmann::json&,std::size_t max_bytes);
}
