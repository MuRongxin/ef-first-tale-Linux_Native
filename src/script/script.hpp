#pragma once
#include "data/content.hpp"
#include "script/dialogue.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ef {
struct Instruction {
    std::string script, command, raw;
    std::size_t line{};
    char variant{};
    std::vector<std::string> args;
    std::optional<std::vector<DialogueEvent>> events{};
    bool colon_variant{}, unsupported{};
    [[nodiscard]] std::string location() const;
};
// Only ef_legacy_script (offline tools/tests) defines the SC lexical parser.
void validate_instruction(const Instruction& instruction);
std::vector<Instruction> parse_script(std::string_view text, const std::string& id);
std::vector<Instruction> script_json(const nlohmann::json& document,const std::string& id);
std::vector<Instruction> read_script_json(std::string_view text,const std::string& id);
bool core_command(const std::string& name);
struct Program {
    std::string id;
    std::vector<Instruction> instructions;
    std::map<std::string, std::size_t> labels;
};
class ScriptLibrary {
public:
    using LegacyParser=std::function<std::vector<Instruction>(std::string_view,const std::string&)>;
    // A legacy callback is exclusively for offline migration verification; application uses JSON.
    explicit ScriptLibrary(const Content& content,const std::string& index_path={},LegacyParser legacy_parser={});
    [[nodiscard]] std::vector<Instruction> parse(const std::string& id) const;
    [[nodiscard]] std::shared_ptr<const Program> compile(const std::string& id, char variant) const;
    [[nodiscard]] std::string resolve(const std::string& current, const std::string& target) const;
    [[nodiscard]] const std::map<std::string, std::string>& entries() const { return entries_; }
    [[nodiscard]] nlohmann::json audit() const;
    [[nodiscard]] nlohmann::json audit_resources() const;
private:
    void expand(const std::string& id, char variant, std::set<std::string>& stack, std::vector<Instruction>& out) const;
    const Content& content_;
    bool legacy_sc_{};
    LegacyParser parser_;
    std::map<std::string, std::string> entries_;
};
enum class ScriptState { running, message, waiting, choices, ended };
struct Message {
    std::string script, voice, name, text;
    int id{};
    std::size_t source_line{};
    bool plain_text{};
};
struct Choice { std::string text, label; };
struct ScriptOptions { char variant{'e'}; bool text_preview{}; std::size_t start_line{}; double characters_per_second{}; };
struct ScriptHost {
    std::function<bool(const Instruction&)> command;
    std::function<void(const Message&)> message;
    std::function<void(double)> tick;
    std::function<bool()> busy;
    std::function<bool()> voice_playing{};
    std::function<void(const std::string&, std::int64_t)> local_assignment{};
    std::function<void(const Instruction&)> observe{};
};
class ScriptRuntime {
    friend struct CheckpointCodec;
public:
    using Loader = std::function<std::shared_ptr<const Program>(const std::string&)>;
    using Resolver = std::function<std::string(const std::string&, const std::string&)>;
    ScriptRuntime(Loader loader, Resolver resolver, std::string entry, ScriptOptions options = {});
    void update(double seconds);
    void advance();
    void choose(std::size_t index);
    void set_host(ScriptHost host) { host_ = std::move(host); }
    [[nodiscard]] ScriptState state() const { return state_; }
    [[nodiscard]] const std::optional<Message>& message() const { return message_; }
    [[nodiscard]] std::string displayed_text() const;
    [[nodiscard]] std::string full_message_text() const;
    [[nodiscard]] bool message_automatic() const { return dialogue_ && dialogue_->automatic(); }
    [[nodiscard]] bool message_complete() const { return !dialogue_ || dialogue_->complete(); }
    [[nodiscard]] std::uint64_t message_serial() const { return message_serial_; }
    [[nodiscard]] bool controls_enabled() const { return controls_enabled_; }
    [[nodiscard]] const std::vector<Choice>& choices() const { return choices_; }
    [[nodiscard]] const std::vector<std::string>& diagnostics() const { return diagnostics_; }
    [[nodiscard]] std::int64_t variable(const std::string& name) const;
    // Host clocks may update an existing local without recursively notifying assignments.
    void update_local_variable(const std::string& name, std::int64_t value);
    // Chapter bootstrap before the first instruction; rejects an already used VM.
    void initialize_globals(const std::map<std::string,std::int64_t>&);
    // Read-only diagnostics for offline route coverage, not a persistent save format.
    [[nodiscard]] nlohmann::json inspect() const;
private:
    void execute(const Instruction& instruction);
    void jump(const std::string& label);
    void diagnostic(const Instruction& instruction, const std::string& message);
    Loader loader_;
    Resolver resolver_;
    ScriptOptions options_;
    ScriptHost host_;
    std::shared_ptr<const Program> program_;
    std::size_t pc_{};
    ScriptState state_{ScriptState::running};
    double wait_remaining_{};
    std::optional<Message> message_;
    std::optional<Dialogue> dialogue_;
    std::uint64_t message_serial_{};
    bool controls_enabled_{true},message_plain_{};
    std::vector<Choice> choices_;
    std::map<std::string, std::int64_t> variables_, globals_;
    std::set<std::string> reported_;
    std::vector<std::string> diagnostics_;
};
std::string message_display_text(const std::string& text);
}
