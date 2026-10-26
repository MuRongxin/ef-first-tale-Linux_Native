#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <bit>
namespace {
using Pixel=std::array<unsigned char,4>;
constexpr int ww=1200,wh=900;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(Pixel actual,Pixel expected) {
    for (std::size_t n=0; n<4; ++n) if (std::abs(static_cast<int>(actual[n])-expected[n])>2)
        throw std::runtime_error("Screen pixel mismatch: "+std::to_string(actual[n])+" != "+std::to_string(expected[n]));
}
unsigned char screen(unsigned a,unsigned b) { return static_cast<unsigned char>(254-((255-a)*(255-b)/256)); }
unsigned char overlay(unsigned a,unsigned b) {
    return static_cast<unsigned char>(a<128 ? std::min(255U,2*((a*b+255)/256)) : 255-std::min(255U,2*(((255-a)*(255-b)+255)/256)));
}
Pixel overlaid(Pixel a,Pixel b) { for (int c=0;c<3;++c) a[static_cast<std::size_t>(c)]=overlay(a[static_cast<std::size_t>(c)],b[static_cast<std::size_t>(c)]); return a; }
Pixel hsv(Pixel pixel,std::array<int,3> values) {
    const int s=(std::clamp(values[1],-100,100)+100)*65536/100;
    const int inv=65536-s, v=std::clamp(values[2],-100,100)*256;
    const auto weight=[&](int n) { return (std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(inv)*n))>>16)+v; };
    // Independent six lookup tables follow the shipped per-channel accumulation routine.
    static std::array<std::array<int,256>,6> table{};
    static std::array<int,3> previous{999,999,999};
    if (previous!=values) {
    previous=values;
    for (int n=0;n<256;++n) {
        table[0][static_cast<std::size_t>(n)]=n*(weight(19595)+s);
        table[1][static_cast<std::size_t>(n)]=n*weight(19595);
        table[2][static_cast<std::size_t>(n)]=n*(weight(38470)+s);
        table[3][static_cast<std::size_t>(n)]=n*weight(38470);
        table[4][static_cast<std::size_t>(n)]=n*(weight(7471)+s);
        table[5][static_cast<std::size_t>(n)]=n*weight(7471);
    }
    }
    if (pixel[3]==0) return pixel;
    const auto r=pixel[0],g=pixel[1],b=pixel[2];
    pixel[0]=static_cast<unsigned char>(std::clamp((table[0][r]+table[3][g]+table[5][b])>>16,0,255));
    pixel[1]=static_cast<unsigned char>(std::clamp((table[1][r]+table[2][g]+table[5][b])>>16,0,255));
    pixel[2]=static_cast<unsigned char>(std::clamp((table[1][r]+table[3][g]+table[4][b])>>16,0,255));
    return pixel;
}
Pixel filtered(Pixel a,Pixel b) { for (int c=0; c<3; ++c) a[static_cast<std::size_t>(c)]=screen(a[static_cast<std::size_t>(c)],b[static_cast<std::size_t>(c)]); return a; }
struct Temporary {
    std::filesystem::path path=std::filesystem::current_path()/("test-screen-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path,ignored); }
};
struct Window {
    SDL_Window* window{}; SDL_GLContext context{};
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Screen filter test",ww,wh,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if (window) context=SDL_GL_CreateContext(window);
        if (!context) { if (window) SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error("No GL context"); }
    }
    ~Window() { SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); }
};
Pixel read(ef::Viewport view,int x,int y) {
    Pixel pixel{};
    glReadPixels(view.x+x*view.width/800,wh-view.y-y*view.height/600-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    return pixel;
}
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        nlohmann::json manifest{{"version",1},{"development",true},{"design_size",{800,600}},
            {"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"images",nlohmann::json::object()}};
        const auto add=[&](const std::string& id,int width,int height,auto color) {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4);
            for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
                const auto pixel=color(x,y); const auto offset=(static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x))*4;
                std::copy(pixel.begin(),pixel.end(),pixels.begin()+static_cast<std::ptrdiff_t>(offset));
            }
            png_image png{}; png.version=PNG_IMAGE_VERSION; png.width=static_cast<unsigned>(width); png.height=static_cast<unsigned>(height); png.format=PNG_FORMAT_RGBA;
            require(png_image_write_to_file(&png,(temp.path/(id+".png")).c_str(),0,pixels.data(),0,nullptr),"Cannot write synthetic screen fixture");
            manifest["images"][id]={{"design_size",{width,height}},{"variants",{{"1",{{"path",id+".png"},{"pixels",{width,height}}}}}}};
        };
        const Pixel source{80,120,160,255},top{10,20,30,255},middle{40,80,120,255},bottom{200,100,20,255};
        add("source",2,600,[&](int,int) { return source; });
        add("tall",2,1200,[&](int,int) { return source; });
        add("alpha",2,200,[&](int,int) { return Pixel{80,120,160,128}; });
        add("clear",2,600,[](int,int) { return Pixel{255,255,255,0}; });
        add("band",1,601,[&](int,int y) { return y==0 ? top : y<301 ? middle : bottom; });
        for (int height : {611,621}) add("band"+std::to_string(height),1,height,[&](int,int y) { return y<height-600 ? Pixel{255,0,0,255} : middle; });
        add("large1",900,601,[&](int,int) { return middle; });
        add("large2",900,601,[&](int,int) { return bottom; });
        add("colors",256,256,[](int x,int y) { return Pixel{static_cast<unsigned char>(x),static_cast<unsigned char>(y),static_cast<unsigned char>((x*7+y*13)&255),255}; });
        add("matrix",256,256,[](int x,int) { const auto n=static_cast<unsigned char>(x); return Pixel{n,n,n,255}; });
        add("ramp",1,256,[](int,int y) { const auto n=static_cast<unsigned char>(y); return Pixel{n,n,n,255}; });
        std::ofstream(temp.path/"manifest.json")<<manifest.dump(); Content content(temp.path);
        Window window; Renderer renderer; glDisable(GL_FRAMEBUFFER_SRGB);
        Viewport view{0,0,800,600};
        Layer layer; layer.kind=LayerKind::image; layer.asset="matrix"; layer.screen="ramp"; layer.width=layer.height=256;
        renderer.begin(ww,wh,view,{0,0,0,1}); renderer.image(content,layer,AssetTier::x4);
        std::vector<unsigned char> matrix(256*256*4); glReadPixels(0,wh-256,256,256,GL_RGBA,GL_UNSIGNED_BYTE,matrix.data());
        for (unsigned y=0; y<256; ++y) for (unsigned x=0; x<256; ++x) {
            const auto offset=((255-y)*256+x)*4;
            require(matrix[offset]==screen(x,y) && matrix[offset+1]==screen(x,y) && matrix[offset+2]==screen(x,y) && matrix[offset+3]==255,
                "GPU screen formula differs from native byte formula");
        }
        layer.screen.clear(); layer.overlay="ramp";
        renderer.begin(ww,wh,view,{0,0,0,1}); renderer.image(content,layer,AssetTier::x4);
        glReadPixels(0,wh-256,256,256,GL_RGBA,GL_UNSIGNED_BYTE,matrix.data());
        for (unsigned y=0;y<256;++y) for (unsigned x=0;x<256;++x) {
            const auto offset=((255-y)*256+x)*4;
            require(matrix[offset]==overlay(x,y) && matrix[offset+1]==overlay(x,y) && matrix[offset+2]==overlay(x,y) && matrix[offset+3]==255,
                "GPU overlay formula differs from native byte formula");
        }
        layer.overlay.clear(); layer.asset="colors";
        for (const auto values : {std::array<int,3>{0,-25,-8},{0,5,-5},{5,-5,0},{42,0,0},{0,-100,100},{0,100,-100}}) {
            layer.hsv=values;
            renderer.begin(ww,wh,view,{0,0,0,1});renderer.image(content,layer,AssetTier::x4);
            glReadPixels(0,wh-256,256,256,GL_RGBA,GL_UNSIGNED_BYTE,matrix.data());
            for (unsigned y=0;y<256;++y) for (unsigned x=0;x<256;++x) {
                const auto expected=hsv({static_cast<unsigned char>(x),static_cast<unsigned char>(y),static_cast<unsigned char>((x*7+y*13)&255),255},values);
                const auto offset=((255-y)*256+x)*4;
                for (std::size_t c=0;c<4;++c) require(matrix[offset+c]==expected[c],"GPU HSV differs from native fixed-point lookup tables");
            }
        }
        layer.hsv={};
        for (auto tier : {AssetTier::x4,AssetTier::x8}) for (bool enlarged : {false,true}) {
            view=enlarged ? Viewport{0,0,1200,900} : Viewport{0,0,800,600};
            layer.asset="source"; layer.screen="band"; layer.x=layer.y=0; layer.width=800; layer.height=600;
            auto draw=[&]() { renderer.begin(ww,wh,view,{0,0,0,1}); renderer.stage(content,{}, {layer},1,1,tier); };
            draw(); near(read(view,100,100),filtered(source,middle)); near(read(view,100,500),filtered(source,bottom));
            for (int height : {611,621}) {
                layer.screen="band"+std::to_string(height); draw(); near(read(view,100,2),filtered(source,middle));
            }
            layer.asset="tall"; layer.screen="band"; layer.height=1200; layer.y=0; draw(); near(read(view,100,100),filtered(source,top));
            layer.y=-600; draw(); near(read(view,100,100),filtered(source,middle)); near(read(view,100,500),filtered(source,bottom));
            layer.asset="alpha"; layer.height=200; layer.y=0; draw();
            auto expected=filtered(source,bottom); for (int c=0; c<3; ++c) expected[static_cast<std::size_t>(c)]=static_cast<unsigned char>(expected[static_cast<std::size_t>(c)]*128/255);
            near(read(view,100,100),expected);
            layer.asset="clear"; layer.height=600; draw(); near(read(view,100,100),{0,0,0,255});
            layer.asset="source"; layer.height=600; layer.screen="band"; layer.overlay="band611"; draw();
            near(read(view,100,100),filtered(overlaid(source,middle),middle));
            layer.hsv={0,-25,-8}; draw();
            near(read(view,100,100),filtered(overlaid(hsv(source,layer.hsv),middle),middle));
            layer.asset="alpha"; layer.height=200; draw();
            expected=filtered(overlaid(hsv(source,layer.hsv),middle),bottom);
            for (int c=0;c<3;++c) expected[static_cast<std::size_t>(c)]=static_cast<unsigned char>(expected[static_cast<std::size_t>(c)]*128/255);
            near(read(view,100,100),expected);
            layer.asset="clear";layer.height=600;draw();near(read(view,100,100),{0,0,0,255});
            // A following UI rectangle must not inherit the strip sampler or filter uniform.
            Layer ui; ui.kind=LayerKind::rectangle; ui.width=ui.height=200; ui.color={1,1,1,1}; renderer.rectangle(ui);
            near(read(view,100,100),{255,255,255,255});
            layer.overlay.clear(); layer.hsv={};
        }
        layer.asset="source"; layer.screen="band"; layer.height=600; layer.y=0;
        const auto old=layer; layer.screen.clear();
        view={0,0,800,600};
        renderer.begin(ww,wh,view,{0,0,0,1}); renderer.stage(content,{old},{layer},0,90,AssetTier::x4); near(read(view,100,100),filtered(source,middle));
        renderer.clear_cache();
        renderer.begin(ww,wh,view,{0,0,0,1}); renderer.stage(content,{old},{layer},0,90,AssetTier::x8); near(read(view,100,100),filtered(source,middle));
        renderer.begin(ww,wh,view,{0,0,0,1}); renderer.stage(content,{old},{layer},1,90,AssetTier::x8); near(read(view,100,100),source);
        layer.screen="large1"; layer.overlay="large2";
        bool rejected_pair=false;
        try { renderer.image(content,layer,AssetTier::x4); } catch (const std::exception&) { rejected_pair=true; }
        require(rejected_pair,"Filter pair exceeding cache capacity should fail without evicting the pinned sampler");
        layer.screen.clear();
        renderer.begin(ww,wh,view,{0,0,0,1});renderer.image(content,layer,AssetTier::x4);
        near(read(view,100,100),overlaid(source,bottom));
        require(glGetError()==GL_NO_ERROR,"Screen rendering left GL errors");
        std::cout << "Screen/overlay rendering PASS: all 65,536 byte pairs per filter, composition order, alpha, 601/611/621 strips, padded/cropped 1200px background, viewport/tier/cache changes and UI isolation\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
