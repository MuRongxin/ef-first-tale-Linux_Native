#include "save/thumbnail.hpp"
#include <stdexcept>
namespace ef {
nlohmann::json encode_save_thumbnail(const Image& image) {
    if (image.width!=100 || image.height!=75 || image.rgba.size()!=30000) throw std::runtime_error("Invalid save thumbnail dimensions");
    constexpr char digits[]="0123456789abcdef";std::string rgb;rgb.reserve(45000);
    for (std::size_t i=0;i<image.rgba.size();++i) if (i%4!=3) {const auto b=image.rgba[i];rgb+=digits[b>>4];rgb+=digits[b&15];}
    return {{"format","rgb8-hex"},{"width",100},{"height",75},{"pixels",rgb}};
}
Image decode_save_thumbnail(const nlohmann::json& j) {
    if (!j.is_object() || j.size()!=4 || j.at("format")!="rgb8-hex" || j.at("width")!=100 || j.at("height")!=75 || !j.at("pixels").is_string()) throw std::runtime_error("Invalid save thumbnail");
    const auto& s=j.at("pixels").get_ref<const std::string&>();if (s.size()!=45000) throw std::runtime_error("Invalid save thumbnail size");
    auto hex=[](char c) -> unsigned char {if (c>='0' && c<='9') return static_cast<unsigned char>(c-'0');if (c>='a' && c<='f') return static_cast<unsigned char>(c-'a'+10);throw std::runtime_error("Invalid save thumbnail pixels");};
    Image image{100,75,std::vector<std::uint8_t>(30000,255)};
    for (std::size_t n=0;n<22500;++n) image.rgba[n/3*4+n%3]=static_cast<std::uint8_t>((hex(s[n*2])<<4)|hex(s[n*2+1]));
    return image;
}
}
