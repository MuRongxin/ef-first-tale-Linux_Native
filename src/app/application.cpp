#include "app/application.hpp"
#include "app/script_view.hpp"
#include "audio/output.hpp"
#include "core/display.hpp"
#include "data/content.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include "text/font.hpp"
#ifdef EF_HAS_FFMPEG
#include "video/movie_player.hpp"
#endif
#include <SDL3/SDL.h>
#include <memory>
#include <cmath>
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
    const auto font_config=options.font_config.empty() ? default_font_config() : options.font_config;
    FontSettings font_settings;
    try { font_settings=load_font_settings(font_config); }
    catch (const std::exception& e) { std::cerr << "[font] Cannot read preferences; using defaults: " << e.what() << '\n'; }
    if (options.default_font) font_settings={};
    if (options.font_file) { font_settings.file=*options.font_file; font_settings.face=0; }
    if (options.font_face) font_settings.face=*options.font_face;
    if (options.font_percent) font_settings.scale=static_cast<float>(*options.font_percent)/100;
    if (options.text_outline || options.text_shadow || options.text_baseline) {
        auto appearance=font_settings.appearance.value_or(TextAppearance{});
        if (options.text_outline) appearance.outline_width=static_cast<float>(*options.text_outline);
        if (options.text_shadow) appearance.shadow_x=appearance.shadow_y=static_cast<float>(*options.text_shadow);
        if (options.text_baseline) appearance.baseline_offset=static_cast<float>(*options.text_baseline);
        font_settings.appearance=appearance;
    }
    font_settings.validate();
    const bool font_override=options.default_font || options.font_file || options.font_face || options.font_percent ||
        options.text_outline || options.text_shadow || options.text_baseline;
    auto create_font=[&]() {
        try { return std::make_unique<Font>(content,font_settings); }
        catch (const std::exception& e) {
            if (options.font_file || options.font_face || font_settings.file.empty()) throw;
            std::cerr << "[font] Saved font unavailable; using packaged font: " << e.what() << '\n';
            font_settings.file.clear(); font_settings.face=0;
            return std::make_unique<Font>(content,font_settings);
        }
    };
    auto scene = script ? script->scene() : Scene::load(content);
    if (options.validate) {
        content.validate_images();
        auto font=create_font();
        font->validate(scene);
        std::cout << "Content validation PASS: " << content.assets().size() << " images, " << scene.layers.size() << " layers\n";
        return 0;
    }
    Window window(options);
    Renderer renderer;
    // Declared after the GL context: all glyph/texture destructors run before it.
    auto font = create_font();
    font->validate(scene);
    auto persist_font=[&]() {
        try { save_font_settings(font_config,font_settings); return true; }
        catch (const std::exception& e) { std::cerr << "[font] Preview active but preferences were not saved: " << e.what() << '\n'; return false; }
    };
    if (font_override) persist_font();
    std::cout << "font=" << font->name() << " face=" << font_settings.face << " size=" << std::lround(font_settings.scale*100) << "%\n";
    Uint64 font_notice_until{};
    bool font_notice_error{}, font_notice_saved{true};
    auto change_font=[&](const FontSettings& candidate) {
        try {
            auto replacement=std::make_unique<Font>(content,candidate);
            replacement->validate(scene);
            auto preview=scene;
            replacement->style(preview);
            replacement->fit_messages(preview,1);
            const auto title=scene.title+" | "+replacement->name()+" | "+std::to_string(std::lround(candidate.scale*100))+"%";
            checked(SDL_SetWindowTitle(window.handle,title.c_str()),"Font preview title");
            font=std::move(replacement); font_settings=candidate;
            font_notice_saved=persist_font(); font_notice_error=false;
            std::cout << "font applied=" << font->name() << " face=" << font_settings.face << " size=" << std::lround(font_settings.scale*100) << "%\n";
        } catch (const std::exception& e) {
            font_notice_error=true;
            std::cerr << "[font] Selection rejected; previous font retained: " << e.what() << '\n';
        }
        font_notice_until=SDL_GetTicks()+5000;
    };
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE,true);
    std::unique_ptr<AudioOutput> audio;
    if (script && !options.text_preview) {
        audio = std::make_unique<AudioOutput>(script->presentation().mixer(), !options.silent);
        script->presentation().realtime_audio(audio->active());
#ifdef EF_HAS_FFMPEG
        script->presentation().set_movie_supported(true);
#endif
    }
#ifdef EF_HAS_FFMPEG
    std::unique_ptr<MoviePlayer> movie;
    std::uint64_t movie_started{};
#endif
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
#ifdef EF_HAS_FFMPEG
            if (movie && script && script->presentation().movie_active()) {
                const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT;
                const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                    event.key.key != SDLK_F10 && event.key.key != SDLK_F11;
                if ((click || key) && script->presentation().movie_request() && script->presentation().movie_request()->skippable)
                    movie->skip();
                if (click || key) continue; // the movie owns input while it is on screen
            }
