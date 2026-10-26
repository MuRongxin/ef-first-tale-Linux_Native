// Offline compatibility parser. Deliberately not linked into ef_native.
#include "script/script.hpp"
#include "core/utf8.hpp"
#include <sstream>
#include <stdexcept>
namespace ef {
namespace {
std::vector<std::string> words(const std::string& text) {
    std::istringstream stream(text);
    std::vector<std::string> result;
    for (std::string value; stream >> value;) result.push_back(std::move(value));
    return result;
}
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
        // The native loader drops these unmatched production-note tokens after stripping [e]/[j].
        // Limit compatibility to the full-width semicolon; unknown command text remains strict.
        std::string_view note=std::string_view(raw).substr(first);
        if (note.starts_with("[e]") || note.starts_with("[j]")) note.remove_prefix(3);
        if (note.starts_with("；")) continue;
        Instruction instruction{id, {}, raw, line_number, 0, {}};
        std::string line = raw.substr(first);
        if (line.starts_with("[e]:.") || line.starts_with("[j]:.")) {
            instruction.variant = line[1];
            line.erase(0,4); // explicitly supported archive spelling; raw/physical line are retained
        } else if (line.starts_with("[e].") || line.starts_with("[j].")) {
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
        try { validate_instruction(instruction); }
        catch (const std::exception& e) { throw std::runtime_error(instruction.location() + ": " + e.what()); }
        output.push_back(std::move(instruction));
    }
    return output;
}
}
