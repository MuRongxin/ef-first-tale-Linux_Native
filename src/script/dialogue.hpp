#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ef {
enum class DialogueKind { text, automatic, pause, voice };
struct DialogueEvent {
    DialogueKind kind{DialogueKind::text};
    std::string text;
    int ticks{10};
    bool operator==(const DialogueEvent&) const = default;
};
std::vector<DialogueEvent> legacy_dialogue_events(const std::string& text);

// A development text clock: callers choose the reveal speed; no original CPS is assumed.
// Source is UTF-8 and already decoded by the importer. Supported controls are
// \n, \a, \w, \v and \\. Limits: 1 MiB source and 65,536 tokens per message.
class Dialogue {
public:
    Dialogue(std::string text, double characters_per_second);
    Dialogue(const std::vector<DialogueEvent>& events, double characters_per_second);
    // voice_playing describes the supplied interval. It is also remembered for reveal().
    void update(double seconds, bool voice_playing);
    // Reveal reachable text immediately. Fixed waits and a playing voice still block.
    // Before the first update, voice status is conservatively treated as playing.
    void reveal();
    [[nodiscard]] const std::string& full_text() const { return full_text_; }
    [[nodiscard]] const std::string& text() const { return displayed_; }
    [[nodiscard]] bool complete() const;
    [[nodiscard]] bool waits_for_voice() const { return needs_voice_; }
    [[nodiscard]] bool automatic() const { return automatic_; }
private:
    enum class Kind { glyph, automatic, wait, voice };
    struct Token { Kind kind; std::size_t begin{}, length{}; double pause{0.1}; };
    void advance(double seconds, bool immediate);
    std::string source_, displayed_, full_text_;
    std::vector<Token> tokens_;
    std::size_t cursor_{};
    double seconds_per_character_{}, credit_{}, pause_{};
    bool needs_voice_{}, automatic_{}, voice_wait_{}, voice_playing_{true};
};
}
