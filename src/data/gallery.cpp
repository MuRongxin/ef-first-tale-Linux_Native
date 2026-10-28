#include "data/gallery.hpp"
#include <regex>
#include <algorithm>
namespace ef {
GalleryCatalog::GalleryCatalog(const Content& content) {
    // Observed numbered event/ending families, not the archive or inventory's broad cg label.
    const std::regex numbered(R"(^((evh?|ed)([0-9]+)_[0-9]+).*[.]png$)");
    std::map<std::string,GalleryGroup> found;
    std::vector<std::pair<std::string,std::string>> thumbs;
    for (const auto& [id,asset]:content.assets()) {
        (void)asset;
        const auto normalized=resource_name(id);
        const auto slash=normalized.find('/');
        if (slash==std::string::npos) continue;
        const auto tag=normalized.substr(0,slash),name=normalized.substr(slash+1);
        std::smatch match;
        if ((tag=="sys" || tag=="r18_sys") && name.starts_with("thumb/")) {
            const auto base=name.substr(6);
            if (std::regex_match(base,match,numbered)) thumbs.emplace_back(match[1].str(),id);
            continue;
        }
        if (tag!="bg" && tag!="r18_bg") continue;
        if (!name.starts_with("ev") && !name.starts_with("ed")) continue;
        if (!std::regex_match(name,match,numbered)) {excluded[id]="字幕或天空演出素材（非编号 CG）";continue;}
        const auto key=match[1].str();
        if (name.find("kiridashi")!=std::string::npos) {excluded[id]="Cutin 分层组件";continue;}
        if (key=="ev01_019" || key=="ev01_033" || key=="ev01_074" || key=="ev01_206") {
            excluded[id]="眼部切入或无人背景（本地原图核对）";continue;
        }
        auto& group=found[key];group.id=key;
        group.section=match[2]=="ed" ? "结局" : match[2]=="evh" ? "成人补充" : "事件";
        group.section+=" "+match[3].str();
        group.variants.push_back(id);
    }
    for (auto& [key,group]:found) {
        for (const auto& [anchor,id]:thumbs) if (anchor==key) group.thumbnails.push_back(id);
        std::sort(group.variants.begin(),group.variants.end(),[](const auto& a,const auto& b) {
            return resource_name(a).substr(a.find('/')+1)<resource_name(b).substr(b.find('/')+1);
        });
        // Prefer the original gallery's representative, retaining every numbered variant.
        if (!group.thumbnails.empty()) {
            auto name=resource_name(group.thumbnails.front());name=name.substr(name.find("thumb/")+6);
            const auto first=std::find_if(group.variants.begin(),group.variants.end(),[&](const auto& id) {return resource_name(id).substr(id.find('/')+1)==name;});
            if (first!=group.variants.end()) std::rotate(group.variants.begin(),first,first+1);
        }
        groups.push_back(std::move(group));
    }
    const auto order=[](const GalleryGroup& g) {return g.id.starts_with("ed") ? 1 : g.id.starts_with("evh") ? 2 : 0;};
    std::sort(groups.begin(),groups.end(),[&](const auto& a,const auto& b) {
        if (order(a)!=order(b)) return order(a)<order(b);
        return a.id<b.id;
    });
}
nlohmann::json GalleryCatalog::report() const {
    nlohmann::json rows=nlohmann::json::array();std::size_t count{};
    for (const auto& g:groups) {rows.push_back({{"id",g.id},{"section",g.section},{"variants",g.variants},{"original_thumbnails",g.thumbnails}});count+=g.variants.size();}
    return {{"version",1},{"group_count",groups.size()},{"variant_count",count},{"groups",rows},{"excluded",excluded}};
}
}
