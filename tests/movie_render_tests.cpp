#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Pixel = std::array<unsigned char,4>;
using Pixels = std::vector<Pixel>;
constexpr int width = 800, height = 600;
struct Window {
    SDL_Window* window{};
    SDL_GLContext context{};
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Movie render test",width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if (window) context=SDL_GL_CreateContext(window);
        if (!context) { if (window) SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error("No GL context"); }
    }
    ~Window() { SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); }
};
Pixels read() {
    Pixels raw(static_cast<std::size_t>(width)*height),result(raw.size());
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
    for (int y=0;y<height;++y)
        std::copy_n(raw.begin()+static_cast<std::ptrdiff_t>((height-1-y)*width),width,
            result.begin()+static_cast<std::ptrdiff_t>(y*width));
    return result;
}
Pixel at(const Pixels& pixels,int x,int y) { return pixels[static_cast<std::size_t>(y*width+x)]; }
void require_color(const Pixels& pixels,int x,int y,Pixel expected,const char* where) {
    const auto actual=at(pixels,x,y);
    if (actual!=expected) throw std::runtime_error(std::string(where)+" at "+std::to_string(x)+","+std::to_string(y)
        +": got "+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2])
        +" expected "+std::to_string(expected[0])+","+std::to_string(expected[1])+","+std::to_string(expected[2]));
}
}
int main() {
    try {
        using namespace ef;
        Window window;
        Renderer renderer;
        const Viewport view{0,0,width,height};
        std::vector<std::uint8_t> frame(8*8*4);
        const auto fill=[&](std::uint8_t r,std::uint8_t g,std::uint8_t b) {
            for (std::size_t i=0;i<frame.size();i+=4) { frame[i]=r;frame[i+1]=g;frame[i+2]=b;frame[i+3]=255; }
        };
        fill(11,22,33);
        renderer.begin(width,height,view,{0,0,0,1});
        renderer.movie(frame.data(),8,8,100,50,200,100);
        auto pixels=read();
        require_color(pixels,0,0,{0,0,0,255},"Movie must clear outside the destination rectangle");
        require_color(pixels,99,50,{0,0,0,255},"Movie left edge");
        require_color(pixels,100,50,{11,22,33,255},"Movie top-left");
        require_color(pixels,299,149,{11,22,33,255},"Movie bottom-right");
        require_color(pixels,300,150,{0,0,0,255},"Movie right/bottom edge");
        // The next frame reuses the allocated texture through glTexSubImage2D.
        fill(201,120,7);
        renderer.begin(width,height,view,{0,0,0,1});
        renderer.movie(frame.data(),8,8,0,0,800,600);
        pixels=read();
        require_color(pixels,0,0,{201,120,7,255},"Second movie frame upload");
        require_color(pixels,799,599,{201,120,7,255},"Full-canvas movie frame");
        std::cout << "Movie render tests PASS: opaque frame rectangle, black surround and texture reuse\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
