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
void count(const Instruction& instruction, std::size_t expected) {
    if (instruction.args.size() != expected) throw std::runtime_error("Expected " + std::to_string(expected) + " arguments");
}
void validate_values(const Instruction& instruction) {
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
void validate_instruction(const Instruction& instruction) {validate_values(instruction);}
std::string Instruction::location() const { return script + ':' + std::to_string(line) + ' ' + command; }
bool core_command(const std::string& name) {
    static const std::set<std::string> supported{".message", ".wait", ".label", ".goto", ".if", ".select", ".set", ".setGlobal", ".chain", ".end", ".pragma", "#include"};
    return supported.contains(name);
}
ScriptLibrary::ScriptLibrary(const Content& content,const std::string& index_path,LegacyParser legacy_parser) : content_(content),legacy_sc_(static_cast<bool>(legacy_parser)),parser_(legacy_parser ? std::move(legacy_parser) : read_script_json) {
    const auto index = content.read_json(index_path.empty() ? content.script_index() : index_path);
    if (!index.at("version").is_number_integer() || index.at("version") != (legacy_sc_ ? 1 : 2) || (!legacy_sc_ && index.value("format","")!="ef.script.v1") || index.at("encoding") != "utf-8" || !index.at("scripts").is_object())
        throw std::runtime_error("Expected JSON script index v2; migrate the content package with tools/migrate_scripts.py");
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
    try {
        auto instructions=parser_(text,id);
        for (const auto& i:instructions) if (!i.unsupported) validate_instruction(i);
        return instructions;
    } catch (const std::exception& e) {throw std::runtime_error(id+": "+e.what());}
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
    nlohmann::json report{{"support_scope", "core_control_flow; scene/audio capabilities depend on runtime host and arguments"}, {"scripts", entries_.size()}, {"instructions", 0}, {"messages", 0}, {"commands", nlohmann::json::object()}, {"unsupported", nlohmann::json::object()}};
    std::size_t instructions = 0, messages = 0;
    auto preserved = nlohmann::json::array();
    std::map<std::string, std::size_t> commands, unsupported;
    for (const auto& [id, path] : entries_) {
        (void)path;
        for (const auto& instruction : parse(id)) {
            ++instructions;
            ++commands[instruction.command];
            if (instruction.unsupported) preserved.push_back(instruction.location());
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
    report["preserved_unsupported"] = preserved;
    return report;
}
nlohmann::json ScriptLibrary::audit_resources() const {
    if (!content_.has_resource_catalog()) throw std::runtime_error("No resource catalog; run tools/prepare_content.py --all --game-root PATH");
    nlohmann::json issues = nlohmann::json::array(), unavailable = nlohmann::json::array();
    std::map<std::string, std::size_t> counts;
    for (const auto& [id, resource] : content_.resources()) {
        ++counts[resource.kind];
        const auto check = [&](const std::string& path) {
            if (!std::filesystem::is_regular_file(content_.resolve(path))) unavailable.push_back({{"id", id}, {"path", path}});
        };
        if (resource.kind == "image") {
            for (const auto& [tier, variant] : content_.asset(id).variants) { (void)tier; check(variant.path); }
        } else check(resource.path);
    }
    std::size_t references{}, missing{}, ambiguous{};
    std::set<std::pair<std::string, std::string>> unique;
    for (const auto& [id, path] : entries_) {
        (void)path;
        for (const auto& instruction : parse(id)) {
            const auto& cmd = instruction.command;
            const auto& args = instruction.args;
            if (cmd == "<unparsed>") continue;
            std::vector<std::pair<std::string, std::string>> refs;
            if (cmd == ".message") refs.emplace_back("voice", args[1]);
            else if (cmd == ".playBGM" || cmd == ".playSE" || cmd == ".playSE2" || cmd == ".playVoice" || cmd == ".chain" || cmd == "#include") {
                if (args.empty()) throw std::runtime_error(instruction.location() + ": missing resource argument");
                const std::string kind = cmd == ".playBGM" ? "bgm" :
                    (cmd == ".playSE" || cmd == ".playSE2") ? "se" : cmd == ".playVoice" ? "voice" : "script";
                refs.emplace_back(kind, args[0]);
            } else if (cmd == ".effect" && !args.empty() && (args[0] == "Snow" || args[0] == "SnowH")) {
                for (const auto* sprite : {"snowS.png", "snowM.png", "snowL.png"}) refs.emplace_back("image",sprite);
            } else if (cmd == ".movie") {
                if (args.size() < 2) throw std::runtime_error(instruction.location() + ": missing movie filename");
                refs.emplace_back("video", args[1]);
            } else {
                for (const auto& arg : args) {
                    if (arg == "*" || arg.empty()) continue;
                    std::string name = arg;
                    for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
                    if (name.ends_with(".png") || name.ends_with(".png-") || name.ends_with(".png.tmp")) refs.emplace_back("image", arg);
                    else if (name.ends_with(".ani")) refs.emplace_back("animation", arg);
                }
            }
            for (const auto& [kind, name] : refs) {
                if (name.empty() || name == "*") continue;
                ++references;
                unique.emplace(kind, resource_name(name));
                const auto match = content_.lookup_resource(kind, name, id.starts_with("r18_scr/"));
                if (match.status == ResourceStatus::resolved) continue;
                const bool absent = match.status == ResourceStatus::missing;
                if (absent) ++missing; else ++ambiguous;
                issues.push_back({{"script", id}, {"line", instruction.line}, {"variant", instruction.variant ? std::string(1, instruction.variant) : ""},
                    {"command", cmd}, {"kind", kind}, {"name", name}, {"status", absent ? "missing" : "ambiguous"}, {"candidates", match.candidates}});
            }
        }
    }
    return {{"scripts", entries_.size()}, {"references", references}, {"unique_references", unique.size()},
            {"missing", missing}, {"ambiguous", ambiguous}, {"issues", issues}, {"counts", counts}, {"unavailable_files", unavailable}};
}
ScriptRuntime::ScriptRuntime(Loader loader, Resolver resolver, std::string entry, ScriptOptions options)
    : loader_(std::move(loader)), resolver_(std::move(resolver)), options_(options), program_(loader_(entry)) {
    if (!program_) throw std::runtime_error("Script loader returned no program");
    if (options_.start_line) {
        const auto it = std::find_if(program_->instructions.begin(), program_->instructions.end(), [&](const Instruction& i) {
            return i.script == entry && i.line >= options_.start_line;
        });
        if (it == program_->instructions.end()) throw std::runtime_error("No entry instruction at or after requested start line");
        pc_ = static_cast<std::size_t>(it-program_->instructions.begin());
    }
}
void ScriptRuntime::initialize_globals(const std::map<std::string,std::int64_t>& values) {
    if (pc_ || message_serial_ || state_!=ScriptState::running || !globals_.empty() || !variables_.empty())
        throw std::runtime_error("Session bootstrap requires a fresh runtime");
    globals_=values;
}
void ScriptRuntime::update_local_variable(const std::string& name, std::int64_t value) {
    if (auto it=variables_.find(name); it!=variables_.end()) it->second=value;
}
nlohmann::json ScriptRuntime::inspect() const {
    return {{"program",program_->id},{"next_instruction",pc_},{"locals",variables_},{"globals",globals_}};
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
    const bool voice_before = host_.voice_playing && host_.voice_playing();
    if (host_.tick) host_.tick(seconds);
    if (state_ == ScriptState::message && dialogue_) {
        dialogue_->update(seconds,voice_before);
        const bool voice_now = host_.voice_playing && host_.voice_playing();
        if (dialogue_->complete() && (dialogue_->automatic() || (!controls_enabled_ && !voice_now)))
            state_ = ScriptState::running;
    }
    if (state_ == ScriptState::waiting) {
        wait_remaining_ = std::max(0.0, wait_remaining_ - seconds);
        if (wait_remaining_ > 1e-9) return;
        state_ = ScriptState::running;
    }
    for (int budget = 10000; state_ == ScriptState::running; --budget) {
        if (host_.busy && host_.busy()) break;
        if (budget == 0) throw std::runtime_error(program_->id + ": instruction budget exhausted (possible infinite loop)");
        if (pc_ == program_->instructions.size()) { state_ = ScriptState::ended; break; }
        const auto instruction = program_->instructions.at(pc_++);
        try {
            if (instruction.colon_variant || instruction.raw.find("[e]:.") != std::string::npos || instruction.raw.find("[j]:.") != std::string::npos)
                diagnostic(instruction,"compatibility: colon after timing variant");
            if (host_.observe) host_.observe(instruction);
            execute(instruction);
        }
        catch (const std::exception& e) { throw std::runtime_error(instruction.location() + ": " + e.what()); }
    }
}
void ScriptRuntime::advance() {
    if (state_ != ScriptState::message || (!options_.text_preview && !controls_enabled_)) return;
    if (dialogue_ && !dialogue_->complete()) {
        dialogue_->update(0,host_.voice_playing && host_.voice_playing());
        dialogue_->reveal();
        return;
    }
    state_ = ScriptState::running;
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
    if (instruction.unsupported) {
        if (!options_.text_preview) throw std::runtime_error("Unsupported preserved JSON instruction");
        diagnostic(instruction,"text-preview: unsupported preserved instruction");return;
    }
    if (cmd == ".message") {
        if (!options_.text_preview && !args[1].empty() && !host_.message)
            throw std::runtime_error("Voice output requires a script host; use --text-preview for proofreading");
        std::optional<Dialogue> text;
        if (!options_.text_preview) {
            if (instruction.events) text.emplace(*instruction.events,options_.characters_per_second);
            else text.emplace(args[3],options_.characters_per_second);
        }
        message_plain_=instruction.events.has_value();
        if (text && text->waits_for_voice() && !host_.voice_playing)
            throw std::runtime_error("Voice wait requires a voice-state callback");
        if (options_.text_preview && !args[1].empty()) diagnostic(instruction, "text-preview: message voice is not played");
        const bool timing=instruction.events ? std::any_of(instruction.events->begin(),instruction.events->end(),[](const DialogueEvent& e) {return e.kind!=DialogueKind::text;}) :
            args[3].find("\\w")!=std::string::npos || args[3].find("\\v")!=std::string::npos || args[3].find("\\a")!=std::string::npos;
        if (options_.text_preview && timing)
            diagnostic(instruction, "text-preview: timing escapes are displayed without original timing");
        message_ = Message{instruction.script, args[1], args[2], args[3], static_cast<int>(integer(args[0])), instruction.line, instruction.events.has_value()};
        if (!options_.text_preview && host_.message) host_.message(*message_);
        dialogue_ = std::move(text);
        if (dialogue_) dialogue_->update(0,host_.voice_playing && host_.voice_playing());
        ++message_serial_;
        state_ = ScriptState::message;
    } else if (cmd == ".pragma") {
        count(instruction,1);
        if (args[0] != "enable_control" && args[0] != "disable_control") throw std::runtime_error("Unsupported pragma: " + args[0]);
        if (!options_.text_preview) controls_enabled_ = args[0] == "enable_control";
    } else if (cmd == ".wait") {
        wait_remaining_ = static_cast<double>(integer(args[0])) / 100.0;
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
        if (cmd == ".setGlobal") { if (host_.global_assignment) host_.global_assignment(args[0],value); globals_[args[0]] = value; variables_.erase(args[0]); }
        else {
            if (host_.local_assignment) host_.local_assignment(args[0],value);
            variables_[args[0]] = value;
        }
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
    else if (host_.command && !options_.text_preview && host_.command(instruction)) { /* host owns the command and any asynchronous wait */ }
    else if (options_.text_preview) diagnostic(instruction, "text-preview: unimplemented command " + cmd);
    else throw std::runtime_error("Unimplemented command (strict mode): " + cmd);
}
std::string ScriptRuntime::full_message_text() const {
    return dialogue_ ? dialogue_->full_text() : message_ ? (message_plain_ ? message_->text : message_display_text(message_->text)) : "";
}
std::string ScriptRuntime::displayed_text() const {
    return dialogue_ ? dialogue_->text() : message_ ? (message_plain_ ? message_->text : message_display_text(message_->text)) : "";
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
