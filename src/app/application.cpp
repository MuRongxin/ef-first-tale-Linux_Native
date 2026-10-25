#include "app/application.hpp"
#include "app/script_view.hpp"
#include "audio/output.hpp"
#include "core/display.hpp"
#include "data/content.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include "text/font.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace ef {
namespace {
void checked(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
class Window {
public:
    SDL_Window* handle{};
    SDL_GLContext context{};
    explicit Window(const Options& options) {
        checked(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
        try {
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3), "GL major version");
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3), "GL minor version");
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE), "GL core profile");
            checked(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "GL double buffer");
            auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (options.maximized) flags |= SDL_WINDOW_MAXIMIZED;
            handle = SDL_CreateWindow("ef - the first tale. | Native preview", options.width, options.height, flags);
            if (!handle) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
            checked(SDL_SetWindowMinimumSize(handle, 320, 240), "Minimum window size");
            // A null fullscreen mode selects desktop/borderless; never exclusive.
            checked(SDL_SetWindowFullscreenMode(handle, nullptr), "Borderless fullscreen mode");
            if (options.fullscreen) checked(SDL_SetWindowFullscreen(handle, true), "Fullscreen");
            context = SDL_GL_CreateContext(handle);
            if (!context) throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
            if (!SDL_GL_SetSwapInterval(1)) std::cerr << "[video] vsync unavailable; frame pacing enabled\n";
        } catch (...) { cleanup(); throw; }
    }
    ~Window() { cleanup(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
private:
    void cleanup() {
        if (context) SDL_GL_DestroyContext(context);
        if (handle) SDL_DestroyWindow(handle);
        SDL_Quit();
    }
};
}
int run(const Options& options) {
    Content content(options.content);
    if (options.audit_resources) {
        const auto report = ScriptLibrary(content).audit_resources();
        std::cout << report.dump(2) << '\n';
        return report.at("missing") == 0 && report.at("ambiguous") == 0 && report.at("unavailable_files").empty() ? 0 : 2;
    }
    if (options.audit_scripts) {
        std::cout << ScriptLibrary(content).audit().dump(2) << '\n';
        return 0;
    }
    std::unique_ptr<ScriptView> script;
    if (!options.script.empty()) script = std::make_unique<ScriptView>(content, options);
    if (options.trace_messages) {
        int messages = 0;
        std::uint64_t last_message{};
        for (int operations = 0; messages < options.trace_messages; ++operations) {
            if (operations >= 100000) throw std::runtime_error("Trace operation budget exhausted");
            script->update(86400);
            const auto state = script->runtime().state();
            if (script->runtime().message_serial() != last_message) {
                last_message = script->runtime().message_serial();
                const auto& message = *script->runtime().message();
                std::cout << "message " << message.script << " L0=" << message.id << " source_line=" << message.source_line << '\n';
                ++messages;
                // Finish development typewriter text before requesting the next message.
                script->advance();
                script->advance();
            } else if (state == ScriptState::choices || state == ScriptState::ended)
                throw std::runtime_error("Trace stopped before requested message count: choice or script end");
            if (script->runtime().state() == ScriptState::message && script->runtime().message_complete()) script->advance();
        }
        std::cout << "Script trace PASS: " << messages << " messages\n";
        return 0;
    }
    auto scene = script ? script->scene() : Scene::load(content);
    if (options.validate) {
        content.validate_images();
        Font font(content);
        font.validate(scene);
        std::cout << "Content validation PASS: " << content.assets().size() << " images, " << scene.layers.size() << " layers\n";
        return 0;
    }
    Window window(options);
    Renderer renderer;
    // Declared after the GL context: all glyph/texture destructors run before it.
    auto font = std::make_unique<Font>(content);
    font->validate(scene);
    std::unique_ptr<AudioOutput> audio;
    if (script && !options.text_preview) {
        audio = std::make_unique<AudioOutput>(script->presentation().mixer(), !options.silent);
        script->presentation().realtime_audio(audio->active());
    }
    bool running = true;
    int frames = 0;
    int smoke_phase = 0;
    auto previous = SDL_GetTicksNS();
    const auto started = previous;
    FixedStep timer;
    std::uint64_t ticks{};
    std::optional<AssetTier> last_tier;
    while (running) {
        const auto frame_start = SDL_GetTicksNS();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if (script && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                int ww{}, wh{}, pw{}, ph{};
                checked(SDL_GetWindowSize(window.handle, &ww, &wh), "Window input size");
                checked(SDL_GetWindowSizeInPixels(window.handle, &pw, &ph), "Pixel input size");
                if (ww > 0 && wh > 0) {
                    const auto point = fit_viewport(pw, ph).to_design(event.button.x * static_cast<float>(pw) / static_cast<float>(ww),
                                                                   event.button.y * static_cast<float>(ph) / static_cast<float>(wh));
                    if (point) script->click(*point);
                }
            }
            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
            if (script) {
                if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) script->advance();
                if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) script->choose(event.key.key - SDLK_1);
            }
            const auto flags = SDL_GetWindowFlags(window.handle);
            switch (event.key.key) {
            case SDLK_ESCAPE:
                if (flags & SDL_WINDOW_FULLSCREEN) checked(SDL_SetWindowFullscreen(window.handle, false), "Leave fullscreen");
                else running = false;
                break;
            case SDLK_F11:
                checked(SDL_SetWindowFullscreen(window.handle, !(flags & SDL_WINDOW_FULLSCREEN)), "Toggle fullscreen");
                break;
            case SDLK_F10:
                if (!(flags & SDL_WINDOW_FULLSCREEN)) {
                    if (flags & SDL_WINDOW_MAXIMIZED) checked(SDL_RestoreWindow(window.handle), "Restore window");
                    else checked(SDL_MaximizeWindow(window.handle), "Maximize window");
                }
                break;
            case SDLK_R:
                if (script) { std::cerr << "[script] Restart the application to reload a script session\n"; break; }
                try {
                    Content replacement(options.content);
                    auto replacement_scene = Scene::load(replacement);
                    auto replacement_font = std::make_unique<Font>(replacement);
                    replacement_font->validate(replacement_scene);
                    content = std::move(replacement);
                    scene = std::move(replacement_scene);
                    font = std::move(replacement_font);
                    renderer.clear_cache();
                    last_tier.reset();
                    std::cout << "Content reloaded\n";
                } catch (const std::exception& e) { std::cerr << "[reload] " << e.what() << '\n'; }
                break;
            default: break;
            }
        }
        if (!running) break;
        const auto flags = SDL_GetWindowFlags(window.handle);
        const auto tier = asset_tier(flags & SDL_WINDOW_MAXIMIZED, flags & SDL_WINDOW_FULLSCREEN);
        if (tier != last_tier) {
            const auto title = scene.title + " | " + std::to_string(static_cast<int>(tier)) + "x";
            checked(SDL_SetWindowTitle(window.handle, title.c_str()), "Window title");
            std::cout << "window tier=" << static_cast<int>(tier)
                      << " maximized=" << !!(flags & SDL_WINDOW_MAXIMIZED)
                      << " fullscreen=" << !!(flags & SDL_WINDOW_FULLSCREEN) << '\n';
            last_tier = tier;
        }
        const int steps = timer.advance(static_cast<double>(frame_start - previous) / 1e9);
        ticks += static_cast<std::uint64_t>(steps);
        previous = frame_start;
        if (script) {
            for (int i = 0; i < steps; ++i) script->update(FixedStep::interval);
            scene = script->scene();
        }
        int width{}, height{};
        checked(SDL_GetWindowSizeInPixels(window.handle, &width, &height), "Framebuffer size");
        const auto view = fit_viewport(width, height);
        if (view.width == 0 || view.height == 0 || (flags & SDL_WINDOW_MINIMIZED)) { SDL_Delay(20); continue; }
        if (audio && audio->failed()) throw std::runtime_error("Audio output callback failed");
        renderer.begin(width, height, view, scene.background);
        if (script && !options.text_preview) {
            const auto& stage = script->presentation();
            renderer.stage(content, stage.previous(), stage.current(), stage.progress(), stage.revision(), tier);
            for (const auto& layer : stage.effects()) renderer.image(content,layer,tier);
        }
        for (const auto& layer : scene.layers) {
            switch (layer.kind) {
            case LayerKind::image: renderer.image(content, layer, tier); break;
            case LayerKind::rectangle: renderer.rectangle(layer); break;
            case LayerKind::text: font->draw(renderer, layer, view.scale()); break;
            }
        }
        ++frames;
        bool finished = options.frames > 0 && frames >= options.frames;
        if (options.smoke) {
            if (smoke_phase == 0 && frames >= 3) {
                if (tier != AssetTier::x4) throw std::runtime_error("Smoke must start in a normal 4x window");
                checked(SDL_SetWindowFullscreen(window.handle, true), "Smoke enter borderless");
                smoke_phase = 1;
            } else if (smoke_phase == 1 && tier == AssetTier::x8 && (flags & SDL_WINDOW_FULLSCREEN)) {
                checked(SDL_SetWindowFullscreen(window.handle, false), "Smoke leave borderless");
                smoke_phase = 2;
            } else if (smoke_phase == 2 && tier == AssetTier::x4 && !(flags & SDL_WINDOW_FULLSCREEN)) {
                finished = true;
                std::cout << "Window smoke PASS: 4x -> borderless 8x -> restored 4x\n";
            }
            if (SDL_GetTicksNS() - started > 15'000'000'000ULL) throw std::runtime_error("Window smoke timed out");
        }
        if (finished && !options.screenshot.empty()) renderer.capture(options.screenshot, width, height);
        checked(SDL_GL_SwapWindow(window.handle), "Swap buffers");
        if (finished) break;
        const auto elapsed = SDL_GetTicksNS() - frame_start;
        if (elapsed < 16'666'667ULL) SDL_DelayNS(16'666'667ULL - elapsed);
    }
    std::cout << "Exit: frames=" << frames << " fixed_ticks=" << ticks << '\n';
    return 0;
}
}
