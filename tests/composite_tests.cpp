#include "render/renderer.hpp"
#include "scene/cutin.hpp"
#include "scene/walking.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
constexpr int window_width = 1200, window_height = 900;
using Pixel = std::array<unsigned char,4>;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected compositor rejection");
}
void near(Pixel actual, Pixel expected, int tolerance = 2) {
    for (std::size_t n = 0; n < 4; ++n)
        if (std::abs(static_cast<int>(actual[n])-expected[n]) > tolerance)
            throw std::runtime_error("Unexpected composite pixel component " + std::to_string(n) + ": " + std::to_string(actual[n]) + " != " + std::to_string(expected[n]));
}
struct Temporary {
    std::filesystem::path path = std::filesystem::current_path() / ("test-composite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct Window {
    SDL_Window* window{};
    SDL_GLContext context{};
    Window() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("Composite test",window_width,window_height,SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (window) context = SDL_GL_CreateContext(window);
        if (!window || !context) {
            const std::string error = SDL_GetError();
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
            throw std::runtime_error(error);
        }
    }
    ~Window() { SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); }
};
void png(const std::filesystem::path& path, int size, Pixel top, Pixel bottom, bool horizontal=false) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size)*static_cast<std::size_t>(size)*4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const auto& color = (horizontal ? x : y) < size/2 ? top : bottom;
        const auto offset = (static_cast<std::size_t>(y)*static_cast<std::size_t>(size)+static_cast<std::size_t>(x))*4;
        std::copy(color.begin(),color.end(),pixels.begin()+static_cast<std::ptrdiff_t>(offset));
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION; image.width = static_cast<unsigned>(size); image.height = static_cast<unsigned>(size); image.format = PNG_FORMAT_RGBA;
    require(png_image_write_to_file(&image,path.c_str(),0,pixels.data(),0,nullptr),"Could not generate PNG fixture");
}
ef::Layer rectangle(ef::Color color) {
    ef::Layer result; result.kind = ef::LayerKind::rectangle; result.width = 800; result.height = 600; result.color = color;
    return result;
}
ef::Layer image(const std::string& id) {
    ef::Layer result; result.kind = ef::LayerKind::image; result.asset = id; result.width = 400; result.height = 600;
    return result;
}
Pixel read(int x, int top) {
    Pixel result{};
    glReadPixels(x,window_height-top-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    return result;
}
Pixel read(ef::Viewport view, int x, int y) {
    return read(view.x+x*view.width/800,view.y+y*view.height/600);
}
struct Target {
    GLuint framebuffer{}, texture{};
    Target(int width, int height) {
        glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Test framebuffer incomplete");
    }
    ~Target() { glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture); }
};
}
int main() {
    try {
        using namespace ef;
        Temporary temp;
        png(temp.path/"old4.png",8,{255,0,0,128},{0,255,0,128});
        png(temp.path/"old8.png",16,{0,0,255,128},{255,0,0,128});
        png(temp.path/"new4.png",8,{255,255,255,128},{255,0,255,128});
        png(temp.path/"new8.png",16,{0,255,255,128},{255,255,255,128});
        png(temp.path/"mask4.png",8,{0,0,0,255},{255,255,255,255});
        png(temp.path/"mask8.png",16,{0,0,0,255},{255,255,255,255});
        png(temp.path/"walk4.png",8,{255,0,0,255},{0,255,0,255},true);
        png(temp.path/"walk8.png",16,{255,0,0,255},{0,255,0,255},true);
        nlohmann::json manifest{{"version",1},{"design_size",{800,600}},{"entry_scene","unused.scene.json"},
            {"font",{{"path","unused.ttf"}}},{"images",nlohmann::json::object()}};
        for (const std::string id : {"old","new","mask","walk"}) manifest["images"][id] = {{"design_size",{2,2}},{"variants",{
            {"4",{{"path",id+"4.png"},{"pixels",{8,8}}}}, {"8",{{"path",id+"8.png"},{"pixels",{16,16}}}}}}};
        std::ofstream(temp.path/"manifest.json") << manifest.dump();
        Content content(temp.path);
        Window window;
        Renderer renderer;
        glDisable(GL_FRAMEBUFFER_SRGB);
        std::vector<Layer> previous{rectangle({0,0,1,1}),image("old")};
        std::vector<Layer> current{rectangle({0,1,0,1}),image("new")};
        Viewport view{200,150,800,600};
        const auto render = [&](float progress, std::uint64_t revision = 1, AssetTier tier = AssetTier::x4) {
            renderer.begin(window_width,window_height,view,{0.2f,0.3f,0.4f,1});
            renderer.stage(content,previous,current,progress,revision,tier);
            require(glGetError()==GL_NO_ERROR,"Compositor left a GL error");
        };
        render(0);
        const std::array<Pixel,3> old{read(view,100,150),read(view,100,450),read(view,600,150)};
        near(old[0],{128,0,127,255}); near(old[1],{0,128,127,255}); near(old[2],{0,0,255,255});
        render(1);
        const std::array<Pixel,3> next{read(view,100,150),read(view,100,450),read(view,600,150)};
        near(next[0],{128,255,128,255}); near(next[1],{128,127,128,255});
        render(0.5f);
        const std::array<Pixel,3> middle{read(view,100,150),read(view,100,450),read(view,600,150)};
        for (std::size_t n = 0; n < middle.size(); ++n) {
            Pixel expected{};
            for (std::size_t c = 0; c < 4; ++c) expected[c] = static_cast<unsigned char>((static_cast<unsigned>(old[n][c])+next[n][c]+1)/2);
            near(middle[n],expected);
        }
        near(read(10,10),{0,0,0,255},0);
        near(read(1190,890),{0,0,0,255},0);
        previous[0].color = {1,1,0,1};
        render(0);
        near(read(view,600,150),old[2]); // Old snapshot stays frozen at this revision.
        render(0,2);
        near(read(view,600,150),{255,255,0,255});
        previous[0].color = {0,0,1,1};
        render(0,3);
        previous[0].color = {1,1,0,1};
        render(0,3,AssetTier::x8);
        near(read(view,100,150),{127,127,128,255}); // Tier change rebuilt the snapshot.
        previous[0].color = {1,0,0,1};
        view = {400,300,400,300};
        render(0,3,AssetTier::x8);
        near(read(view,600,150),{255,0,0,255}); // FBO dimensions changed.
        previous[0].color = {0,0,1,1};
        renderer.clear_cache();
        render(0,3,AssetTier::x8);
        near(read(view,600,150),{0,0,255,255});
        previous[0].color = {0,1,0,1};
        view.x += 50;
        render(0,3,AssetTier::x8);
        near(read(view,600,150),{0,255,0,255}); // Position-only viewport change also invalidates.
        current[0].color = {1,0,0,1};
        render(0.5f,3,AssetTier::x8);
        near(read(view,600,150),{128,128,0,255}); // Current scene is rendered every frame.

        Target draw_target(window_width,window_height), read_target(32,32);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_target.framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read_target.framebuffer);
        renderer.begin(window_width,window_height,view,{0,0,0,1});
        glDisable(GL_SCISSOR_TEST); glScissor(11,22,33,44);
        glDisable(GL_BLEND); glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_SUBTRACT);
        glClearColor(0.1f,0.2f,0.3f,0.4f);
        std::array<GLint,4> saved_view{}; glGetIntegerv(GL_VIEWPORT,saved_view.data());
        const auto check_state = [&] {
            GLint draw{},read{},src{},equation{};
            std::array<GLint,4> vp{},scissor{};
            std::array<GLfloat,4> clear{};
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
            glGetIntegerv(GL_VIEWPORT,vp.data()); glGetIntegerv(GL_SCISSOR_BOX,scissor.data()); glGetFloatv(GL_COLOR_CLEAR_VALUE,clear.data());
            glGetIntegerv(GL_BLEND_SRC_RGB,&src); glGetIntegerv(GL_BLEND_EQUATION_RGB,&equation);
            require(static_cast<GLuint>(draw)==draw_target.framebuffer && static_cast<GLuint>(read)==read_target.framebuffer,"Framebuffer binding not restored");
            require(vp==saved_view && scissor==std::array<GLint,4>{11,22,33,44} && !glIsEnabled(GL_SCISSOR_TEST),"Viewport/scissor not restored");
            require(!glIsEnabled(GL_BLEND) && src==GL_ONE && equation==GL_FUNC_REVERSE_SUBTRACT,"Blend state not restored");
            require(std::abs(clear[0]-0.1f)<1e-6f && std::abs(clear[3]-0.4f)<1e-6f,"Clear color not restored");
        };
        renderer.stage(content,previous,current,0.5f,4,AssetTier::x4);
        check_state();
        auto invalid = previous; invalid[0].kind = static_cast<LayerKind>(99);
        rejects([&] { renderer.stage(content,invalid,current,0.5f,5,AssetTier::x4); });
        check_state();
        rejects([&] { renderer.stage(content,previous,current,std::numeric_limits<float>::quiet_NaN(),5,AssetTier::x4); });
        check_state();
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        GLint maximum[2]{}; glGetIntegerv(GL_MAX_VIEWPORT_DIMS,maximum);
        const int big_width = std::min(maximum[0],10000), big_height = std::min(maximum[1],10000);
        if (static_cast<std::uint64_t>(big_width)*static_cast<std::uint64_t>(big_height)*8 > 256ULL*1024*1024) {
            glViewport(0,0,big_width,big_height);
            rejects([&] { renderer.stage(content,previous,current,0.5f,6,AssetTier::x4); });
        }
        auto cropped=image("old"); cropped.source=std::array<float,4>{0,1,2,1};
        renderer.begin(window_width,window_height,view,{0,0,0,1});
        renderer.stage(content,{}, {rectangle({0,0,0,1}),cropped},1,98,AssetTier::x4);
        near(read(view,100,150),{0,128,0,255}); near(read(view,100,450),{0,128,0,255});
        cropped.source=std::array<float,4>{0,2,2,1};
        rejects([&] { renderer.stage(content,{}, {cropped},1,99,AssetTier::x4); });
        renderer.begin(window_width,window_height,view,{1,1,1,1});
        renderer.stage(content,previous,{},1,6,AssetTier::x4);
        near(read(view,600,150),{0,0,0,255},0);
        const Pixel old_pixel{240,100,10,255},new_pixel{20,200,250,255};
        previous={rectangle({240/255.0f,100/255.0f,10/255.0f,1})};
        current={rectangle({20/255.0f,200/255.0f,250/255.0f,1})};
        for (const auto tier : {AssetTier::x4,AssetTier::x8}) for (const Viewport area : {Viewport{0,0,800,600},Viewport{0,0,1200,900},Viewport{100,100,800,600}}) {
            for (const int phase : {0,128,256,384,511,512}) {
                renderer.begin(window_width,window_height,area,{0,0,0,1});
                renderer.stage(content,previous,current,static_cast<float>(phase)/512,123,tier,"mask");
                for (const int y : {100,500}) {
                    Pixel expected=old_pixel;
                    const int mask=y<300 ? 255 : 0; // Native loader inverts the source mask.
                    const int amount=std::clamp(mask+phase-256,0,255);
                    for (std::size_t c=0;c<3;++c) expected[c]=phase==512 ? new_pixel[c] : static_cast<unsigned char>(static_cast<int>(old_pixel[c])+static_cast<int>(std::floor((static_cast<int>(new_pixel[c])-old_pixel[c])*amount/256.0)));
                    near(read(area,100,y),expected,0);
                }
            }
            renderer.clear_cache();
        }
        renderer.begin(window_width,window_height,view,{0,0,0,1});
        renderer.stage(content,previous,current,.5f,124,AssetTier::x4,"mask");
        renderer.rectangle(rectangle({1,1,1,1}));near(read(view,100,100),{255,255,255,255},0);
        Layer panorama=image("walk");panorama.width=3200;panorama.height=600;panorama.part=StagePart::background;
        Walking walking(60,-2);walking.update(.3);
        const auto wrapped=walking.layers({panorama});
        Layer cut_image=image("walk");cut_image.width=800;cut_image.height=600;
        Cutin cut(cut_image,-1);cut.update(.01);
        for (const auto tier : {AssetTier::x4,AssetTier::x8}) {
            view={0,0,1200,900};renderer.begin(window_width,window_height,view,{0,0,0,1});
            renderer.stage(content,{},wrapped,1,130,tier);
            near(read(view,100,300),{255,0,0,255},0);near(read(view,700,300),{255,0,0,255},0);
            renderer.image(content,cut.layer(),tier);
            near(read(view,790,300),{255,0,0,255},0); // Entering image is still outside this point.
            Cutin held(cut_image,-1);held.update(2);
            renderer.image(content,held.layer(),tier);near(read(view,700,300),{0,255,0,255},0);
            auto frame=rectangle({0,0,1,1});frame.part=StagePart::frame;frame.x=400;frame.width=400;
            renderer.begin(window_width,window_height,view,{0,0,0,1});
            renderer.stage(content,{}, {held.layer(),frame},1,131,tier);
            near(read(view,100,300),{255,0,0,255},0);near(read(view,700,300),{0,0,255,255},0);
            frame.color[3]=.5f;
            renderer.begin(window_width,window_height,view,{0,0,0,1});
            renderer.stage(content,{}, {held.layer(),frame},1,132,tier);
            near(read(view,700,300),{0,127,128,255});
            held.fade_out();held.update(.01);
            renderer.begin(window_width,window_height,view,{0,0,1,1});
            renderer.image(content,held.layer(),tier);
            near(read(view,799,300),{0,0,255,255},0); // Exit uncovers the unchanged stage.
        }
        require(glGetError()==GL_NO_ERROR,"Compositor cleanup left a GL error");
        std::cout << "Composite tests PASS: whole-scene alpha lerp, opaque output, image Y orientation, bars, snapshot keys, resize, live current scene, cache invalidation, GL state/exception restoration and budget limits\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
