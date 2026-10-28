#include "data/gallery.hpp"
#include "data/image.hpp"
#include <png.h>
#include <webp/encode.h>
#include <fstream>
#include <chrono>
#include <iostream>
#include <set>
using namespace ef;
namespace {void require(bool b,const char* message) {if (!b) throw std::runtime_error(message);}}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("ef-gallery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        nlohmann::json doc={{"version",1},{"design_size",{800,600}},{"development",true},{"entry_scene","scene.json"},{"font",{{"path","font.ttf"}}},{"images",nlohmann::json::object()}};
        const std::vector<std::string> ids={"bg/ev01_001a01.png","bg/ev01_001b01.png","r18_bg/evh05_012a01.png","bg/ed02_001a01.png","bg/st_001.png","bg/bg001.png","bg/ev01_sky_a_sepia.png","bg/ev01_033a01.png","bg/ev01_013_kiridashi_base.png","sys/thumb/ev01_001b01.png","bg/ev02_172a01.png","r18_bg/ev01_001a02.png"};
        for (const auto& id:ids) doc["images"][id]={{"design_size",{800,600}},{"variants",{{"1",{{"path","fixture.png"},{"pixels",{800,600}}}}}}};
        std::ofstream(root/"manifest.json")<<doc.dump();
        Content content(root);GalleryCatalog catalog(content);
        require(catalog.groups.size()==4,"CG groups must include endings and no-thumbnail events");
        require(catalog.excluded.size()==3,"Exclude sky, empty room and cutin components");
        std::set<std::string> variants;
        for (const auto& g:catalog.groups) for (const auto& id:g.variants) require(variants.insert(id).second,"No duplicate variant");
        require(variants.size()==6,"All variants retained, without unlock/save input");
        const auto& first=catalog.groups.front();
        require(first.id=="ev01_001" && first.variants.size()==3,"Cross-tag variants share numbered group");
        require(first.variants.front()=="bg/ev01_001b01.png","Use observed thumbnail representative");
        require(first.thumbnails.size()==1,"Keep thumbnail correspondence");
        const auto report=catalog.report();require(report.at("variant_count")==6,"Audit count");
        Image image{80,60,std::vector<std::uint8_t>(80*60*4)};
        for (std::size_t i=0;i<image.rgba.size();i+=4) {image.rgba[i]=70;image.rgba[i+1]=100;image.rgba[i+2]=150;image.rgba[i+3]=128;}
        png_image png{};png.version=PNG_IMAGE_VERSION;png.width=80;png.height=60;png.format=PNG_FORMAT_RGBA;
        require(png_image_write_to_file(&png,(root/"fixture.png").c_str(),0,image.rgba.data(),0,nullptr),"PNG fixture");
        std::uint8_t* data{};const auto bytes=WebPEncodeLosslessRGBA(image.rgba.data(),80,60,80*4,&data);
        require(bytes>0,"WebP fixture");std::ofstream file(root/"fixture.webp",std::ios::binary);file.write(reinterpret_cast<char*>(data),static_cast<std::streamsize>(bytes));file.close();WebPFree(data);
        for (const auto name:{"fixture.png","fixture.webp"}) {
            const auto small=decode_image_scaled(root/name,16,16);require(small.width==16 && small.height==12,"Aspect-preserving bounds");
            require(small.rgba[0]==70 && small.rgba[3]==128,"Color/alpha retained");
            const auto full=decode_image(root/name);require(full.width==80 && full.rgba==image.rgba,"Full decode unchanged");
            bool rejected=false;
            try {(void)decode_image_scaled(root/name,16,16,800,600);} catch (...) {rejected=true;}
            require(rejected,"Reject declaration mismatch before scaled decode");
            const auto original=decode_image_scaled(root/name,256,192);require(original.width==80 && original.height==60,"Never upscale fallback");
        }
        Image wide{131072,1,std::vector<std::uint8_t>(131072*4,255)};
        png.width=131072;png.height=1;
        require(png_image_write_to_file(&png,(root/"wide.png").c_str(),0,wide.rgba.data(),0,nullptr),"Wide PNG fixture");
        const auto reduced=decode_image_scaled(root/"wide.png",65536,1);
        require(reduced.width==65536 && reduced.rgba.back()==255,"Wide PNG scaling must not overflow coordinate arithmetic");
        std::filesystem::remove_all(root);std::cout<<"Gallery catalog and scaled decode PASS\n";
    } catch (const std::exception& e) {std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
