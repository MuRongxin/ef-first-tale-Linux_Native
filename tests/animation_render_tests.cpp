#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <chrono>
#include <fstream>
#include <iostream>
namespace {
using namespace ef;using J=nlohmann::json;using Pixel=std::array<unsigned char,4>;
void require(bool v,const char* s) {if (!v) throw std::runtime_error(s);}
void png(const std::filesystem::path& path,int w,int h,const std::vector<unsigned char>& pixels) {
    png_image image{};image.version=PNG_IMAGE_VERSION;image.width=static_cast<unsigned>(w);image.height=static_cast<unsigned>(h);image.format=PNG_FORMAT_RGBA;
    require(png_image_write_to_file(&image,path.c_str(),0,pixels.data(),0,nullptr),"PNG fixture");
}
Pixel read(int x,int y) {Pixel p{};glReadPixels(x,599-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
void near(Pixel a,Pixel b) {for (int i=0;i<4;++i) if (std::abs(int(a[i])-b[i])>2) throw std::runtime_error("ANI pixel mismatch channel "+std::to_string(i)+" value "+std::to_string(a[i])+" expected "+std::to_string(b[i]));}
struct Fixture {
    std::filesystem::path path=std::filesystem::current_path()/("test-ani-gl-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {std::filesystem::create_directory(path);}
    ~Fixture() {std::error_code e;std::filesystem::remove_all(path,e);}
};
struct Window {
    SDL_Window* window{};SDL_GLContext context{};
    Window() {require(SDL_Init(SDL_INIT_VIDEO),"SDL init");SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);window=SDL_CreateWindow("ANI pixels",800,600,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);require(window,"SDL window");context=SDL_GL_CreateContext(window);require(context,"GL context");}
    ~Window() {SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();}
};
}
int main() {
    try {
        Fixture f;J variants=J::object();
        for (int tier:{4,8}) {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(8*8*tier*tier*4));
            for (std::size_t i=0;i<pixels.size();i+=4) {pixels[i+1]=pixels[i+3]=255;}
            const auto name="base"+std::to_string(tier)+".png";png(f.path/name,8*tier,8*tier,pixels);
            variants[std::to_string(tier)]={{"path",name},{"pixels",{8*tier,8*tier}}};
        }
        png(f.path/"patch.png",2,2,{255,0,0,255,255,0,0,0,255,0,0,128,0,0,255,255});
        std::vector<unsigned char> band;
        for (int i=0;i<9;++i) for (int c=0;c<4;++c) band.push_back(c==3 || i<4 ? 255 : 0);
        png(f.path/"band.png",1,9,band);
        const auto hash=std::string(64,'a');
        J manifest={{"version",1},{"development",true},{"design_size",{800,600}},{"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"animations","animations.json"},{"images",J::object()}};
        manifest["images"]["base"]={{"source_sha256",hash},{"design_size",{8,8}},{"variants",variants}};
        manifest["images"]["band"]={{"design_size",{1,9}},{"variants",{{"1",{{"path","band.png"},{"pixels",{1,9}}}}}}};
        J spec={{"source_sha256",hash},{"base_sha256",hash},{"records",J::array({
            {{"actor","TEL"},{"state","B"},{"path","patch.png"},{"size",{2,2}},{"offset",{3,2}}},
            {{"actor","TEL"},{"state","@;*:1;B:1;~"},{"size",{0,0}},{"offset",{0,0}}},
            {{"actor","景"},{"state","中口"},{"path","patch.png"},{"size",{2,2}},{"offset",{0,6}}},
            {{"actor","みやこ"},{"state","中口"},{"path","patch.png"},{"size",{2,2}},{"offset",{6,6}}}})}};
        std::ofstream(f.path/"animations.json")<<J{{"format","ef.animations"},{"version",1},{"source_scale",1},{"images",{{"base",spec}}}};
        std::ofstream(f.path/"manifest.json")<<manifest;
        Content content(f.path);Window window;Renderer renderer;Layer layer;layer.kind=LayerKind::image;layer.asset="base";layer.width=800;layer.height=600;layer.animation.emplace(*content.animation("base"),5);
        auto draw=[&](AssetTier tier) {renderer.begin(800,600,{0,0,800,600},{0,0,0,1});renderer.stage(content,{}, {layer},1,1,tier);require(glGetError()==GL_NO_ERROR,"ANI GL state");};
        for (auto tier:{AssetTier::x4,AssetTier::x8,AssetTier::x4}) {
            draw(tier);near(read(350,187),{0,255,0,255});
            layer.animation->update(.01);draw(tier);
            near(read(350,187),{255,0,0,255});near(read(450,187),{0,255,0,255});near(read(350,262),{128,127,0,255});near(read(450,262),{0,0,255,255});
            const auto frozen=layer;
            layer.animation->update(.01);draw(tier);near(read(350,187),{0,255,0,255});
            renderer.begin(800,600,{0,0,800,600},{0,0,0,1});renderer.stage(content,{frozen},{layer},0,77,tier);near(read(350,187),{255,0,0,255});
            renderer.invalidate_scene();
        }
        layer.animation->update(.01);layer.screen="band";draw(AssetTier::x8);
        near(read(350,187),{254,254,254,255});near(read(450,262),{0,0,254,255});
        layer.screen.clear();layer.trim_bottom=5;layer.height=225;draw(AssetTier::x4);
        near(read(350,187),{255,0,0,255});near(read(450,262),{0,0,0,255});
        layer.trim_bottom=0;layer.height=600;layer.source=std::array<float,4>{3,2,2,2};draw(AssetTier::x4);
        near(read(200,150),{255,0,0,255});near(read(600,150),{0,255,0,255});near(read(600,450),{0,0,255,255});
        renderer.clear_cache();draw(AssetTier::x8);near(read(200,150),{255,0,0,255});
        layer.source.reset();layer.animation.emplace(*content.animation("base"),5);
        for (auto tier:{AssetTier::x4,AssetTier::x8}) {
            layer.animation->reset_voice();layer.animation->voice("景",1000,1,true);draw(tier);
            near(read(50,487),{255,0,0,255});near(read(650,487),{0,255,0,255});
            layer.animation->voice("#宫子",1000,2,true);draw(tier);
            near(read(50,487),{0,255,0,255});near(read(650,487),{255,0,0,255});
            layer.animation->reset_voice();draw(tier);near(read(650,487),{0,255,0,255});
        }
        std::cout<<"ANI GPU alpha, crop, atlas, material coordinates, snapshots and 4x/8x PASS\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
