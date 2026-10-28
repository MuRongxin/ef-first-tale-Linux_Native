#pragma once
#include "data/content.hpp"
namespace ef {
struct GalleryGroup {
    std::string id, section;
    std::vector<std::string> variants, thumbnails;
};
struct GalleryCatalog {
    explicit GalleryCatalog(const Content&);
    std::vector<GalleryGroup> groups;
    std::map<std::string,std::string> excluded;
    [[nodiscard]] nlohmann::json report() const;
};
}
