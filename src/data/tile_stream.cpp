#include "data/tile_stream.hpp"
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace ef {
namespace {
std::filesystem::path tile_path(const std::filesystem::path& dir,TileIndex index) {
    return dir/(std::to_string(index.x)+"-"+std::to_string(index.y)+".tile");
}
void write_tile(const std::filesystem::path& path,const Image& image) {
    uLongf size=compressBound(static_cast<uLong>(image.rgba.size()));
    std::vector<unsigned char> compressed(size);
    if (compress2(compressed.data(),&size,image.rgba.data(),static_cast<uLong>(image.rgba.size()),Z_BEST_SPEED)!=Z_OK)
        throw std::runtime_error("Cannot compress panorama tile");
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(compressed.data()),static_cast<std::streamsize>(size));out.close();
    if (!out) throw std::runtime_error("Cannot write panorama tile cache");
}
Image read_tile(const std::filesystem::path& path,TileRegion r) {
    Image result{r.width,r.height,std::vector<std::uint8_t>(static_cast<std::size_t>(r.width)*r.height*4)};
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if (!in || in.tellg()<=0 || in.tellg()>static_cast<std::streamoff>(compressBound(static_cast<uLong>(result.rgba.size()))))
        throw std::runtime_error("Invalid panorama tile cache size");
    std::vector<unsigned char> compressed(static_cast<std::size_t>(in.tellg()));in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(compressed.data()),static_cast<std::streamsize>(compressed.size())))
        throw std::runtime_error("Truncated panorama tile cache");
    auto size=static_cast<uLongf>(result.rgba.size());
    if (uncompress(result.rgba.data(),&size,compressed.data(),static_cast<uLong>(compressed.size()))!=Z_OK || size!=result.rgba.size())
        throw std::runtime_error("Corrupt panorama tile cache");
    return result;
}
std::string identity(const std::filesystem::path& path,int width,int height) {
    const auto value=path.string()+":"+std::to_string(std::filesystem::file_size(path))+":"+
        std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count())+":"+std::to_string(width)+":"+std::to_string(height);
    std::uint64_t hash=14695981039346656037ULL;
    for (const unsigned char c:value) {hash^=c;hash*=1099511628211ULL;}
    std::ostringstream out;out<<std::hex<<hash;return out.str();
}
}
TileRegion TileStream::region(int width,int height,TileIndex i) {
    if (width<=0 || height<=0 || i.x<0 || i.y<0 || i.x>(width-1)/edge || i.y>(height-1)/edge)
        throw std::runtime_error("Panorama tile outside source");
    const int x=std::max(0,i.x*edge-1),y=std::max(0,i.y*edge-1);
    return {x,y,std::min(width,i.x*edge+edge+1)-x,std::min(height,i.y*edge+edge+1)-y};
}
TileStream::TileStream(std::filesystem::path root):root_(std::move(root)) {
    if (root_.empty()) {
        if (const auto cache=std::getenv("XDG_CACHE_HOME");cache && *cache) root_=std::filesystem::path(cache)/"ef-native/tiles-v1";
        else if (const auto home=std::getenv("HOME");home && *home) root_=std::filesystem::path(home)/".cache/ef-native/tiles-v1";
    }
    worker_=std::thread([this] {work();});
}
TileStream::~TileStream() {stopping_=true;++epoch_;wake_.notify_all();if (worker_.joinable()) worker_.join();}
void TileStream::begin_frame() {
    std::lock_guard lock(mutex_);++frame_;
    for (auto it=ready_.begin();it!=ready_.end();) {
        const auto source=sources_.find(it->first.first);
        if (source==sources_.end() || source->second.frame+1<frame_) {bytes_-=it->second.image.rgba.size();it=ready_.erase(it);} else ++it;
    }
    wake_.notify_one();
}
std::string TileStream::request(const std::filesystem::path& path,int w,int h,const std::vector<TileIndex>& missing,std::size_t visible) {
    const auto key=path.string()+":"+std::to_string(w)+":"+std::to_string(h);
    std::lock_guard lock(mutex_);
    auto [it,inserted]=sources_.try_emplace(key);
    auto& source=it->second;
    if (inserted) {source.path=path;source.width=w;source.height=h;}
    if (source.frame!=frame_) {source.wanted.clear();source.urgent.clear();}
    source.frame=frame_;
    std::size_t position{};
    for (const auto index:missing) {
        (void)region(w,h,index);
        if (position++<visible) source.urgent.insert(index);
        if (std::find(source.wanted.begin(),source.wanted.end(),index)==source.wanted.end()) source.wanted.push_back(index);
    }
    for (auto ready=ready_.begin();ready!=ready_.end();) {
        if (ready->first.first==key && std::find(source.wanted.begin(),source.wanted.end(),ready->first.second)==source.wanted.end()) {
            bytes_-=ready->second.image.rgba.size();ready=ready_.erase(ready);
        } else ++ready;
    }
    wake_.notify_one();return key;
}
std::optional<Image> TileStream::take(const std::string& key,TileIndex index) {
    std::lock_guard lock(mutex_);const auto found=ready_.find({key,index});
    if (found==ready_.end()) return {};
    bytes_-=found->second.image.rgba.size();auto result=std::move(found->second.image);ready_.erase(found);
    if (auto s=sources_.find(key);s!=sources_.end()) {std::erase(s->second.wanted,index);s->second.urgent.erase(index);}
    wake_.notify_one();return result;
}
void TileStream::clear() {
    std::lock_guard lock(mutex_);++epoch_;sources_.clear();ready_.clear();bytes_=0;wake_.notify_all();
}
TileStream::Stats TileStream::stats() const {
    std::lock_guard lock(mutex_);std::size_t pending{};for (const auto& [key,s]:sources_) pending+=s.wanted.size();
    return {bytes_,pending,builds_,reads_};
}
void TileStream::prepare(Source& source,std::uint64_t epoch) {
    if (root_.empty()) throw std::runtime_error("No writable panorama cache location");
    std::filesystem::create_directories(root_);
    source.cache=root_/identity(source.path,source.width,source.height);
    if (std::filesystem::is_regular_file(source.cache/"complete")) return;
    const auto temp=source.cache.string()+".tmp-"+std::to_string(getpid())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Scratch {std::filesystem::path path;~Scratch() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}} scratch{temp};
    std::filesystem::create_directory(temp);
    const std::size_t stride=static_cast<std::size_t>(source.width)*4;
    // One band, never the entire uncompressed panorama (158 MiB at 40,432 pixels).
    if (stride*static_cast<std::size_t>(edge+2)>192ULL*1024*1024) throw std::runtime_error("Panorama scanline band exceeds 192 MiB");
    std::vector<std::uint8_t> band(stride*static_cast<std::size_t>(edge+2));
    int tile_y=0,first=0,last=std::min(source.height,edge+1),stored=0;
    stream_image_rows(source.path,source.width,source.height,[&](int y,std::span<const std::uint8_t> row) {
        if (stopping_ || epoch_.load()!=epoch) throw std::runtime_error("Panorama preparation cancelled");
        if (y%32==0) {
            std::lock_guard lock(mutex_);
            const auto key=source.path.string()+":"+std::to_string(source.width)+":"+std::to_string(source.height);
            const auto active=sources_.find(key);
            if (active==sources_.end() || active->second.frame+1<frame_) throw std::runtime_error("Panorama view changed");
        }
        std::copy(row.begin(),row.end(),band.begin()+static_cast<std::ptrdiff_t>(stride*static_cast<std::size_t>(stored++)));
        while (y+1==last && tile_y<=(source.height-1)/edge) {
        for (int x=0;x<=(source.width-1)/edge;++x) {
            if (stopping_ || epoch_.load()!=epoch) throw std::runtime_error("Panorama preparation cancelled");
            const auto r=region(source.width,source.height,{x,tile_y});
            Image tile{r.width,r.height,std::vector<std::uint8_t>(static_cast<std::size_t>(r.width)*r.height*4)};
            for (int row_y=0;row_y<r.height;++row_y)
                std::copy_n(band.data()+static_cast<std::size_t>(row_y)*stride+static_cast<std::size_t>(r.x)*4,
                            static_cast<std::size_t>(r.width)*4,tile.rgba.data()+static_cast<std::size_t>(row_y)*r.width*4);
            write_tile(tile_path(temp,{x,tile_y}),tile);
        }
        ++tile_y;if (tile_y>(source.height-1)/edge) return;
        const int next_first=tile_y*edge-1;
        const int carry=last-next_first; // last core row and the next band's first core row
        std::memmove(band.data(),band.data()+static_cast<std::size_t>(next_first-first)*stride,static_cast<std::size_t>(carry)*stride);
        first=next_first;stored=carry;last=std::min(source.height,tile_y*edge+edge+1);
        }
    });
    std::ofstream complete(temp+"/complete");complete<<"ef.tiles.v1 "<<source.width<<' '<<source.height;complete.close();
    if (!complete) throw std::runtime_error("Cannot finish panorama tile cache");
    // Other renderer instances may finish the same immutable source concurrently.
    if (!std::filesystem::is_regular_file(source.cache/"complete")) {
        std::error_code error;std::filesystem::rename(temp,source.cache,error);
        if (error && !std::filesystem::is_regular_file(source.cache/"complete"))
            throw std::runtime_error("Cannot publish panorama tile cache: "+error.message());
    }
    {std::lock_guard lock(mutex_);++builds_;}
    try {prune_disk(source.cache);} catch (...) { /* eviction is optional */ }
}
void TileStream::prune_disk(const std::filesystem::path& keep) {
    constexpr std::uintmax_t budget=2ULL*1024*1024*1024;
    struct Item {std::filesystem::path path;std::filesystem::file_time_type time;std::uintmax_t bytes;};
    std::vector<Item> items;std::uintmax_t total{};
    for (const auto& dir:std::filesystem::directory_iterator(root_)) {
        if (!dir.is_directory() || !std::filesystem::is_regular_file(dir.path()/"complete")) continue;
        std::uintmax_t bytes{};for (const auto& f:std::filesystem::directory_iterator(dir.path())) if (f.is_regular_file()) bytes+=f.file_size();
        items.push_back({dir.path(),std::filesystem::last_write_time(dir.path()/"complete"),bytes});total+=bytes;
    }
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b) {return a.time<b.time;});
    for (const auto& i:items) if (total>budget && i.path!=keep) {std::filesystem::remove_all(i.path);total-=i.bytes;}
}
void TileStream::work() {
    for (;;) {
        Source source;std::string key;TileIndex index;std::uint64_t epoch{};
        {
            std::unique_lock lock(mutex_);
            auto next=[&] {
                for (const bool urgent:{true,false})
                    for (const auto& [k,s]:sources_) if (!s.failed && s.frame+1>=frame_)
                        for (const auto i:s.wanted) if (s.urgent.contains(i)==urgent && !ready_.contains({k,i})) {key=k;source=s;index=i;return true;}
                return false;
            };
            wake_.wait(lock,[&] {
                if (stopping_) return true;
                if (!next()) return false;
                const bool urgent=source.urgent.contains(index);
                const auto limit=urgent ? ready_budget : ready_budget-16ULL*1024*1024;
                const auto maximum=static_cast<std::size_t>(edge+2)*(edge+2)*4;
                // Never let neighbours from one layer starve another layer's viewport.
                if (urgent) for (auto it=ready_.begin();bytes_+maximum>limit && it!=ready_.end();) {
                    const auto owner=sources_.find(it->first.first);
                    if (owner==sources_.end() || owner->second.frame+1<frame_ || !owner->second.urgent.contains(it->first.second)) {
                        bytes_-=it->second.image.rgba.size();it=ready_.erase(it);
                    } else ++it;
                }
                return bytes_+maximum<=limit;
            });
            if (stopping_) return;
            epoch=epoch_;
        }
        try {
            if (!source.prepared) prepare(source,epoch);
            if (stopping_ || epoch_.load()!=epoch) continue;
            Image image;
            try {image=read_tile(tile_path(source.cache,index),region(source.width,source.height,index));}
            catch (...) {
                std::error_code ignored;std::filesystem::remove_all(source.cache,ignored);prepare(source,epoch);
                image=read_tile(tile_path(source.cache,index),region(source.width,source.height,index));
            }
            std::lock_guard lock(mutex_);
            if (epoch_.load()!=epoch || stopping_) continue;
            auto found=sources_.find(key);if (found==sources_.end()) continue;
            found->second.prepared=true;found->second.cache=source.cache;++reads_;
            if (found->second.frame+1<frame_ || std::find(found->second.wanted.begin(),found->second.wanted.end(),index)==found->second.wanted.end()) continue;
            // Retain only requested neighbourhoods; stale results cannot fill the bounded queue.
            for (auto it=ready_.begin();it!=ready_.end();) {
                auto active=sources_.find(it->first.first);
                if (active==sources_.end() || active->second.frame+1<frame_ ||
                    std::find(active->second.wanted.begin(),active->second.wanted.end(),it->first.second)==active->second.wanted.end()) {
                    bytes_-=it->second.image.rgba.size();it=ready_.erase(it);
                } else ++it;
            }
            bytes_+=image.rgba.size();ready_.emplace(std::pair{key,index},Ready{std::move(image),frame_});
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            if (epoch_.load()!=epoch || stopping_) continue;
            if (auto found=sources_.find(key);found!=sources_.end()) {
                if (found->second.frame+1<frame_) continue;
                found->second.failed=true;
            }
            std::cerr<<"[tiles] "<<source.path<<": "<<e.what()<<" (using preview)\n"<<std::flush;
        }
    }
}
}
