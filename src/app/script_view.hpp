#pragma once
#include "app/application.hpp"
#include "script/script.hpp"
#include "scene/scene.hpp"
#include "scene/presentation.hpp"
#include "scene/message_ui.hpp"
namespace ef {
class ScriptView {
public:
    ScriptView(const Content& content, const Options& options);
    void update(double seconds);
    void advance() { runtime_.advance(); }
    void choose(std::size_t index);
    void click(Point point);
    [[nodiscard]] Scene scene() const;
    [[nodiscard]] const ScriptRuntime& runtime() const { return runtime_; }
    Presentation& presentation() { return presentation_; }
    const Presentation& presentation() const { return presentation_; }
private:
    Presentation presentation_;
    ScriptLibrary library_;
    ScriptRuntime runtime_;
    Scene template_;
    std::optional<MessageUI> message_ui_;
    double elapsed_{}, ready_started_{};
    Options options_;
    std::size_t reported_{};
    std::uint64_t last_message_{}, last_ready_{};
    ScriptState last_state_{ScriptState::running};
    const Layer* choice_layer() const;
};
}
