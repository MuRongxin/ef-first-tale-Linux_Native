#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
namespace {
using Pixel=std::array<unsigned char,4>;
constexpr int ww=1200,wh=900;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(Pixel a,Pixel b) {
    for (std::size_t c=0;c<4;++c) if (std::abs(static_cast<int>(a[c])-b[c])>1)
        throw std::runtime_error("Shake pixel mismatch: "+std::to_string(a[c])+" != "+std::to_string(b[c]));
}
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
        Window window; Renderer renderer; glDisable(GL_FRAMEBUFFER_SRGB);
        std::uint64_t serial=0;
        for (const Viewport view : {Viewport{0,0,800,600},Viewport{0,0,1200,900},Viewport{100,100,800,600}}) {
            auto rect=[&](float x,float y,float w,float h,Color color) {
                Layer layer; layer.kind=LayerKind::rectangle; layer.x=x;layer.y=y;layer.width=w;layer.height=h;layer.color=color;renderer.rectangle(layer);
            };
            auto source=[&]() {
                renderer.begin(ww,wh,view,{1,0,0,1});
                rect(400,0,400,600,{0,1,0,1}); rect(300,250,200,100,{1,1,1,1});
                rect(100,100,50,50,{0,0,1,1}); // Stand-in for UI drawn after the stage.
            };
            source(); ShakeFrame shift; shift.x=50; shift.sample=1; renderer.shake(shift,++serial);
            near(read(view,20,200),{0,0,0,255}); near(read(view,175,125),{0,0,255,255});
            renderer.begin(ww,wh,view,{1,1,1,1}); renderer.shake(shift,serial);
            near(read(view,175,125),{0,0,255,255}); near(read(view,200,200),{255,0,0,255});
            shift.x=0;shift.y=-50;shift.sample=2; renderer.shake(shift,serial);
            near(read(view,200,580),{0,0,0,255}); near(read(view,125,75),{0,0,255,255});
            source(); ShakeFrame zoom; zoom.kind=ShakeKind::zoom;zoom.crop=100;zoom.sample=1;renderer.shake(zoom,++serial);
            near(read(view,280,300),{255,255,255,255});
            zoom.crop=0;zoom.sample=2;renderer.shake(zoom,serial);near(read(view,280,300),{255,0,0,255});
            source(); ShakeFrame rotation;rotation.kind=ShakeKind::rotation;rotation.angle=90;rotation.sample=1;renderer.shake(rotation,++serial);
            near(read(view,50,200),{127,0,0,255});
            near(read(view,300,200),{127,127,0,255});
            source();renderer.shake(rotation,serial);near(read(view,50,200),{127,0,0,255});
            rotation.angle=0;rotation.sample=2;renderer.shake(rotation,serial);near(read(view,50,200),{191,0,0,255});
            renderer.clear_cache();source();renderer.shake(rotation,serial);near(read(view,50,200),{255,0,0,255});
            rect(0,0,50,50,{1,1,1,1});near(read(view,20,20),{255,255,255,255});
        }
        require(glGetError()==GL_NO_ERROR,"Shake left GL errors");
        std::cout << "Shake rendering PASS: frozen UI/canvas, shifts/black edges, zoom, cumulative rotation, sample hold, viewport/cache and following UI\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
