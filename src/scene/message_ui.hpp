#pragma once
#include "scene/scene.hpp"
#include "script/script.hpp"

namespace ef {
class MessageUI {
    friend struct CheckpointCodec;
public:
    MessageUI(const Content& content,const std::string& path);
    // Feed panel/message events before building a scene; rendering never appends text.
    void set_mode(int mode); // Immediate reset for restore/audit; script events use transition_to.
    void transition_to(int mode,Scene outgoing);
    void update(double seconds);
    [[nodiscard]] bool busy() const { return transitioning_; }
    [[nodiscard]] float progress() const;
    void begin_message(int mode,const Message& message);
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
    int mode_{};
    std::optional<Message> page_message_, active_message_;
    std::string prefix_, current_text_;
    std::size_t prefix_characters_{};
    struct Label { std::string text; std::size_t byte_offset{}; };
    std::vector<Label> page_labels_;
    Scene outgoing_;
    double transition_elapsed_{};
    bool transitioning_{};
    [[nodiscard]] Scene current_scene(int mode,const std::optional<Message>& message,const std::string& text,
                                      std::size_t visible,bool ready,double elapsed) const;
};
}