#endif
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
            if (event.type==SDL_EVENT_DROP_FILE && event.drop.data) {
                auto candidate=font_settings;
                candidate.file=std::filesystem::absolute(event.drop.data); candidate.face=0;
                change_font(candidate);
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
            case SDLK_F5: {
                auto candidate=font_settings;
                const auto previous_appearance=candidate.appearance.value_or(TextAppearance{});
                TextAppearance next; next.baseline_offset=previous_appearance.baseline_offset;
                if (previous_appearance.outline_width==0) {
                    next.shadow_x=next.shadow_y=1;
                    if (previous_appearance.shadow_x!=0 || previous_appearance.shadow_y!=0) next.outline_width=1;
                }
                candidate.appearance=next;
                change_font(candidate); break;
            }
            case SDLK_F6: {
                if (font_settings.file.empty()) break;
                auto candidate=font_settings;
                candidate.face=static_cast<int>((candidate.face+1)%font->face_count());
                change_font(candidate); break;
            }
            case SDLK_F7:
            case SDLK_F8: {
                auto candidate=font_settings;
                candidate.scale=static_cast<float>(std::clamp(std::lround(candidate.scale*100)+(event.key.key==SDLK_F8 ? 5 : -5),75L,150L))/100;
                change_font(candidate); break;
            }
            case SDLK_F9: change_font({}); break;
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
                    auto replacement_font = std::make_unique<Font>(replacement,font_settings);
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
#ifdef EF_HAS_FFMPEG
        if (script && !options.text_preview) {
            auto& presentation = script->presentation();
            if (presentation.movie_active()) {
                const auto& request = *presentation.movie_request();
                if (!movie) {
                    movie = std::make_unique<MoviePlayer>(content.resolve(request.path), request.width, request.height, audio ? audio.get() : nullptr);
                    movie_started = SDL_GetTicksNS();
                    std::cerr << "[movie] start " << request.asset << " " << request.width << "x" << request.height << "\n";
                }
                const auto now = SDL_GetTicksNS();
                const double delta = movie_started == 0 ? 0.0 : static_cast<double>(now - movie_started) / 1e9;
                movie_started = now;
                movie->update(delta);
                if (movie->finished()) {
                    const std::string asset = request.asset;
                    presentation.finish_movie();
                    movie.reset();
                    std::cerr << "[movie] finished " << asset << "\n";
                }
            } else if (movie) movie.reset();
        }
#endif
        int width{}, height{};
        checked(SDL_GetWindowSizeInPixels(window.handle, &width, &height), "Framebuffer size");
        const auto view = fit_viewport(width, height);
        if (view.width == 0 || view.height == 0 || (flags & SDL_WINDOW_MINIMIZED)) { SDL_Delay(20); continue; }
        if (audio && audio->failed()) throw std::runtime_error("Audio output callback failed");
        auto rendered_scene=scene;
        font->style(rendered_scene);
        font->fit_messages(rendered_scene,view.scale());
        bool movie_drawn = false;
#ifdef EF_HAS_FFMPEG
        if (movie && script && script->presentation().movie_active() && !movie->rgba().empty()) {
            const auto& request = *script->presentation().movie_request();
            renderer.begin(width, height, view, scene.background);
            renderer.movie(movie->rgba().data(), movie->width(), movie->height(),
                request.x, request.y, request.width, request.height);
            movie_drawn = true;
        }
#endif
        if (!movie_drawn) {
            renderer.begin(width, height, view, scene.background);
            if (script && !options.text_preview) {
                const auto& stage = script->presentation();
                renderer.stage(content, stage.previous(), stage.current(), stage.progress(), stage.revision(), tier, stage.transition_mask(),stage.transition_type(),
                    [&](const Layer& layer) { font->draw(renderer,layer,view.scale()); });
            }
            for (const auto& layer : rendered_scene.layers) {
                switch (layer.kind) {
                case LayerKind::image: renderer.image(content, layer, tier); break;
                case LayerKind::rectangle: renderer.rectangle(layer); break;
                case LayerKind::text: {
                    const auto drawn=font->draw(renderer,layer,view.scale());
                    if (layer.advance_marker) {
                        const auto& marker=*layer.advance_marker;
                        Layer sprite; sprite.kind=LayerKind::image; sprite.asset=marker.asset;
                        sprite.width=sprite.height=static_cast<float>(marker.frame_size);
                        sprite.x=std::floor((drawn.cursor.x+5)/2)*2;
                        sprite.y=drawn.cursor.y+drawn.line_height-sprite.height+2;
                        sprite.source=std::array<float,4>{static_cast<float>(marker.frame*marker.frame_size),0,sprite.width,sprite.height};
                        renderer.image(content,sprite,tier);
                    }
                    break;
                }
                }
            }
            if (script && !options.text_preview && script->presentation().shaking())
                renderer.shake(script->presentation().shake_frame(),script->presentation().shake_serial());
            if (SDL_GetTicks()<font_notice_until) {
                Layer background; background.kind=LayerKind::rectangle;
                background.x=10; background.y=10; background.width=780; background.height=84;
                background.color={0,0,0,0.9f}; renderer.rectangle(background);
                Layer notice; notice.kind=LayerKind::text; notice.x=20; notice.y=16; notice.width=760; notice.font_size=18;
                notice.text=font_notice_error ? "字体无法使用，已保留之前的设置。" :
                    "字号 "+std::to_string(std::lround(font_settings.scale*100))+"%   字面 "+std::to_string(font_settings.face)+"   F5 切换外观   F6 切换字面";
                notice.text+="\nF7/F8 调整字号   F9 恢复默认   拖入字体文件即可应用。";
                notice.text+=font_notice_saved ? "\n有效设置已自动保存。" : "\n当前预览已生效，但配置保存失败，请检查配置目录。";
                font->draw(renderer,notice,view.scale());
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
