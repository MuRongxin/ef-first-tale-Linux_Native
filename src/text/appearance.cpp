#include "text/appearance.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ef {
void TextAppearance::validate() const {
    if (!std::isfinite(outline_width) || outline_width<0 || outline_width>4 ||
        !std::isfinite(shadow_x) || std::abs(shadow_x)>8 || !std::isfinite(shadow_y) || std::abs(shadow_y)>8 ||
        !std::isfinite(baseline_offset) || std::abs(baseline_offset)>8)
        throw std::runtime_error("Invalid text outline, shadow or baseline offset");
    for (const auto& color : {outline_color,shadow_color})
        if (!std::all_of(color.begin(),color.end(),[](float n) { return std::isfinite(n) && n>=0 && n<=1; }))
            throw std::runtime_error("Invalid text effect color");
}
void to_json(nlohmann::json& json,const TextAppearance& value) {
    value.validate();
    json={{"outline_width",value.outline_width},{"outline_color",value.outline_color},
          {"shadow_x",value.shadow_x},{"shadow_y",value.shadow_y},{"shadow_color",value.shadow_color},
          {"baseline_offset",value.baseline_offset}};
}
void from_json(const nlohmann::json& json,TextAppearance& value) {
    if (!json.is_object()) throw std::runtime_error("Text appearance must be an object");
    TextAppearance result;
    result.outline_width=json.value("outline_width",result.outline_width);
    result.outline_color=json.value("outline_color",result.outline_color);
    result.shadow_x=json.value("shadow_x",result.shadow_x); result.shadow_y=json.value("shadow_y",result.shadow_y);
    result.shadow_color=json.value("shadow_color",result.shadow_color);
    result.baseline_offset=json.value("baseline_offset",result.baseline_offset);
    result.validate(); value=result;
}
}
