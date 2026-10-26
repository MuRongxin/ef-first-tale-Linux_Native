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
                             "  --audit-resources     Check all script image/audio/video references and package files; exit 2 for unresolved references\n"
                             "  --script ID           Run an imported script, e.g. scr/test.sc (strict by default)\n"
                             "  --variant e|j         Explicit timing variant; default e is a development choice\n"
                             "  --start-line N        Development entry at source line; does not restore preceding state\n"
                             "  --text-speed N        Development text speed, 0=instant, default 40 characters/second\n"
                             "  --font FILE           Custom TTF/OTF/TTC; saved after successful window startup\n"
                             "  --font-face N         Face index in a font collection (default 0)\n"
                             "  --font-size N         Font size percentage (75..150), auto-fit to message regions\n"
                             "  --text-outline N      Black outline width in design pixels (0..4)\n"
                             "  --text-shadow N       Black shadow diagonal offset (0..8)\n"
                             "  --text-baseline N     Glyph baseline offset (-8..8); wrapping remains unchanged\n"
                             "  --font-default        Restore packaged font, default size and text appearance\n"
                             "  --font-config FILE    Override user font configuration path\n"
                             "  --silent              Advance audio silently without opening a device\n"
                             "  --text-preview        Proofreading only: report and skip unimplemented commands\n"
                             "  --trace-messages N    Headless script check, require N messages; stops at choices\n"
                             "  --width N --height N  Initial window size (320..16384 / 240..16384);\n"
                             "                        default auto: largest 4:3 tier fitting the display\n"
                             "  --window-config FILE  Override remembered window geometry path\n"
                             "  --maximized           Start maximized (8x)\n"
                             "  --fullscreen          Start in desktop/borderless fullscreen (8x)\n"
                             "  --frames N            Exit after N rendered frames\n"
                             "  --smoke               Check normal -> borderless -> restored tier transitions\n"
                             "  --screenshot FILE     Write final frame as PPM (requires --frames or --smoke)\n"
                             "Keys: drop font file to preview; F5 text appearance; F6 next collection face; F7/F8 smaller/larger; F9 default.\n"
                             "      F11 borderless, F10 maximize/restore, R reload content, Esc leave fullscreen/exit.\n";
                return 0;
            }
            if (arg == "--content") options.content = value();
            else if (arg == "--script") options.script = value();
            else if (arg == "--variant") {
                const auto variant = value();
                if (variant != "e" && variant != "j") throw std::runtime_error("--variant must be e or j");
                options.variant = variant.front();
            }
            else if (arg == "--start-line") options.start_line = integer(1, 1000000);
            else if (arg == "--text-speed") options.text_speed = integer(0,1000);
            else if (arg == "--font") options.font_file = std::filesystem::absolute(value());
            else if (arg == "--font-face") options.font_face = integer(0,65535);
            else if (arg == "--font-size") options.font_percent = integer(75,150);
            else if (arg == "--font-config") options.font_config = value();
            else if (arg == "--text-outline") options.text_outline = integer(0,4);
            else if (arg == "--text-shadow") options.text_shadow = integer(0,8);
            else if (arg == "--text-baseline") options.text_baseline = integer(-8,8);
            else if (arg == "--font-default") options.default_font = true;
            else if (arg == "--silent") options.silent = true;
            else if (arg == "--text-preview") options.text_preview = true;
            else if (arg == "--audit-scripts") options.audit_scripts = true;
            else if (arg == "--audit-resources") options.audit_resources = true;
            else if (arg == "--trace-messages") options.trace_messages = integer(1, 100000);
            else if (arg == "--screenshot") options.screenshot = value();
            else if (arg == "--width") options.width = integer(320, 16384);
            else if (arg == "--height") options.height = integer(240, 16384);
            else if (arg == "--window-config") options.window_config = value();
            else if (arg == "--frames") options.frames = integer(1, 1000000);
            else if (arg == "--maximized") options.maximized = true;
            else if (arg == "--fullscreen") options.fullscreen = true;
            else if (arg == "--validate-content") options.validate = true;
            else if (arg == "--smoke") options.smoke = true;
            else throw std::runtime_error("Unknown option: " + std::string(arg));
        }
        if ((options.width == 0) != (options.height == 0))
            throw std::runtime_error("--width and --height must be used together");
        if (options.smoke && (options.maximized || options.fullscreen || options.frames || options.validate))
            throw std::runtime_error("--smoke requires a normal window and cannot combine with --frames/--validate-content");
        if (!options.screenshot.empty() && (!(options.frames || options.smoke) || options.validate))
            throw std::runtime_error("--screenshot requires --frames or --smoke");
        if ((options.text_preview || options.trace_messages || options.start_line || options.silent) && options.script.empty()) throw std::runtime_error("Script options require --script ID");
        if ((!options.script.empty() && (options.smoke || options.validate || options.audit_scripts || options.audit_resources)) ||
            (options.trace_messages && (options.frames || !options.screenshot.empty())))
            throw std::runtime_error("Conflicting script/validation/window smoke options");
        if ((options.audit_resources || options.audit_scripts) && (options.smoke || options.validate || options.frames || options.maximized || options.fullscreen || !options.screenshot.empty() || (options.audit_resources && options.audit_scripts)))
            throw std::runtime_error("Audit options require a separate headless invocation");
        return ef::run(options);
    } catch (const std::exception& e) {
        std::cerr << "ef_native: " << e.what() << '\n';
        return 1;
    }
}
