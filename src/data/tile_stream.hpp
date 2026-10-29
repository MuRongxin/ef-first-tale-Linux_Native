#pragma once
#include "data/image.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace ef {
struct TileIndex {
    int x{},y{};
    auto operator<=>(const TileIndex&) const = default;
};
struct TileRegion { int x{},y{},width{},height{}; };
// A one-pixel apron preserves bilinear samples on both sides of every seam.
class TileStream {
public:
    static constexpr int edge=1024;
    static constexpr std::size_t ready_budget=64ULL*1024*1024;
    explicit TileStream(std::filesystem::path cache_root = {});
    ~TileStream();
    TileStream(const TileStream&)=delete;
    TileStream& operator=(const TileStream&)=delete;
    void begin_frame();
    // Ordered visible tiles first, then neighbouring tiles. Never performs decode/I/O.
    std::string request(const std::filesystem::path&,int width,int height,const std::vector<TileIndex>& missing,
                        std::size_t visible=std::numeric_limits<std::size_t>::max());
    std::optional<Image> take(const std::string&,TileIndex);
    void clear();
    static TileRegion region(int width,int height,TileIndex);
    struct Stats {std::size_t ready_bytes{},pending{};std::uint64_t builds{},reads{};};
    Stats stats() const;
private:
    struct Source {
        std::filesystem::path path,cache;
        int width{},height{};
        std::vector<TileIndex> wanted;
        std::set<TileIndex> urgent;
        std::uint64_t frame{};
        bool prepared{},failed{};
    };
    struct Ready {Image image;std::uint64_t used{};};
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string,Source> sources_;
    std::map<std::pair<std::string,TileIndex>,Ready> ready_;
    std::size_t bytes_{};
    std::uint64_t frame_{},builds_{},reads_{};
    std::atomic<std::uint64_t> epoch_{};
    std::atomic<bool> stopping_{};
    std::thread worker_;
    void work();
    void prepare(Source&,std::uint64_t epoch);
    void prune_disk(const std::filesystem::path& keep);
};
}
