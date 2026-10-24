#include "script/script.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ef {
namespace {
std::int64_t integer(const std::string& value) {
    std::int64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) throw std::runtime_error("Expected integer: " + value);
    return result;
}
std::vector<std::string> words(const std::string& text) {
    std::istringstream stream(text);
    std::vector<std::string> result;
    for (std::string value; stream >> value;) result.push_back(std::move(value));
    return result;
}
void count(const Instruction& instruction, std::size_t expected) {
    if (instruction.args.size() != expected) throw std::runtime_error("Expected " + std::to_string(expected) + " arguments");
}
void validate(const Instruction& instruction) {
    const auto& cmd = instruction.command;
    const auto& args = instruction.args;
    if (cmd == ".message") {
        count(instruction, 4);
        const auto id = integer(args[0]);
        if (id < 0 || id > 65535) throw std::runtime_error("Message L0 must fit the read bitmap (0..65535)");
    } else if (cmd == ".wait") {
        count(instruction, 1);
        if (integer(args[0]) < 0) throw std::runtime_error("Negative wait duration");
    } else if (cmd == ".label" || cmd == ".goto" || cmd == ".chain" || cmd == "#include") count(instruction, 1);
    else if (cmd == ".end") count(instruction, 0);
    else if (cmd == ".if") {
        count(instruction, 4);
        if (args[1] != "==" && args[1] != "!=" && args[1] != "<" && args[1] != "<=" && args[1] != ">" && args[1] != ">=")
            throw std::runtime_error("Unsupported comparison operator");
        (void)integer(args[2]);
    } else if (cmd == ".set" || cmd == ".setGlobal") {
        if ((args.size() != 3 && args.size() != 5) || args[1] != "=") throw std::runtime_error("Unsupported assignment syntax");
        if (args.size() == 3) (void)integer(args[2]);
        else {
            if (args[3] != "+" && args[3] != "-") throw std::runtime_error("Unsupported arithmetic operator");
            (void)integer(args[4]);
        }
    } else if (cmd == ".select") {
        if (args.size() < 2) throw std::runtime_error("Selection requires at least two options");
        for (const auto& arg : args) {
            const auto separator = arg.rfind(':');
            if (separator == std::string::npos || separator == 0 || separator + 1 == arg.size()) throw std::runtime_error("Expected choice text:label");
        }
    }
}
}
std::string Instruction::location() const { return script + ':' + std::to_string(line) + ' ' + command; }
bool core_command(const std::string& name) {
    static const std::set<std::string> supported{".message", ".wait", ".label", ".goto", ".if", ".select", ".set", ".setGlobal", ".chain", ".end", "#include"};
    return supported.contains(name);
}
std::vector<Instruction> parse_script(std::string_view text, const std::string& id) {
    (void)decode_utf8(text);
    if (text.find('\0') != std::string_view::npos) throw std::runtime_error(id + ": NUL in script");
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    std::istringstream stream{std::string(text)};
    std::vector<Instruction> output;
    std::size_t line_number{};
    for (std::string raw; std::getline(stream, raw);) {
        ++line_number;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        Instruction instruction{id, {}, raw, line_number, 0, {}};
        std::string line = raw.substr(first);
        if (line.starts_with("[e].") || line.starts_with("[j].")) {
            instruction.variant = line[1];
            line.erase(0, 3);
        }
        const auto separator = line.find_first_of(" \t");
        instruction.command = line.substr(0, separator);
        if (line.front() != '.' && !line.starts_with("#include")) instruction.command = "<unparsed>";
        const auto arguments = separator == std::string::npos ? std::string{} : line.substr(separator + 1);
        if (instruction.command == ".message") {
            // The first three ASCII spaces delimit id, voice and name; empty fields matter.
            std::size_t begin = 0;
            for (int i = 0; i < 3; ++i) {
                const auto end = arguments.find(' ', begin);
                if (end == std::string::npos) throw std::runtime_error(instruction.location() + ": message requires id voice name text (empty fields allowed)");
                instruction.args.push_back(arguments.substr(begin, end - begin));
                begin = end + 1;
            }
            instruction.args.push_back(arguments.substr(begin));
        } else instruction.args = words(arguments);
        try { validate(instruction); }
        catch (const std::exception& e) { throw std::runtime_error(instruction.location() + ": " + e.what()); }
        output.push_back(std::move(instruction));
    }
    return output;
}
ScriptLibrary::ScriptLibrary(const Content& content) : content_(content) {
    const auto index = content.read_json("scripts/index.json");
    if (index.at("version") != 1 || index.at("encoding") != "utf-8" || !index.at("scripts").is_object())
        throw std::runtime_error("Unsupported script index");
    for (const auto& [id, entry] : index.at("scripts").items()) {
        if (id.empty() || id.find("..") != std::string::npos || id.find('\\') != std::string::npos || id.front() == '/')
            throw std::runtime_error("Invalid script ID: " + id);
        const auto path = entry.at("path").get<std::string>();
        (void)content.resolve(path);
        entries_.emplace(id, path);
    }
}
std::vector<Instruction> ScriptLibrary::parse(const std::string& id) const {
    const auto it = entries_.find(id);
    if (it == entries_.end()) throw std::runtime_error("Unknown script: " + id);
    const auto path = content_.resolve(it->second);
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024) throw std::runtime_error("Script exceeds 16 MiB: " + id);
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read script: " + id);
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return parse_script(text, id);
}
std::string ScriptLibrary::resolve(const std::string& current, const std::string& target) const {
    if (target.find('/') != std::string::npos || target.find('\\') != std::string::npos || target.find("..") != std::string::npos)
        throw std::runtime_error("Script references must use a local filename: " + target);
    const auto prefix = current.substr(0, current.find('/') + 1);
    if (entries_.contains(prefix + target)) return prefix + target;
    if (prefix == "r18_scr/" && entries_.contains("scr/" + target)) return "scr/" + target;
    throw std::runtime_error(current + ": missing script reference " + target);
}
void ScriptLibrary::expand(const std::string& id, char variant, std::set<std::string>& stack, std::vector<Instruction>& out) const {
    if (stack.size() >= 16 || !stack.insert(id).second) throw std::runtime_error("Include cycle/depth limit at " + id);
    for (auto& instruction : parse(id)) {
        if (instruction.variant && instruction.variant != variant) continue;
        if (instruction.command == "#include") expand(resolve(id, instruction.args[0]), variant, stack, out);
        else out.push_back(std::move(instruction));
        if (out.size() > 200000) throw std::runtime_error("Expanded script exceeds instruction budget");
    }
    stack.erase(id);
}
std::shared_ptr<const Program> ScriptLibrary::compile(const std::string& id, char variant) const {
    if (variant != 'e' && variant != 'j') throw std::runtime_error("Script variant must be e or j");
    auto program = std::make_shared<Program>();
    program->id = id;
    std::set<std::string> stack;
    expand(id, variant, stack, program->instructions);
    for (std::size_t i = 0; i < program->instructions.size(); ++i) {
        const auto& instruction = program->instructions[i];
        if (instruction.command == ".label" && !program->labels.emplace(instruction.args[0], i).second)
            throw std::runtime_error(instruction.location() + ": duplicate label");
    }
    for (const auto& instruction : program->instructions) {
        const auto require_label = [&](const std::string& label) {
            if (!program->labels.contains(label)) throw std::runtime_error(instruction.location() + ": missing label " + label);
        };
        if (instruction.command == ".goto") require_label(instruction.args[0]);
        else if (instruction.command == ".if") require_label(instruction.args[3]);
        else if (instruction.command == ".select") {
            for (const auto& arg : instruction.args) require_label(arg.substr(arg.rfind(':') + 1));
        } else if (instruction.command == ".chain") (void)resolve(instruction.script, instruction.args[0]);
    }
    return program;
}
nlohmann::json ScriptLibrary::audit() const {
    nlohmann::json report{{"scripts", entries_.size()}, {"instructions", 0}, {"messages", 0}, {"commands", nlohmann::json::object()}, {"unsupported", nlohmann::json::object()}};
    std::size_t instructions = 0, messages = 0;
    std::map<std::string, std::size_t> commands, unsupported;
    for (const auto& [id, path] : entries_) {
        (void)path;
        for (const auto& instruction : parse(id)) {
            ++instructions;
            ++commands[instruction.command];
            if (instruction.command == ".message") ++messages;
            if (!core_command(instruction.command)) ++unsupported[instruction.command];
        }
        (void)compile(id, 'e');
        (void)compile(id, 'j');
    }
    report["instructions"] = instructions;
    report["messages"] = messages;
    report["commands"] = commands;
    report["unsupported"] = unsupported;
    return report;
}
ScriptRuntime::ScriptRuntime(Loader loader, Resolver resolver, std::string entry, ScriptOptions options)
    : loader_(std::move(loader)), resolver_(std::move(resolver)), options_(options), program_(loader_(entry)) {
    if (!program_) throw std::runtime_error("Script loader returned no program");
}
std::int64_t ScriptRuntime::variable(const std::string& name) const {
    if (const auto it = variables_.find(name); it != variables_.end()) return it->second;
    if (const auto it = globals_.find(name); it != globals_.end()) return it->second;
    return 0; // new-session default, persistence will be provided by the save layer
}
void ScriptRuntime::diagnostic(const Instruction& instruction, const std::string& message) {
    if (reported_.insert(message).second) diagnostics_.push_back(instruction.location() + ": " + message);
}
void ScriptRuntime::jump(const std::string& label) {
    const auto it = program_->labels.find(label);
    if (it == program_->labels.end()) throw std::runtime_error("Unknown label: " + label);
    pc_ = it->second + 1;
}
void ScriptRuntime::update(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid script delta time");
    if (state_ == ScriptState::waiting) {
        wait_remaining_ = std::max(0.0, wait_remaining_ - seconds);
        if (wait_remaining_ > 1e-9) return;
        state_ = ScriptState::running;
    }
    for (int budget = 10000; state_ == ScriptState::running; --budget) {
        if (budget == 0) throw std::runtime_error(program_->id + ": instruction budget exhausted (possible infinite loop)");
        if (pc_ == program_->instructions.size()) { state_ = ScriptState::ended; break; }
        const auto instruction = program_->instructions.at(pc_++);
        try { execute(instruction); }
        catch (const std::exception& e) { throw std::runtime_error(instruction.location() + ": " + e.what()); }
    }
}
void ScriptRuntime::advance() {
    if (state_ == ScriptState::message) state_ = ScriptState::running;
}
void ScriptRuntime::choose(std::size_t index) {
    if (state_ != ScriptState::choices || index >= choices_.size()) throw std::runtime_error("Invalid choice index/state");
    jump(choices_[index].label);
    choices_.clear();
    state_ = ScriptState::running;
}
void ScriptRuntime::execute(const Instruction& instruction) {
    const auto& cmd = instruction.command;
    const auto& args = instruction.args;
    if (cmd == ".message") {
        if (!options_.text_preview && (!args[1].empty() || args[3].find("\\w") != std::string::npos || args[3].find("\\v") != std::string::npos || args[3].find("\\a") != std::string::npos))
            throw std::runtime_error("Voice/text timing is not implemented; use explicit --text-preview for proofreading");
        if (!args[1].empty()) diagnostic(instruction, "text-preview: message voice is not played");
        if (args[3].find("\\w") != std::string::npos || args[3].find("\\v") != std::string::npos || args[3].find("\\a") != std::string::npos)
            diagnostic(instruction, "text-preview: timing escapes are displayed without original timing");
        message_ = Message{instruction.script, args[1], args[2], args[3], static_cast<int>(integer(args[0])), instruction.line};
        state_ = ScriptState::message;
    } else if (cmd == ".wait") {
        wait_remaining_ = static_cast<double>(integer(args[0])) / 1000.0;
        if (wait_remaining_ > 0) state_ = ScriptState::waiting;
    } else if (cmd == ".label") { /* indexed at compile time */ }
    else if (cmd == ".goto") jump(args[0]);
    else if (cmd == ".if") {
        const auto left = variable(args[0]), right = integer(args[2]);
        const bool taken = args[1] == "==" ? left == right : args[1] == "!=" ? left != right : args[1] == "<" ? left < right :
            args[1] == "<=" ? left <= right : args[1] == ">" ? left > right : left >= right;
        if (taken) jump(args[3]);
    } else if (cmd == ".set" || cmd == ".setGlobal") {
        std::int64_t value = integer(args.back());
        if (args.size() == 5) {
            const auto base = variable(args[2]);
            if (args[3] == "-") {
                if (value == std::numeric_limits<std::int64_t>::min()) throw std::runtime_error("Arithmetic overflow");
                value = -value;
            }
            if ((value > 0 && base > std::numeric_limits<std::int64_t>::max() - value) ||
                (value < 0 && base < std::numeric_limits<std::int64_t>::min() - value)) throw std::runtime_error("Arithmetic overflow");
            value += base;
        }
        if (cmd == ".setGlobal") { globals_[args[0]] = value; variables_.erase(args[0]); }
        else variables_[args[0]] = value;
    } else if (cmd == ".select") {
        choices_.clear();
        for (const auto& arg : args) {
            const auto separator = arg.rfind(':');
            choices_.push_back({arg.substr(0, separator), arg.substr(separator + 1)});
        }
        state_ = ScriptState::choices;
    } else if (cmd == ".chain") {
        const auto next = resolver_(instruction.script, args[0]);
        auto replacement = loader_(next);
        if (!replacement) throw std::runtime_error("Script loader returned no program");
        program_ = std::move(replacement);
        pc_ = 0;
        // Local variables survive .chain; route counters span multiple script files.
    } else if (cmd == ".end") state_ = ScriptState::ended;
    else if (options_.text_preview) diagnostic(instruction, "text-preview: unimplemented command " + cmd);
    else throw std::runtime_error("Unimplemented command (strict mode): " + cmd);
}
std::string message_display_text(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            if (text[i + 1] == 'n') { result += '\n'; ++i; continue; }
            if (text[i + 1] == 'w' || text[i + 1] == 'v' || text[i + 1] == 'a') { ++i; continue; }
        }
        result += text[i];
    }
    return result;
}
}
