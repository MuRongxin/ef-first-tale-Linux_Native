#pragma once
#include "scene/scene.hpp"
#include "script/script.hpp"

namespace ef {
class MessageUI {
public:
    MessageUI(const Content& content,const std::string& path);
    [[nodiscard]] Scene scene(int mode,const std::optional<Message>& message,const std::string& text,
                              std::size_t visible,bool ready,double elapsed) const;
    [[nodiscard]] static std::string display_name(const std::string& raw);
private:
    struct Mode {
        std::vector<Layer> images;
        Layer name, body;
        bool name_newline{};
        std::string name_prefix,name_suffix;
    };
    std::map<int,Mode> modes_;
    std::string advance_asset_;
    int frame_size_{},frames_{},frame_ms_{};
};
}
