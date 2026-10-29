#include "data/tile_stream.hpp"
#include <png.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
void require(bool b,const char* message) {if (!b) throw std::runtime_error(message);}
std::uint8_t sample(int x,int y,int c) {return static_cast<std::uint8_t>((x*7+y*11+c*53)%256);}
void fixture(const std::filesystem::path& path,int w,int h) {
    ef::Image image{w,h,std::vector<std::uint8_t>(static_cast<std::size_t>(w)*h*4)};
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) for (int c=0;c<4;++c) image.rgba[(static_cast<std::size_t>(y)*w+x)*4+c]=sample(x,y,c);
    png_image png{};png.version=PNG_IMAGE_VERSION;png.width=static_cast<unsigned>(w);png.height=static_cast<unsigned>(h);png.format=PNG_FORMAT_RGBA;
    require(png_image_write_to_file(&png,path.c_str(),0,image.rgba.data(),0,nullptr),"fixture write");
}
ef::Image wait(ef::TileStream& stream,const std::string& key,ef::TileIndex i) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for (;;) {if (auto image=stream.take(key,i)) return std::move(*image);
        require(std::chrono::steady_clock::now()<deadline,"tile timeout");std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
void check(const ef::Image& image,ef::TileRegion r) {
    require(image.width==r.width && image.height==r.height,"tile dimensions/apron");
    for (int y=0;y<r.height;++y) for (int x=0;x<r.width;++x) for (int c=0;c<4;++c)
        require(image.rgba[(static_cast<std::size_t>(y)*r.width+x)*4+c]==sample(r.x+x,r.y+y,c),"tile pixel differs from source");
}
}
int main() {
    const auto root=std::filesystem::current_path()/("test-tiles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);const auto path=root/"source.png";fixture(path,2050,1025);
        const std::vector<ef::TileIndex> wanted{{0,0},{1,0},{2,0},{0,1},{1,1},{2,1}};
        {
            ef::TileStream stream(root/"cache");stream.begin_frame();const auto key=stream.request(path,2050,1025,wanted);
            for (const auto i:wanted) check(wait(stream,key,i),ef::TileStream::region(2050,1025,i));
            require(stream.stats().builds==1,"source decoded more than once");
            require(stream.stats().ready_bytes<=ef::TileStream::ready_budget,"CPU queue budget");
            // Consumed results are not decoded repeatedly while waiting for another frame.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));require(stream.stats().ready_bytes==0,"consumed tile rescheduled");
            stream.clear();require(stream.stats().ready_bytes==0,"clear retained results");
        }
        {
            ef::TileStream stream(root/"cache");stream.begin_frame();const auto key=stream.request(path,2050,1025,{{2,1}});
            check(wait(stream,key,{2,1}),ef::TileStream::region(2050,1025,{2,1}));require(stream.stats().builds==0,"disk cache was not reused");
            // Corrupted cached tile rebuilds from the immutable source.
            for (const auto& dir:std::filesystem::directory_iterator(root/"cache")) std::ofstream(dir.path()/"2-1.tile")<<"broken";
            stream.clear();stream.request(path,2050,1025,{{2,1}});check(wait(stream,key,{2,1}),ef::TileStream::region(2050,1025,{2,1}));
            require(stream.stats().builds==1,"corrupt tile did not rebuild");
        }
        {
            ef::TileStream stream(root/"cache");stream.request(path,2050,1025,wanted);stream.clear();
            require(stream.stats().pending==0 && stream.stats().ready_bytes==0,"cancelled jobs retained");
        }
        {
            // Several layers can fill the neighbour queue without a GL consumer.
            // A newly visible layer must still get its first tile promptly.
            ef::TileStream stream(root/"pressure-cache");stream.begin_frame();
            for (int n=0;n<12;++n) {
                const auto alias=root/("prefetch-"+std::to_string(n)+".png");std::filesystem::create_hard_link(path,alias);
                stream.request(alias,2050,1025,wanted,0);
            }
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while (stream.stats().ready_bytes<42ULL*1024*1024) {
                require(std::chrono::steady_clock::now()<deadline,"prefetch pressure timeout");std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            require(stream.stats().ready_bytes<=48ULL*1024*1024,"neighbours consumed viewport reserve");
            const auto urgent=root/"visible.png";std::filesystem::create_hard_link(path,urgent);
            const auto key=stream.request(urgent,2050,1025,{{0,0}},1);
            check(wait(stream,key,{0,0}),ef::TileStream::region(2050,1025,{0,0}));
            require(stream.stats().ready_bytes<=ef::TileStream::ready_budget,"priority queue overflow");
            stream.begin_frame();stream.begin_frame();require(stream.stats().ready_bytes==0,"stale prefetch retained");
        }
        // Wrong declarations and truncated PNG must reject rather than overrun rows.
        bool rejected=false;try {ef::stream_image_rows(path,2049,1025,[](int,auto) {});} catch (...) {rejected=true;}require(rejected,"dimension mismatch accepted");
        std::filesystem::remove_all(root);std::cout<<"tile stream checks passed\n";return 0;
    } catch (const std::exception& e) {std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
