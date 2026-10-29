#include "render/renderer.hpp"
#include <SDL3/SDL.h>
#include <png.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
void require(bool v,const char* s) {if (!v) throw std::runtime_error(s);}
std::array<unsigned char,4> pixel(int x,int y) {
    return {static_cast<unsigned char>((x*17+y*13)%256),static_cast<unsigned char>((x/3+y*5)%256),static_cast<unsigned char>((x*3+y/7)%256),173};
}
void png(const std::filesystem::path& path,int w,int h) {
    FILE* file=std::fopen(path.c_str(),"wb");require(file,"png fixture open");
    auto* writer=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);auto* info=png_create_info_struct(writer);
    std::vector<unsigned char> row(static_cast<std::size_t>(w)*4);
    if (setjmp(png_jmpbuf(writer))) throw std::runtime_error("fixture PNG write");
    png_init_io(writer,file);png_set_IHDR(writer,info,static_cast<unsigned>(w),static_cast<unsigned>(h),8,PNG_COLOR_TYPE_RGBA,0,0,0);png_write_info(writer,info);
    for (int y=0;y<h;++y) {for (int x=0;x<w;++x) {const auto p=pixel(x,y);std::copy(p.begin(),p.end(),row.begin()+x*4);}png_write_row(writer,row.data());}
    png_write_end(writer,info);png_destroy_write_struct(&writer,&info);std::fclose(file);
}
struct Window {
    SDL_Window* window{};SDL_GLContext context{};
    Window() {
        require(SDL_Init(SDL_INIT_VIDEO),"SDL init");SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("Tile renderer",800,600,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);require(window,"SDL window");context=SDL_GL_CreateContext(window);require(context,"SDL GL");
    }
    ~Window() {SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();}
};
std::vector<unsigned char> capture() {std::vector<unsigned char> p(800*600*4);glReadPixels(0,0,800,600,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
}
int main(int argc,char** argv) {
    const auto root=std::filesystem::current_path()/("test-tile-render-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);setenv("XDG_CACHE_HOME",root.c_str(),1);
        png(root/"wide4.png",9000,1200);png(root/"wide8.png",18000,2400);
        png(root/"reference4.png",3200,1200);png(root/"reference8.png",6400,2400);
        png(root/"strip4.png",4,2404);png(root/"strip8.png",8,4808);
        nlohmann::json manifest{{"version",1},{"design_size",{800,600}},{"entry_scene","unused.json"},{"font",{{"path","unused.ttf"}}},{"images",nlohmann::json::object()}};
        manifest["images"]["wide"]={{"design_size",{2250,300}},{"variants",{{"4",{{"path","wide4.png"},{"pixels",{9000,1200}}}},{"8",{{"path","wide8.png"},{"pixels",{18000,2400}}}}}}};
        manifest["images"]["ref"]={{"design_size",{800,300}},{"variants",{{"4",{{"path","reference4.png"},{"pixels",{3200,1200}}}},{"8",{{"path","reference8.png"},{"pixels",{6400,2400}}}}}}};
        manifest["images"]["strip"]={{"design_size",{1,601}},{"variants",{{"4",{{"path","strip4.png"},{"pixels",{4,2404}}}},{"8",{{"path","strip8.png"},{"pixels",{8,4808}}}}}}};
        std::ofstream(root/"manifest.json")<<manifest.dump();ef::Content content(root);Window window;ef::Renderer renderer;
        auto draw=[&](const ef::Content& c,const ef::Layer& layer,ef::AssetTier tier) {
            renderer.begin(800,600,{0,0,800,600},{.2f,.3f,.4f,1});renderer.image(c,layer,tier);
            require(glGetError()==GL_NO_ERROR,"tile GL error");
        };
        auto settle=[&](const ef::Content& c,const ef::Layer& layer,ef::AssetTier tier,std::size_t count) {
            (void)count;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            for (;;) {
                draw(c,layer,tier);
                const auto stats=renderer.tile_stats();require(stats.gpu_bytes<=192ULL*1024*1024 && stats.worker.ready_bytes<=64ULL*1024*1024,"tile budgets");
                if (stats.resident>0 && stats.visible_missing==0) break;
                require(std::chrono::steady_clock::now()<deadline,"renderer tile timeout");std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        ef::Layer wide;wide.kind=ef::LayerKind::image;wide.asset="wide";wide.width=2250;wide.height=300;
        ef::Layer reference=wide;reference.asset="ref";reference.width=800;
        for (const auto tier:{ef::AssetTier::x4,ef::AssetTier::x8}) {
            renderer.clear_cache();settle(content,wide,tier,tier==ef::AssetTier::x4 ? 8 : 21);
            const auto actual=capture();draw(content,reference,tier);const auto expected=capture();
            for (std::size_t i=0;i<actual.size();++i) require(std::abs(static_cast<int>(actual[i])-expected[i])<=2,"tiled pixels/seams/alpha differ from monolithic reference");
            // Atlas source that straddles both tile axes, including fractional crop.
            wide.source=std::array<float,4>{253.25f,126.5f,400,150};wide.width=800;wide.height=300;
            reference.source=wide.source;
            settle(content,wide,tier,tier==ef::AssetTier::x4 ? 8 : 21);const auto cropped=capture();draw(content,reference,tier);const auto crop_reference=capture();
            for (std::size_t i=0;i<cropped.size();++i) require(std::abs(static_cast<int>(cropped[i])-crop_reference[i])<=3,"atlas tile pixels differ");
            wide.source.reset();reference.source.reset();wide.width=2250;
            wide.screen=reference.screen="strip";wide.overlay=reference.overlay="strip";wide.hsv=reference.hsv={0,12,-7};
            wide.trim_bottom=reference.trim_bottom=17;wide.height=reference.height=283;
            settle(content,wide,tier,tier==ef::AssetTier::x4 ? 8 : 21);const auto filtered=capture();draw(content,reference,tier);const auto filter_reference=capture();
            for (std::size_t i=0;i<filtered.size();++i) require(std::abs(static_cast<int>(filtered[i])-filter_reference[i])<=3,"tile filter mapping or bottom trim differs");
            wide.screen.clear();reference.screen.clear();wide.overlay.clear();reference.overlay.clear();wide.hsv=reference.hsv={};wide.trim_bottom=reference.trim_bottom=0;wide.height=reference.height=300;
        }
        // Move far away: old areas are evicted; nearby CPU prefetch is reused.
        wide.x=-1400;settle(content,wide,ef::AssetTier::x8,21);require(renderer.tile_stats().worker.builds==2,"pan caused full source rebuild");
        renderer.clear_cache();wide.x=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        do {
            renderer.begin(800,600,{0,0,800,600},{0,0,0,1});renderer.stage(content,{wide},{},0,777,ef::AssetTier::x8);
            require(std::chrono::steady_clock::now()<deadline,"snapshot detail timeout");
            if (!renderer.tile_stats().visible_missing) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (true);
        const auto snapshot=capture();
        renderer.begin(800,600,{0,0,800,600},{0,0,0,1});renderer.stage(content,{}, {reference},1,778,ef::AssetTier::x8);
        const auto snapshot_reference=capture();
        for (std::size_t i=0;i<snapshot.size();++i) require(std::abs(static_cast<int>(snapshot[i])-snapshot_reference[i])<=3,"old FBO did not acquire streamed detail");
        // Real local package can be verified without committing any game material.
        if (argc>1) {
            renderer.clear_cache();ef::Content real(argv[1]);ef::Layer layer;layer.kind=ef::LayerKind::image;layer.asset="bg/bg_scl008a01_03.png";layer.width=5054;layer.height=600;
            const auto started=std::chrono::steady_clock::now();settle(real,layer,ef::AssetTier::x8,35);
            auto stats=renderer.tile_stats();std::cout<<"real 40432x4800 resident="<<stats.resident<<" bytes="<<stats.gpu_bytes<<" builds="<<stats.worker.builds<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<'\n';
            if (argc>2) renderer.capture(std::string(argv[2])+"-left.ppm",800,600);
            const auto scroll_start=std::chrono::steady_clock::now();layer.x=-3900;settle(real,layer,ef::AssetTier::x8,35);
            stats=renderer.tile_stats();std::cout<<"real far-scroll resident="<<stats.resident<<" bytes="<<stats.gpu_bytes<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-scroll_start).count()<<'\n';
            if (argc>2) renderer.capture(std::string(argv[2])+"-right.ppm",800,600);
        }
        renderer.clear_cache();require(renderer.tile_stats().gpu_bytes==0 && renderer.tile_stats().worker.ready_bytes==0,"cache clear");
        std::filesystem::remove_all(root);std::cout<<"tiled render checks passed\n";return 0;
    } catch (const std::exception& e) {std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
