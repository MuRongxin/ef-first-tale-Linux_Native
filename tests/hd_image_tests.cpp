#include "data/content.hpp"
#include "data/image.hpp"
#include <png.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ef;
namespace {
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
template<class F> void rejects(F action) {try {action();} catch (const std::exception&) {return;}throw std::runtime_error("Expected rejection");}
void write(const std::filesystem::path& path,const nlohmann::json& doc) {std::ofstream(path)<<doc.dump();}
void panorama(const std::filesystem::path& path) {
    std::vector<std::uint8_t> row(40000*3);
    auto* output=std::fopen(path.c_str(),"wb");require(output!=nullptr,"Open fixture");
    auto* png=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
    auto* info=png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {png_destroy_write_struct(&png,&info);std::fclose(output);throw std::runtime_error("Write fixture");}
    png_init_io(png,output);png_set_compression_level(png,1);
    png_set_IHDR(png,info,40000,3400,8,PNG_COLOR_TYPE_RGB,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    for (std::size_t i=0;i<row.size();i+=3) {row[i]=70;row[i+1]=100;row[i+2]=150;}
    for (int y=0;y<3400;++y) png_write_row(png,row.data());
    png_write_end(png,info);png_destroy_write_struct(&png,&info);std::fclose(output);
}
}
int main() {
    const auto root=std::filesystem::current_path()/("test-hd-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        std::vector<std::uint8_t> pixels(16*16*4);
        for (int y=0;y<16;++y) for (int x=0;x<16;++x) {
            const auto i=static_cast<std::size_t>(y*16+x)*4;
            pixels[i]=y<8 ? 255 : 0;pixels[i+2]=y<8 ? 0 : 255;pixels[i+3]=128;
        }
        png_image png{};png.version=PNG_IMAGE_VERSION;png.width=16;png.height=16;png.format=PNG_FORMAT_RGBA;
        require(png_image_write_to_file(&png,(root/"hd.png").c_str(),0,pixels.data(),0,nullptr),"Write HD fixture");
        nlohmann::json doc={{"version",1},{"design_size",{800,600}},{"development",false},
            {"entry_scene","scene.json"},{"font",{{"path","font.ttf"}}},
            {"images",{
                {"bg/source.png",{{"design_size",{4,4}},{"variants",{{"4",{{"path","hd.png"},{"pixels",{16,16}}}}}}}},
                {"sys/thumb/source.png",{{"design_size",{2,1}},{"variants",nlohmann::json::object()},
                    {"derived_from",{{"asset","bg/source.png"},{"crop",{0,2,4,2}}}}}}
            }}};
        write(root/"manifest.json",doc);Content content(root);
        const auto choices=content.candidates("sys/thumb/source.png",AssetTier::x8);
        require(choices.size()==1 && choices[0].tier==4 && choices[0].path=="hd.png","Thumbnail always derives from HD 4x");
        const auto thumb=content.decode_variant(choices[0],100,100);
        require(thumb.width==8 && thumb.height==4,"Derived target respects thumbnail design size");
        for (std::size_t i=0;i<thumb.rgba.size();i+=4)
            require(thumb.rgba[i]==0 && thumb.rgba[i+2]==255 && thumb.rgba[i+3]==128,"Crop color and soft alpha");
        rejects([&] {content.decode_variant(choices[0],0,100);});
        for (const auto& derived:nlohmann::json::array({
            {{"asset","missing"},{"crop",{0,0,4,4}}},
            {{"asset","sys/thumb/source.png"},{"crop",{0,0,2,1}}},
            {{"asset","bg/source.png"},{"crop",{0,3,4,2}}},
            {{"asset","bg/source.png"},{"crop",{-1,0,4,2}}}})) {
            auto bad=doc;bad["images"]["sys/thumb/source.png"]["derived_from"]=derived;
            write(root/"manifest.json",bad);rejects([&] {Content rejected(root);});
        }
        auto bad=doc;bad["images"]["sys/thumb/source.png"]["variants"]=doc["images"]["bg/source.png"]["variants"];
        write(root/"manifest.json",bad);rejects([&] {Content rejected(root);});
        panorama(root/"wide.png");
        const auto bounded=decode_image_scaled(root/"wide.png",400,100,40000,3400);
        require(bounded.width==400 && bounded.height==34,"PNG larger than 512 MiB decodes with bounded memory");
        for (std::size_t i=0;i<bounded.rgba.size();i+=4)
            require(bounded.rgba[i]==70 && bounded.rgba[i+1]==100 && bounded.rgba[i+2]==150 && bounded.rgba[i+3]==255,"Streaming area average/RGB alpha");
        rejects([&] {decode_image(root/"wide.png");});
        rejects([&] {decode_image_scaled(root/"wide.png",400,100,39999,3400);});
        std::filesystem::resize_file(root/"wide.png",std::filesystem::file_size(root/"wide.png")/2);
        rejects([&] {decode_image_scaled(root/"wide.png",400,100);});
        std::filesystem::remove_all(root);std::cout<<"HD image tests PASS\n";return 0;
    } catch (const std::exception& e) {std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
