#include "text/font.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Window {
    SDL_Window* window{}; SDL_GLContext context{};
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Text effects test",1600,1200,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if (window) context=SDL_GL_CreateContext(window);
        if (!context) { if (window) SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error("No test GL context"); }
    }
    ~Window() { SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); }
};
using Pixels=std::vector<unsigned char>;
Pixels read(int width,int height) {
    Pixels result(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    return result;
}
bool same_rgb(const Pixels& a,const Pixels& b) {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();i+=4) for (std::size_t c=0;c<3;++c) if (a[i+c]!=b[i+c]) return false;
    return true;
}
std::size_t color_count(const Pixels& pixels,int channel) {
    std::size_t count{};
    for (std::size_t i=0; i<pixels.size(); i+=4)
        if (pixels[i+static_cast<std::size_t>(channel)]>180 && pixels[i+(static_cast<std::size_t>(channel)+1)%3]<60 &&
            pixels[i+(static_cast<std::size_t>(channel)+2)%3]<60) ++count;
    return count;
}
int left_edge(const Pixels& pixels,int width) {
    int left=width;
    for (std::size_t i=0; i<pixels.size(); i+=4)
        if (*std::min_element(pixels.begin()+static_cast<std::ptrdiff_t>(i),pixels.begin()+static_cast<std::ptrdiff_t>(i+3))<200)
            left=std::min(left,static_cast<int>(i/4%static_cast<std::size_t>(width)));
    return left;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Expected content and custom Latin font");
        ef::Content content(argv[1]); Window window; ef::Renderer renderer;
        for (bool custom : {false,true}) {
            ef::FontSettings settings;
            if (custom) settings.file=std::filesystem::absolute(argv[2]);
            ef::Font font(content,settings);
            for (int scale : {1,2}) {
                const int width=800*scale,height=600*scale;
                ef::Layer layer; layer.kind=ef::LayerKind::text; layer.text="A中B";
                layer.x=100; layer.y=100; layer.width=500; layer.height=100;
                layer.font_size=40; layer.line_height=52; layer.kinsoku=true; layer.color={1,0,0,1};
                auto render=[&](const ef::Layer& item) {
                    renderer.begin(width,height,ef::fit_viewport(width,height),{1,1,1,1});
                    auto cursor=font.draw(renderer,item,static_cast<float>(scale));
                    require(glGetError()==GL_NO_ERROR,"Text draw left a GL error");
                    return std::pair{read(width,height),cursor};
                };
                auto [base,cursor]=render(layer);
                require(color_count(base,0)>100,"Missing foreground text");
                layer.appearance.outline_width=2; layer.appearance.outline_color={0,0,1,1};
                auto [outlined,outline_cursor]=render(layer);
                require(color_count(outlined,2)>100,"FreeType stroke not visible");
                const int expansion=left_edge(base,width)-left_edge(outlined,width);
                require(expansion>=2*scale-1 && expansion<=2*scale+1,"Outline radius did not scale in physical pixels");
                require(outline_cursor.cursor.x==cursor.cursor.x && outline_cursor.cursor.y==cursor.cursor.y,"Outline changed advance");
                for (std::size_t i=0; i<base.size(); i+=4)
                    if (base[i]==255 && base[i+1]==0 && base[i+2]==0)
                        require(outlined[i]==255 && outlined[i+1]==0 && outlined[i+2]==0,"Outline covered foreground");
                layer.appearance.shadow_x=3; layer.appearance.shadow_y=4; layer.appearance.shadow_color={0,1,0,1};
                auto [shadow,shadow_cursor]=render(layer);
                require(color_count(shadow,1)>100,"Shadow not visible");
                require(shadow_cursor.cursor.x==cursor.cursor.x && shadow_cursor.line_height==cursor.line_height,"Shadow changed layout");
                layer.visible_characters=1;
                const auto partial=render(layer).first;
                auto first=layer; first.text="A"; first.visible_characters=100;
                require(partial==render(first).first,"Unrevealed characters leaked stroke/shadow");
                layer.visible_characters=0;
                const auto hidden=render(layer).first;
                require(color_count(hidden,0)==0 && color_count(hidden,1)==0 && color_count(hidden,2)==0,"Hidden text left effects");
                layer.visible_characters=100; layer.color[3]=0;
                require(render(layer).first==hidden,"Text effects ignored layer opacity");
                layer.color[3]=1; layer.appearance={}; layer.appearance.baseline_offset=2;
                const auto [shifted,shift_cursor]=render(layer);
                require(shift_cursor.cursor.x==cursor.cursor.x && shift_cursor.cursor.y==cursor.cursor.y,"Baseline changed cursor anchor");
                const auto stride=static_cast<std::size_t>(width)*4;
                for (int y=0; y<height-2*scale; ++y)
                    require(std::equal(shifted.begin()+static_cast<std::ptrdiff_t>(y*stride),shifted.begin()+static_cast<std::ptrdiff_t>((y+1)*stride),
                        base.begin()+static_cast<std::ptrdiff_t>((y+2*scale)*stride)),"Baseline shift is not exact physical pixel translation");
                // Vector HUD text must live inside the stage FBO, including the frozen old snapshot.
                auto hud=layer;hud.appearance={};hud.text="856";hud.color={1,0,0,1};
                auto direct=[&](const ef::Layer& item) {
                    renderer.begin(width,height,ef::fit_viewport(width,height),{0,0,0,1});
                    font.draw(renderer,item,static_cast<float>(scale));return read(width,height);
                };
                const auto red_hud=direct(hud);auto next_hud=hud;next_hud.text="857";next_hud.color={0,1,0,1};
                const auto green_hud=direct(next_hud);
                auto stage_text=[&](const ef::Layer& old,const ef::Layer& next,float progress) {
                    renderer.begin(width,height,ef::fit_viewport(width,height),{0,0,0,1});
                    renderer.stage(content,{old},{next},progress,9100+static_cast<unsigned>(scale),ef::AssetTier::x4,{},0,
                        [&](const ef::Layer& item) {font.draw(renderer,item,static_cast<float>(scale));});
                    require(glGetError()==GL_NO_ERROR,"Vector stage composition left a GL error");
                    return read(width,height);
                };
                require(same_rgb(stage_text(hud,next_hud,0),red_hud),"Old vector HUD missing or displaced in transition FBO");
                auto changed_old=hud;changed_old.text="999";
                require(same_rgb(stage_text(changed_old,next_hud,0),red_hud),"Cached old HUD changed during transition");
                const auto mixed=stage_text(hud,next_hud,.5f);
                require(!same_rgb(mixed,red_hud) && !same_rgb(mixed,green_hud),"HUD text did not participate in the fade");
                require(same_rgb(stage_text(hud,next_hud,1),green_hud),"Vector HUD completion differs from direct FreeType rendering");
                renderer.clear_cache();
                // The legacy non-kinsoku path uses the same effect passes and reset cache correctly.
                layer.kinsoku=false; layer.appearance.outline_width=2;
                require(color_count(render(layer).first,2)==0,"Default outline color should be black");
                layer.appearance.outline_color={0,0,1,1};
                require(color_count(render(layer).first,2)>100,"Legacy text omitted outline");
                layer.appearance={}; layer.kinsoku=true;
                require(render(layer).first==base,"Effect reset left stale cached glyphs");
            }
        }
        std::cout << "Text effects PASS: physical stroke width, shadow, baseline, reveal, fallback and reset at 1x/2x\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
