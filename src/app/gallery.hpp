#pragma once
#include "app/save_menu.hpp"
#include "data/gallery.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace ef {
// One decode worker; GL uploads and texture destruction stay on the window thread.
class Gallery {
public:
    explicit Gallery(const Content&);
    ~Gallery();
    bool event(const SDL_Event&,std::optional<Point>); // true returns to title
    void draw(Renderer&,Font&,AssetTier,float);
private:
    struct Job {std::string key,id;bool full{};AssetTier tier{};std::uint64_t generation{};};
    struct Result {Job job;Image image;std::string error;};
    struct Thumb {Texture texture;std::uint64_t used{};};
    const Content& content_;
    GalleryCatalog catalog_;
    std::vector<std::size_t> visible_;
    std::vector<std::string> sections_{"全部"};
    std::size_t section_{},selected_{},variant_{};
    bool viewing_{};
    bool full_tiled_{};
    int pressed_{-1},limit_{};
    float zoom_{1},pan_x_{},pan_y_{};
    std::uint64_t generation_{},clock_{};
    std::string request_,full_key_,error_;
    std::unique_ptr<Texture> full_;
    int full_width_{},full_height_{};
    std::map<std::string,Thumb> thumbs_;
    std::set<std::string> failed_;
    std::filesystem::path disk_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    bool stopping_{};
    std::thread worker_;
    void work();
    void filter();
    void reset_view();
    void schedule(AssetTier);
    int hit(Point) const;
    static constexpr std::size_t page_size_=12;
};
}
