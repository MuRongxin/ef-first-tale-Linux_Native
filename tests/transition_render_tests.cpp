#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using Pixel=std::array<unsigned char,4>;
using Pixels=std::vector<Pixel>;
constexpr int ww=1200,wh=900;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Temporary {
    std::filesystem::path path=std::filesystem::current_path()/("test-transition-render-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored;std::filesystem::remove_all(path,ignored); }
};
struct Window {
    SDL_Window* window{};SDL_GLContext context{};
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Transition test",ww,wh,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if (window) context=SDL_GL_CreateContext(window);
        if (!context) { if (window) SDL_DestroyWindow(window);SDL_Quit();throw std::runtime_error("No GL context"); }
    }
    ~Window() { SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit(); }
};
Pixels read(ef::Viewport view) {
    Pixels raw(static_cast<std::size_t>(view.width)*static_cast<std::size_t>(view.height)),result(raw.size());
    glReadPixels(view.x,wh-view.y-view.height,view.width,view.height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
    for (int y=0;y<view.height;++y) std::copy_n(raw.begin()+static_cast<std::ptrdiff_t>((view.height-1-y)*view.width),view.width,result.begin()+static_cast<std::ptrdiff_t>(y*view.width));
    return result;
}
void require_pixels(const Pixels& actual,const Pixels& expected,const std::string& where) {
    require(actual.size()==expected.size(),"Pixel size mismatch");
    for (std::size_t n=0;n<actual.size();++n) if (actual[n]!=expected[n])
        throw std::runtime_error(where+" pixel "+std::to_string(n)+": got "+std::to_string(actual[n][0])+","+std::to_string(actual[n][1])+" expected "+std::to_string(expected[n][0])+","+std::to_string(expected[n][1]));
}
Pixels expansion(const Pixels& old,const Pixels& next,int type,int step) {
    const int w=type==16 ? 800 : step,h=type==17 ? 600 : type==16 ? step : step*600/800;
    auto result=old;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) result[static_cast<std::size_t>(y*800+x)]=next[static_cast<std::size_t>((y*600/h)*800+x*800/w)];
    return result;
}
void zoom_step(Pixels& history,const Pixels& source,int phase) {
    const int index=phase<200 ? phase : 399-phase;
    char text[64]{};std::snprintf(text,sizeof(text),"%.10g",1+99*std::pow(static_cast<double>(index)/199,2));
    const double scale=std::stod(text);
    const int w=static_cast<int>(800/scale),h=static_cast<int>(600/scale),left=(800-w)/2,top=(600-h)/2;
    for (int y=0;y<600;++y) for (int x=0;x<800;++x) {
        auto& pixel=history[static_cast<std::size_t>(y*800+x)];
        const auto sample=source[static_cast<std::size_t>((top+y*h/600)*800+left+x*w/800)];
        for (std::size_t c=0;c<3;++c) pixel[c]=static_cast<unsigned char>((static_cast<unsigned>(pixel[c])+sample[c])/2);
        pixel[3]=255;
    }
}
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        nlohmann::json manifest{{"version",1},{"development",true},{"design_size",{800,600}},{"entry_scene","unused"},{"font",{{"path","unused.ttf"}}},{"images",nlohmann::json::object()}};
        for (const std::string id : {"old","next"}) {
            Pixels pixels(800*600);
            for (int y=0;y<600;++y) for (int x=0;x<800;++x) pixels[static_cast<std::size_t>(y*800+x)]=Pixel{
                static_cast<unsigned char>((x+(id=="old" ? 17 : 99))%256),static_cast<unsigned char>((y+(id=="old" ? 33 : 105))%256),static_cast<unsigned char>((x*7+y*13)%256),255};
            png_image png{};png.version=PNG_IMAGE_VERSION;png.width=800;png.height=600;png.format=PNG_FORMAT_RGBA;
            require(png_image_write_to_file(&png,(temp.path/(id+".png")).c_str(),0,pixels.data(),0,nullptr),"Cannot write gradient");
            manifest["images"][id]={{"design_size",{800,600}},{"variants",{{"1",{{"path",id+".png"},{"pixels",{800,600}}}}}}};
        }
        std::ofstream(temp.path/"manifest.json")<<manifest.dump();Content content(temp.path);
        Window window;Renderer renderer;glDisable(GL_FRAMEBUFFER_SRGB);
        Viewport view{0,0,800,600};
        Layer a;a.kind=LayerKind::image;a.asset="old";a.width=800;a.height=600;Layer b=a;b.asset="next";
        auto draw=[&](int type,int phase,int span,std::uint64_t revision,AssetTier tier=AssetTier::x4) {
            renderer.begin(ww,wh,view,{0,0,0,1});renderer.stage(content,{a},{b},static_cast<float>(phase)/static_cast<float>(span),revision,tier,{},type);
        };
        draw(0,1,1,1);const auto next=read(view);
        renderer.begin(ww,wh,view,{0,0,0,1});renderer.stage(content,{}, {a},1,2,AssetTier::x4);const auto old=read(view);
        for (const int type : {14,16,17}) {
            const int edge=type==16 ? 600 : 800;
            for (const int phase : {0,1,2,17,24,79,299,edge-1,edge}) {
                draw(type,phase,edge+1,static_cast<std::uint64_t>(type));
                require_pixels(read(view),expansion(old,next,type,phase),"Expansion "+std::to_string(type)+"/"+std::to_string(phase));
            }
            draw(type,edge+1,edge+1,static_cast<std::uint64_t>(type));require_pixels(read(view),next,"Full completion");
        }
        Pixels expected=old;int sample=0;
        for (const int phase : {0,10,50,100,190,200,250,390,399}) {
            while (sample<phase/10) { ++sample;zoom_step(expected,sample*10<200 ? old : next,sample*10); }
            draw(2,phase,400,100);require_pixels(read(view),expected,"Zoom history "+std::to_string(phase));
            draw(2,phase,400,100);require_pixels(read(view),expected,"Same sample hold");
            if (phase==190) b.color={0,0,0,1}; // New captured scene must stay frozen.
        }
        b.color={1,1,1,1};renderer.clear_cache();draw(2,390,400,100);require_pixels(read(view),expected,"Cache reset replay");
        view={100,100,800,600};draw(2,390,400,100,AssetTier::x8);require_pixels(read(view),expected,"Moved viewport and tier replay");
        draw(2,400,400,100);require_pixels(read(view),next,"Zoom final copy");
        draw(2,100,400,101);draw(2,0,400,101);require_pixels(read(view),old,"History rewind");
        require(glGetError()==GL_NO_ERROR,"Special transition left GL errors");
        std::cout << "Transition GPU PASS: full gradient canvases, non-divisible nearest scaling, strict edges, zoom feedback/hold/frozen sources/reset/rewind/tier/viewport\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
