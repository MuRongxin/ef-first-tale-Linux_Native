#include "app/application.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    try {
        ef::Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            const auto value = [&]() -> std::string_view {
                if (++i == argc) throw std::runtime_error("Missing value for " + std::string(arg));
                return argv[i];
            };
            const auto integer = [&](int minimum, int maximum) {
                const auto text = value();
                int result{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < minimum || result > maximum)
                    throw std::runtime_error("Invalid value for " + std::string(arg));
                return result;
            };
            if (arg == "--help" || arg == "-h") {
                std::cout << "ef_native --content content [options]\n"
                             "  --validate-content    Decode every declared image and validate scene/font without a window\n"
                             "  --audit-scripts       Parse/compile all imported scripts (both e/j variants), print JSON\n"
                             "  --script ID           Run an imported script, e.g. scr/test.sc (strict by default)\n"
                             "  --variant e|j         Explicit timing variant; default e is a development choice\n"
                             "  --text-preview        Proofreading only: report and skip unimplemented commands\n"
                             "  --trace-messages N    Headless script check, require N messages; stops at choices\n"
                             "  --width N --height N  Initial window size (320..16384 / 240..16384)\n"
                             "  --maximized           Start maximized (8x)\n"
                             "  --fullscreen          Start in desktop/borderless fullscreen (8x)\n"
                             "  --frames N            Exit after N rendered frames\n"
                             "  --smoke               Check normal -> borderless -> restored tier transitions\n"
                             "  --screenshot FILE     Write final frame as PPM (requires --frames or --smoke)\n"
                             "Keys: F11 borderless, F10 maximize/restore, R reload content, Esc leave fullscreen/exit.\n";
                return 0;
            }
            if (arg == "--content") options.content = value();
            else if (arg == "--script") options.script = value();
            else if (arg == "--variant") {
                const auto variant = value();
                if (variant != "e" && variant != "j") throw std::runtime_error("--variant must be e or j");
                options.variant = variant.front();
            }
            else if (arg == "--text-preview") options.text_preview = true;
            else if (arg == "--audit-scripts") options.audit_scripts = true;
            else if (arg == "--trace-messages") options.trace_messages = integer(1, 100000);
            else if (arg == "--screenshot") options.screenshot = value();
            else if (arg == "--width") options.width = integer(320, 16384);
            else if (arg == "--height") options.height = integer(240, 16384);
            else if (arg == "--frames") options.frames = integer(1, 1000000);
            else if (arg == "--maximized") options.maximized = true;
            else if (arg == "--fullscreen") options.fullscreen = true;
            else if (arg == "--validate-content") options.validate = true;
            else if (arg == "--smoke") options.smoke = true;
            else throw std::runtime_error("Unknown option: " + std::string(arg));
        }
        if (options.smoke && (options.maximized || options.fullscreen || options.frames || options.validate))
            throw std::runtime_error("--smoke requires a normal window and cannot combine with --frames/--validate-content");
        if (!options.screenshot.empty() && (!(options.frames || options.smoke) || options.validate))
            throw std::runtime_error("--screenshot requires --frames or --smoke");
        if ((options.text_preview || options.trace_messages) && options.script.empty()) throw std::runtime_error("Script options require --script ID");
        if ((!options.script.empty() && (options.smoke || options.validate || options.audit_scripts)) ||
            (options.trace_messages && (options.frames || !options.screenshot.empty())))
            throw std::runtime_error("Conflicting script/validation/window smoke options");
        return ef::run(options);
    } catch (const std::exception& e) {
        std::cerr << "ef_native: " << e.what() << '\n';
        return 1;
    }
}
