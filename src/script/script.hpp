#pragma once
#include "data/content.hpp"
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
    [[nodiscard]] std::string location() const;
};
std::vector<Instruction> parse_script(std::string_view text, const std::string& id);
bool core_command(const std::string& name);
struct Program {
    std::string id;
    std::vector<Instruction> instructions;
    std::map<std::string, std::size_t> labels;
};
class ScriptLibrary {
public:
    explicit ScriptLibrary(const Content& content);
    [[nodiscard]] std::vector<Instruction> parse(const std::string& id) const;
    [[nodiscard]] std::shared_ptr<const Program> compile(const std::string& id, char variant) const;
    [[nodiscard]] std::string resolve(const std::string& current, const std::string& target) const;
    [[nodiscard]] const std::map<std::string, std::string>& entries() const { return entries_; }
    [[nodiscard]] nlohmann::json audit() const;
    [[nodiscard]] nlohmann::json audit_resources() const;
private:
    void expand(const std::string& id, char variant, std::set<std::string>& stack, std::vector<Instruction>& out) const;
    const Content& content_;
    std::map<std::string, std::string> entries_;
};
enum class ScriptState { running, message, waiting, choices, ended };
struct Message {
    std::string script, voice, name, text;
    int id{};
    std::size_t source_line{};
};
struct Choice { std::string text, label; };
struct ScriptOptions { char variant{'e'}; bool text_preview{}; std::size_t start_line{}; };
struct ScriptHost {
    std::function<bool(const Instruction&)> command;
    std::function<void(const Message&)> message;
    std::function<void(double)> tick;
    std::function<bool()> busy;
};
class ScriptRuntime {
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
    [[nodiscard]] const std::vector<Choice>& choices() const { return choices_; }
    [[nodiscard]] const std::vector<std::string>& diagnostics() const { return diagnostics_; }
    [[nodiscard]] std::int64_t variable(const std::string& name) const;
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
    std::vector<Choice> choices_;
    std::map<std::string, std::int64_t> variables_, globals_;
    std::set<std::string> reported_;
    std::vector<std::string> diagnostics_;
};
std::string message_display_text(const std::string& text);
}
