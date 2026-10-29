#include "app/gallery.hpp"
#include <png.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
namespace ef {
namespace {
std::string cache_name(const std::filesystem::path& path) {
    // Versioned key includes package identity and source changes, even for non-blob fixtures.
    const auto s=path.string()+std::to_string(std::filesystem::file_size(path))+std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count());
    std::uint64_t hash=14695981039346656037ULL;
    for (const unsigned char ch:s) {hash^=ch;hash*=1099511628211ULL;}
    std::ostringstream out;out<<std::hex<<hash;return out.str()+".png";
}
void cache_write(const std::filesystem::path& path,const Image& image) {
    const auto tmp=path.string()+".tmp";
    png_image png{};png.version=PNG_IMAGE_VERSION;png.width=static_cast<unsigned>(image.width);png.height=static_cast<unsigned>(image.height);png.format=PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png,tmp.c_str(),0,image.rgba.data(),0,nullptr)) {std::filesystem::remove(tmp);return;}
    std::filesystem::rename(tmp,path);
    // <=512 thumbnails, each <=256x192 RGBA (roughly 100 MiB worst case as PNG).
    std::vector<std::filesystem::directory_entry> files;
    for (const auto& file:std::filesystem::directory_iterator(path.parent_path())) if (file.path().extension()==".png") files.push_back(file);
    if (files.size()>512) {
        std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) {return a.last_write_time()<b.last_write_time();});
        for (std::size_t i=0;i<files.size()-512;++i) std::filesystem::remove(files[i]);
    }
}
}
Gallery::Gallery(const Content& content):content_(content),catalog_(content) {
    GLint maximum{};glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);limit_=std::min(8192,maximum);
    if (limit_<1) throw std::runtime_error("Gallery requires a GL context");
    for (const auto& g:catalog_.groups) if (std::find(sections_.begin(),sections_.end(),g.section)==sections_.end()) sections_.push_back(g.section);
    try {
        if (const auto cache=std::getenv("XDG_CACHE_HOME");cache && *cache) disk_=std::filesystem::path(cache)/"ef-native/gallery-v1";
        else if (const auto home=std::getenv("HOME");home && *home) disk_=std::filesystem::path(home)/".cache/ef-native/gallery-v1";
        if (!disk_.empty()) std::filesystem::create_directories(disk_);
    } catch (...) {disk_.clear();}
    filter();worker_=std::thread([this] {work();});
    std::cout<<"[gallery] opened groups="<<catalog_.groups.size()<<" variants="<<catalog_.report().at("variant_count")<<"\n"<<std::flush;
}
Gallery::~Gallery() {
    {std::lock_guard lock(mutex_);stopping_=true;jobs_.clear();results_.clear();}
    wake_.notify_one();if (worker_.joinable()) worker_.join();
}
void Gallery::filter() {
    visible_.clear();selected_=variant_=0;
    for (std::size_t i=0;i<catalog_.groups.size();++i) if (!section_ || catalog_.groups[i].section==sections_[section_]) visible_.push_back(i);
    reset_view();request_.clear();
}
void Gallery::reset_view() {zoom_=1;pan_x_=pan_y_=0;error_.clear();}
void Gallery::work() {
    for (;;) {
        Job job;
        {std::unique_lock lock(mutex_);wake_.wait(lock,[&] {return stopping_ || !jobs_.empty();});if (stopping_) return;job=std::move(jobs_.front());jobs_.pop_front();}
        Result result;result.job=job;
        try {
            std::string last;
            for (const auto& candidate:content_.candidates(job.id,job.full ? job.tier : AssetTier::x4)) {
                try {
                    const auto source=content_.resolve(candidate.path);
                    std::filesystem::path cached;
                    if (!job.full && !disk_.empty()) {
                        cached=disk_/cache_name(source);
                        if (std::filesystem::is_regular_file(cached)) {
                            try {result.image=decode_image_scaled(cached,256,192);break;} catch (...) {std::error_code ignored;std::filesystem::remove(cached,ignored);}
                        }
                    }
                    result.image=content_.decode_variant(candidate,job.full ? limit_ : 256,job.full ? limit_ : 192);
                    if (!cached.empty()) {try {cache_write(cached,result.image);} catch (...) { /* disk cache is optional */ }}
                    break;
                } catch (const std::exception& e) {last=e.what();}
            }
            if (result.image.rgba.empty()) throw std::runtime_error(last.empty() ? "没有可用图像" : last);
        } catch (const std::exception& e) {result.error=e.what();}
        {std::lock_guard lock(mutex_);if (stopping_) return;if (job.generation==generation_) results_.push_back(std::move(result));}
    }
}
void Gallery::schedule(AssetTier tier) {
    if (visible_.empty()) return;
    const auto page=selected_/page_size_;
    const auto& group=catalog_.groups[visible_[selected_]];
    const std::string state=viewing_ ? group.variants[variant_]+":"+std::to_string(static_cast<int>(tier)) : "page:"+std::to_string(section_)+":"+std::to_string(page);
    if (state==request_) return;
    request_=state;error_.clear();
    std::lock_guard lock(mutex_);++generation_;jobs_.clear();results_.clear();
    if (viewing_) {
        full_.reset();full_key_.clear();
        const auto candidates=content_.candidates(group.variants[variant_],tier);
        full_tiled_=!candidates.empty() && (candidates.front().width>8192 || candidates.front().height>8192 ||
            static_cast<std::uint64_t>(candidates.front().width)*candidates.front().height*4>128ULL*1024*1024);
        if (!full_tiled_) jobs_.push_back({state,group.variants[variant_],true,tier,generation_});
        std::cout<<"[gallery] view "<<group.variants[variant_]<<" tier="<<static_cast<int>(tier)<<"\n"<<std::flush;
    } else {
        full_.reset();full_key_.clear();
        for (std::size_t i=page*page_size_;i<std::min(visible_.size(),(page+1)*page_size_);++i) {
            const auto& id=catalog_.groups[visible_[i]].variants.front();
            if (!thumbs_.contains(id) && !failed_.contains(id)) jobs_.push_back({id,id,false,AssetTier::x4,generation_});
        }
    }
    wake_.notify_one();
}
int Gallery::hit(Point p) const {
    if (p.y>=545 && p.y<590) {if (p.x<150) return -2;if (p.x>=650) return -3;return -4;}
    if (p.y<85 && p.x>=250 && p.x<650) return -5;
    if (!viewing_ && p.x>=32 && p.x<768 && p.y>=100 && p.y<520) {
        const int col=static_cast<int>((p.x-32)/184),row=static_cast<int>((p.y-100)/140);
        if (col<4 && row<3) return row*4+col;
    }
    return -1;
}
bool Gallery::event(const SDL_Event& e,std::optional<Point> point) {
    auto move=[&](int delta) {
        if (visible_.empty()) return;
        if (viewing_) {const auto count=catalog_.groups[visible_[selected_]].variants.size();variant_=static_cast<std::size_t>((static_cast<int>(variant_)+delta+static_cast<int>(count))%static_cast<int>(count));}
        else {const auto n=static_cast<int>(visible_.size());selected_=static_cast<std::size_t>(std::clamp(static_cast<int>(selected_)+delta,0,n-1));}
        reset_view();
    };
    auto activate=[&](int target) {
        if (target==-4) {if (viewing_) {viewing_=false;request_.clear();reset_view();}else return true;}
        if (target==-2) move(viewing_ ? -1 : -static_cast<int>(page_size_));
        if (target==-3) move(viewing_ ? 1 : static_cast<int>(page_size_));
        if (target==-5 && !viewing_) {section_=(section_+1)%sections_.size();filter();}
        if (target>=0 && !viewing_) {const auto index=selected_/page_size_*page_size_+static_cast<std::size_t>(target);if (index<visible_.size()) {selected_=index;variant_=0;viewing_=true;reset_view();}}
        return false;
    };
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_RIGHT) return activate(-4);
    if (e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT) pressed_=point ? hit(*point) : -1;
    if (e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) {
        const auto down=pressed_;pressed_=-1;if (point && hit(*point)==down) return activate(down);
    }
    if (viewing_ && e.type==SDL_EVENT_MOUSE_WHEEL && e.wheel.y!=0) zoom_=std::clamp(zoom_*(e.wheel.y>0 ? 1.25f : .8f),1.f,8.f);
    if (e.type!=SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
    switch (e.key.key) {
    case SDLK_ESCAPE:case SDLK_BACKSPACE:return activate(-4);
    case SDLK_LEFT:move(-1);break;
    case SDLK_RIGHT:move(1);break;
    case SDLK_UP:move(viewing_ ? -1 : -4);break;
    case SDLK_DOWN:move(viewing_ ? 1 : 4);break;
    case SDLK_PAGEUP:move(viewing_ ? -1 : -12);break;
    case SDLK_PAGEDOWN:move(viewing_ ? 1 : 12);break;
    case SDLK_HOME:if (viewing_) variant_=0;else selected_=0;reset_view();break;
    case SDLK_END:if (!visible_.empty()) {if (viewing_) variant_=catalog_.groups[visible_[selected_]].variants.size()-1;else selected_=visible_.size()-1;}reset_view();break;
    case SDLK_RETURN:case SDLK_SPACE:if (viewing_) move(1);else if (!visible_.empty()) {viewing_=true;variant_=0;reset_view();}break;
    case SDLK_TAB:if (!viewing_) {section_=(section_+1)%sections_.size();filter();}break;
    case SDLK_EQUALS:case SDLK_KP_PLUS:zoom_=std::min(8.f,zoom_*1.25f);break;
    case SDLK_MINUS:case SDLK_KP_MINUS:zoom_=std::max(1.f,zoom_*.8f);break;
    case SDLK_0:reset_view();break;
    case SDLK_W:pan_y_+=60;break;case SDLK_S:pan_y_-=60;break;
    case SDLK_A:pan_x_+=60;break;case SDLK_D:pan_x_-=60;break;
    default:break;
    }
    return false;
}
void Gallery::draw(Renderer& r,Font& font,AssetTier tier,float scale) {
    schedule(tier);
    std::deque<Result> ready;
    {std::lock_guard lock(mutex_);ready.swap(results_);}
    for (auto& result:ready) {
        if (!result.error.empty()) {std::cerr<<"[gallery] "<<result.job.id<<": "<<result.error<<'\n';if (result.job.full) error_="图像无法读取，可切换其他差分或返回。";else failed_.insert(result.job.id);continue;}
        try {
        if (result.job.full) {
            full_.reset();full_=std::make_unique<Texture>(result.image);full_key_=result.job.key;full_width_=result.image.width;full_height_=result.image.height;
            std::cout<<"[gallery] ready "<<result.job.id<<" pixels="<<full_width_<<"x"<<full_height_<<" gpu_bytes="<<result.image.rgba.size()<<"\n"<<std::flush;
        } else {
            while (thumbs_.size()>=48) {
                const auto old=std::min_element(thumbs_.begin(),thumbs_.end(),[](const auto& a,const auto& b) {return a.second.used<b.second.used;});thumbs_.erase(old);
            }
            thumbs_.emplace(result.job.id,Thumb{Texture(result.image),++clock_});
            std::cout<<"[gallery] thumb "<<result.job.id<<" resident="<<thumbs_.size()<<"\n"<<std::flush;
        }
        } catch (const std::exception& e) {
            if (result.job.full) error_="高清纹理上传失败，可切换差分或返回。";else failed_.insert(result.job.id);
            std::cerr<<"[gallery] upload: "<<e.what()<<'\n';
        }
    }
    auto rect=[&](float x,float y,float w,float h,Color c) {Layer l;l.kind=LayerKind::rectangle;l.x=x;l.y=y;l.width=w;l.height=h;l.color=c;r.rectangle(l);};
    auto text=[&](std::string value,float x,float y,float w,float size=18) {Layer l;l.kind=LayerKind::text;l.text=std::move(value);l.x=x;l.y=y;l.width=w;l.font_size=size;l.kinsoku=true;font.draw(r,l,scale);};
    rect(0,0,800,600,{.025f,.04f,.07f,1});
    if (viewing_ && !visible_.empty()) {
        const auto& g=catalog_.groups[visible_[selected_]];
        if (full_ || full_tiled_) {
            const auto& design=content_.asset(g.variants[variant_]);
            const float fit=std::min(800.f/static_cast<float>(design.width),480.f/static_cast<float>(design.height));
            const float w=static_cast<float>(design.width)*fit*zoom_,h=static_cast<float>(design.height)*fit*zoom_;
            pan_x_=std::clamp(pan_x_,-std::max(0.f,(w-800)/2),std::max(0.f,(w-800)/2));
            pan_y_=std::clamp(pan_y_,-std::max(0.f,(h-480)/2),std::max(0.f,(h-480)/2));
            if (full_tiled_) {
                Layer l;l.kind=LayerKind::image;l.asset=g.variants[variant_];l.x=(800-w)/2+pan_x_;l.y=60+(480-h)/2+pan_y_;l.width=w;l.height=h;
                r.image(content_,l,tier);
            } else r.quad(*full_,(800-w)/2+pan_x_,60+(480-h)/2+pan_y_,w,h,{1,1,1,1});
        } else text(error_.empty() ? "正在加载高清图…" : error_,230,270,500);
        rect(0,0,800,60,{.025f,.04f,.07f,1});rect(0,530,800,70,{.025f,.04f,.07f,1});
        text(g.id+"   差分 "+std::to_string(variant_+1)+" / "+std::to_string(g.variants.size()),24,10,760,22);
        text(g.variants[variant_],24,37,760,13);
        text("← → 差分 · 滚轮/+−缩放 · WASD平移 · 0适应",165,535,600,14);
    } else {
        text("CG 图鉴 · 全部开放",32,24,340,26);text(sections_[section_]+"（Tab 切换分组）",380,28,400,19);
        const auto page=selected_/page_size_,pages=(visible_.size()+page_size_-1)/page_size_;
        text(std::to_string(visible_.size())+" 组 / 第 "+std::to_string(visible_.empty() ? 0 : page+1)+" / "+std::to_string(pages)+" 页",32,67,700,16);
        for (std::size_t slot=0;slot<page_size_ && page*page_size_+slot<visible_.size();++slot) {
            const auto index=page*page_size_+slot;const auto& g=catalog_.groups[visible_[index]];
            const float x=32+static_cast<float>(slot%4)*184,y=100+static_cast<float>(slot/4)*140;
            rect(x-2,y-2,174,132,index==selected_ ? Color{.35f,.55f,.7f,1} : Color{.08f,.13f,.20f,1});
            const auto thumb=thumbs_.find(g.variants.front());
            if (thumb!=thumbs_.end()) {
                thumb->second.used=++clock_;const auto& asset=content_.asset(g.variants.front());
                const float fit=std::min(168.f/static_cast<float>(asset.width),100.f/static_cast<float>(asset.height));
                const float w=static_cast<float>(asset.width)*fit,h=static_cast<float>(asset.height)*fit;
                r.quad(thumb->second.texture,x+(168-w)/2,y+(100-h)/2,w,h,{1,1,1,1});
            } else text(failed_.contains(g.variants.front()) ? "读取失败" : "加载中…",x+35,y+38,130,16);
            text(g.id+" · "+std::to_string(g.variants.size())+" 张",x+3,y+105,167,16);
        }
        if (visible_.empty()) text("内容包中没有可用 CG。",230,270,500);
    }
    text(viewing_ ? "〈上一差分" : "〈上一页",25,563,150);
    text(viewing_ ? "返回图鉴（Esc）" : "返回标题（Esc）",295,563,250);
    text(viewing_ ? "下一差分〉" : "下一页〉",655,563,145);
}
}
