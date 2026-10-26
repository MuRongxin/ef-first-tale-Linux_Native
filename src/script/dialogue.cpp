#include "script/dialogue.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ef {
namespace {
constexpr std::size_t maximum_source_bytes = 1024 * 1024;
constexpr std::size_t maximum_tokens = 65536;
}
std::vector<DialogueEvent> legacy_dialogue_events(const std::string& text) {
    if (text.size()>maximum_source_bytes || text.find('\0')!=std::string::npos) throw std::runtime_error("Invalid dialogue source budget/NUL");
    (void)decode_utf8(text);
    std::vector<DialogueEvent> events;std::string buffer;
    auto flush=[&] {if (!buffer.empty()) {events.push_back({DialogueKind::text,std::move(buffer)});buffer.clear();}};
    for (std::size_t offset=0;offset<text.size();) {
        const char c=text[offset++];
        if (c!='\\') {buffer+=c;continue;}
        if (offset==text.size()) throw std::runtime_error("Dangling dialogue escape");
        switch (text[offset++]) {
        case 'n':buffer+='\n';break;
        case '\\':buffer+='\\';break;
        case 'a':flush();events.push_back({DialogueKind::automatic,{}});break;
        case 'w':flush();events.push_back({DialogueKind::pause,{}});break;
        case 'v':flush();events.push_back({DialogueKind::voice,{}});break;
        default:throw std::runtime_error("Unsupported dialogue escape at byte "+std::to_string(offset-2));
        }
        if (events.size()>maximum_tokens) throw std::runtime_error("Dialogue event budget exceeded");
    }
    flush();return events;
}
Dialogue::Dialogue(std::string text,double speed) : Dialogue(legacy_dialogue_events(text),speed) {}
Dialogue::Dialogue(const std::vector<DialogueEvent>& events,double characters_per_second) {
    if (!std::isfinite(characters_per_second) || characters_per_second<0) throw std::runtime_error("Dialogue speed must be finite and nonnegative");
    if (characters_per_second>0) {seconds_per_character_=1/characters_per_second;if (!std::isfinite(seconds_per_character_)) throw std::runtime_error("Dialogue speed is too small");}
    if (events.size()>maximum_tokens) throw std::runtime_error("Dialogue event budget exceeded");
    for (const auto& event:events) {
        if (event.kind==DialogueKind::text) {
            if (event.text.size()>maximum_source_bytes-source_.size() || event.text.find('\0')!=std::string::npos) throw std::runtime_error("Invalid dialogue text budget/NUL");
            (void)decode_utf8(event.text);const auto start=source_.size();source_+=event.text;
            for (std::size_t offset=start;offset<source_.size();) {
                if (tokens_.size()==maximum_tokens) throw std::runtime_error("Dialogue exceeds 65,536 token budget");
                const auto first=static_cast<unsigned char>(source_[offset]);
                const std::size_t length=first<0x80 ? 1 : first<0xe0 ? 2 : first<0xf0 ? 3 : 4;
                tokens_.push_back({Kind::glyph,offset,length});offset+=length;
            }
        } else {
            if (tokens_.size()==maximum_tokens) throw std::runtime_error("Dialogue exceeds 65,536 token budget");
            if (event.kind==DialogueKind::automatic) tokens_.push_back({Kind::automatic});
            else if (event.kind==DialogueKind::voice) {tokens_.push_back({Kind::voice});needs_voice_=true;}
            else if (event.kind==DialogueKind::pause) {
                if (event.ticks<0 || event.ticks>1000000) throw std::runtime_error("Dialogue pause out of range");
                tokens_.push_back({Kind::wait,0,0,event.ticks/100.0});
            } else throw std::runtime_error("Unknown dialogue event");
        }
    }
    displayed_.reserve(source_.size());full_text_=source_;
}
bool Dialogue::complete() const { return cursor_ == tokens_.size() && pause_ == 0 && !voice_wait_; }
void Dialogue::update(double seconds, bool voice_playing) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid dialogue delta time");
    voice_playing_ = voice_playing;
    advance(seconds,false);
}
void Dialogue::reveal() { advance(0,true); }
void Dialogue::advance(double seconds, bool immediate) {
    // Constructor bounds tokens, so even zero-delay text cannot spin indefinitely.
    // Saturating the addition also accepts very large finite headless deltas.
    double remaining = seconds;
    if (credit_ > 0) remaining = seconds > std::numeric_limits<double>::max()-credit_ ?
        std::numeric_limits<double>::max() : seconds+credit_;
    credit_ = 0;
    for (std::size_t events = 0; events <= maximum_tokens; ++events) {
        if (pause_ > 0) {
            if (immediate) return;
            const double consumed = std::min(pause_,remaining);
            pause_ -= consumed;
            remaining -= consumed;
            if (pause_ > 1e-12) return;
            pause_ = 0;
        }
        if (voice_wait_) {
            if (voice_playing_) return;
            voice_wait_ = false;
        }
        if (cursor_ == tokens_.size()) return;
        const auto& token = tokens_[cursor_];
        switch (token.kind) {
        case Kind::glyph:
            if (!immediate && seconds_per_character_ > 0) {
                if (remaining < seconds_per_character_ && seconds_per_character_-remaining > seconds_per_character_*1e-12) {
                    credit_ = remaining;
                    return;
                }
                remaining = std::max(0.0,remaining-seconds_per_character_);
            }
            if (token.length) displayed_.append(source_,token.begin,token.length);
            else displayed_ += '\n';
            ++cursor_;
            break;
        case Kind::automatic:
            automatic_ = true;
            ++cursor_;
            break;
        case Kind::wait:
            pause_ = token.pause;
            ++cursor_;
            break;
        case Kind::voice:
            ++cursor_;
            if (voice_playing_) voice_wait_ = true;
            break;
        }
    }
    throw std::runtime_error("Dialogue update event budget exhausted");
}
}
