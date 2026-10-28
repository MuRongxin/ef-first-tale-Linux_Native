#pragma once
#include "data/image.hpp"
#include <nlohmann/json.hpp>
namespace ef {
// Fixed-size RGB hex keeps thumbnails bounded and inside the atomic save object.
nlohmann::json encode_save_thumbnail(const Image&);
Image decode_save_thumbnail(const nlohmann::json&);
}
