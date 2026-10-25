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
constexpr double fixed_pause = 0.1;
}
Dialogue::Dialogue(std::string text, double characters_per_second) : source_(std::move(text)) {
    if (!std::isfinite(characters_per_second) || characters_per_second < 0)
        throw std::runtime_error("Dialogue speed must be finite and nonnegative");
    if (characters_per_second > 0) {
        seconds_per_character_ = 1 / characters_per_second;
        if (!std::isfinite(seconds_per_character_)) throw std::runtime_error("Dialogue speed is too small");
    }
    if (source_.size() > maximum_source_bytes) throw std::runtime_error("Dialogue exceeds 1 MiB source budget");
    (void)decode_utf8(source_);
    if (source_.find('\0') != std::string::npos) throw std::runtime_error("NUL in dialogue");
    for (std::size_t offset = 0; offset < source_.size();) {
        if (tokens_.size() == maximum_tokens) throw std::runtime_error("Dialogue exceeds 65,536 token budget");
        const auto first = static_cast<unsigned char>(source_[offset]);
        if (first == '\\') {
            if (offset+1 == source_.size()) throw std::runtime_error("Dangling dialogue escape");
            const auto control = source_[offset+1];
            switch (control) {
            case 'n': tokens_.push_back({Kind::glyph,offset,0}); break;
            case '\\': tokens_.push_back({Kind::glyph,offset,1}); break;
            case 'a': tokens_.push_back({Kind::automatic}); break;
            case 'w': tokens_.push_back({Kind::wait}); break;
            case 'v': tokens_.push_back({Kind::voice}); needs_voice_ = true; break;
            default: throw std::runtime_error("Unsupported dialogue escape at byte " + std::to_string(offset));
            }
            offset += 2;
        } else {
            // Strict UTF-8 validation above guarantees complete, canonical code points.
            const std::size_t length = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            tokens_.push_back({Kind::glyph,offset,length});
            offset += length;
        }
    }
    displayed_.reserve(source_.size());
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
            pause_ = fixed_pause;
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
