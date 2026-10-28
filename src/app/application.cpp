#include "app/application.hpp"
#include "app/script_view.hpp"
#include "app/save_menu.hpp"
#include "app/title_menu.hpp"
#include "app/gallery.hpp"
#include "save/thumbnail.hpp"
#include "save/slots.hpp"
#include "audio/output.hpp"
#include "core/display.hpp"
#include "data/content.hpp"
#include "render/renderer.hpp"
#include "scene/scene.hpp"
#include "settings/window_settings.hpp"
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
    Window(const Options& options, const WindowSettings& remembered) {
        checked(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");
        try {
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3), "GL major version");
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3), "GL minor version");
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE), "GL core profile");
            checked(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "GL double buffer");
            SDL_Rect usable{};
            const SDL_DisplayID display = SDL_GetPrimaryDisplay();
            if (!display || (!SDL_GetDisplayUsableBounds(display, &usable) && !SDL_GetDisplayBounds(display, &usable)))
                usable = {};
            const auto requested = resolve_window_size(options.width, options.height,
                remembered.width, remembered.height, usable.w, usable.h);
            const bool explicit_size = options.width > 0 && options.height > 0;
            const bool start_maximized = options.maximized || (!explicit_size && remembered.maximized);
            const bool start_fullscreen = options.fullscreen;
            last_windowed_ = requested;
            maximized_ = start_maximized;
            auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (start_maximized) flags |= SDL_WINDOW_MAXIMIZED;
            handle = SDL_CreateWindow("ef - the first tale. | Native preview",
                requested.width, requested.height, flags);
            if (!handle) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
            checked(SDL_SetWindowMinimumSize(handle, 320, 240), "Minimum window size");
            // A null fullscreen mode selects desktop/borderless; never exclusive.
            checked(SDL_SetWindowFullscreenMode(handle, nullptr), "Borderless fullscreen mode");
            if (start_fullscreen) {
                maximized_before_fullscreen_ = maximized_;
                fullscreen_ = true;
                checked(SDL_SetWindowFullscreen(handle, true), "Fullscreen");
            }
            context = SDL_GL_CreateContext(handle);
            if (!context) throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
            if (!SDL_GL_SetSwapInterval(1)) std::cerr << "[video] vsync unavailable; frame pacing enabled\n" << '\n' << std::flush;
        } catch (...) { cleanup(); throw; }
    }
    ~Window() { cleanup(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    // Track windowed geometry independently of maximize/fullscreen; only the last
    // non-maximized, non-fullscreen size is persisted.
    void track(const SDL_Event& event) {
        if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
            maximized_before_fullscreen_ = maximized_;
            fullscreen_ = true;
            return;
        }
        if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
            fullscreen_ = false;
            maximized_ = maximized_before_fullscreen_;
            return;
        }
        if (event.type != SDL_EVENT_WINDOW_RESIZED && event.type != SDL_EVENT_WINDOW_MAXIMIZED &&
            event.type != SDL_EVENT_WINDOW_RESTORED && event.type != SDL_EVENT_WINDOW_MINIMIZED &&
            event.type != SDL_EVENT_WINDOW_DISPLAY_CHANGED)
            return;
        const auto flags = SDL_GetWindowFlags(handle);
        fullscreen_ = (flags & SDL_WINDOW_FULLSCREEN) != 0;
        if (event.type == SDL_EVENT_WINDOW_MAXIMIZED) {
            maximized_ = true;
            return;
        }
        if (event.type == SDL_EVENT_WINDOW_RESTORED) {
            // A restore always leaves the maximized state; update the windowed size here in
            // case the WM reports RESTORED before RESIZED.
            maximized_ = false;
            if (!fullscreen_) {
                int width{}, height{};
                if (SDL_GetWindowSize(handle, &width, &height) && width >= 320 && height >= 240)
                    last_windowed_ = {width, height};
            }
            return;
        }
        if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) return;
        const bool maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (!fullscreen_ && !maximized) {
            int width{}, height{};
            if (SDL_GetWindowSize(handle, &width, &height) && width >= 320 && height >= 240)
                last_windowed_ = {width, height};
            maximized_ = false;
        } else if (maximized && !fullscreen_) {
            maximized_ = true;
        }
    }
    [[nodiscard]] WindowSettings saveable_settings() const {
        return {last_windowed_.width, last_windowed_.height, maximized_};
    }
private:
    WindowSize last_windowed_{800, 600};
    bool maximized_{}, maximized_before_fullscreen_{}, fullscreen_{};
    void cleanup() {
        if (context) SDL_GL_DestroyContext(context);
        if (handle) SDL_DestroyWindow(handle);
        SDL_Quit();
    }
};}
int run(const Options& requested) {
    auto options=requested;
    Content content(options.content);
    if (options.audit_gallery) {std::cout<<GalleryCatalog(content).report().dump(2)<<'\n';return 0;}
    const bool frontend=(options.script.empty() || options.chapter_entry) && options.load_save.empty() && options.load_slot.empty() &&
        !options.validate && !options.smoke && !options.audit_scripts && !options.audit_resources &&
        !content.script_index().empty() && std::filesystem::exists(content.resolve(content.script_index()));
    bool title_active=frontend && options.script.empty();
    std::unique_ptr<SaveSlots> slots;
    if ((frontend && options.save_file.empty()) || !options.save_dir.empty() || options.save_slot || !options.load_slot.empty() || options.list_saves)
        slots=std::make_unique<SaveSlots>(options.save_dir.empty() ? default_save_directory() : options.save_dir);
    if (options.list_saves) {std::cout<<slots->list(content).dump(2)<<'\n';return 0;}
    std::optional<nlohmann::json> initial_save;
    if (!options.load_slot.empty()) {
        initial_save=options.load_slot=="latest" ? slots->load_latest_quick() : slots->load(parse_save_slot(options.load_slot));
        if (!options.save_slot && options.load_slot!="latest") {const auto id=parse_save_slot(options.load_slot);if (!id.quick) options.save_slot=id.number;}
    } else if (!options.load_save.empty()) initial_save=read_checkpoint(options.load_save);
    if (initial_save) {
        options.script=initial_save->at("vm").at("program").get<std::string>();
        if (options.script.empty()) throw std::runtime_error("Save has no script program");
        const auto variant=initial_save->at("vm").at("variant").get<std::string>();
        if (variant!="e" && variant!="j") throw std::runtime_error("Invalid saved timing variant");
        options.variant=variant[0];options.start_line=0;
    }
    const auto loose_save_path=[&] {return !options.save_file.empty() ? options.save_file : !options.load_save.empty() ? options.load_save : default_checkpoint_path();};
    const auto save_session=[&](const nlohmann::json& data) {
        if (slots) {
            if (options.save_slot) {slots->save_manual(options.save_slot,data);return "slot "+std::to_string(options.save_slot);}
            return "slot "+save_slot_name(slots->save_quick(data));
        }
        const auto path=loose_save_path();write_checkpoint(path,data);return path.string();
    };
    const auto load_session=[&]() -> std::pair<nlohmann::json,std::string> {
        if (slots) {
            if (options.save_slot) return {slots->load({false,options.save_slot}),"slot "+std::to_string(options.save_slot)};
            return {slots->load_latest_quick(),"latest quick slot"};
        }
        const auto path=loose_save_path();return {read_checkpoint(path),path.string()};
    };
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
    if (initial_save) script->restore(*initial_save);
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
                std::cout << "message " << message.script << " L0=" << message.id << " source_line=" << message.source_line << "\n\n" << std::flush;
                ++messages;
                if (messages==options.trace_messages && (!requested.save_file.empty() || requested.save_slot>0)) {
                    const auto serial=script->runtime().message_serial();
                    if (!script->runtime().message_complete()) script->advance();
                    for (int n=0;!script->runtime().message_complete() && n<1000;++n) script->update(.1);
                    if (script->runtime().message_serial()!=serial) throw std::runtime_error("Trace save point advanced automatically; choose a manual message");
                    const auto target=save_session(script->checkpoint());
                    std::cout<<"[save] saved "<<target<<'\n' << '\n' << std::flush;
                    break;
                }
                // Finish development typewriter text before requesting the next message.
                script->advance();
                script->advance();
            } else if (state == ScriptState::choices || state == ScriptState::ended)
                throw std::runtime_error("Trace stopped before requested message count: choice or script end");
            if (script->runtime().state() == ScriptState::message && script->runtime().message_complete()) script->advance();
        }
        std::cout << "Script trace PASS: " << messages << " messages\n" << '\n' << std::flush;
        return 0;
    }
    const auto font_config=options.font_config.empty() ? default_font_config() : options.font_config;
    FontSettings font_settings;
    try { font_settings=load_font_settings(font_config); }
    catch (const std::exception& e) { std::cerr << "[font] Cannot read preferences; using defaults: " << e.what() << "\n\n" << std::flush; }
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
            std::cerr << "[font] Saved font unavailable; using packaged font: " << e.what() << "\n\n" << std::flush;
            font_settings.file.clear(); font_settings.face=0;
            return std::make_unique<Font>(content,font_settings);
        }
    };
    auto scene = script ? script->scene() : title_active ? Scene{} : Scene::load(content);
    if (options.validate) {
        content.validate_images();
        auto font=create_font();
        font->validate(scene);
        std::cout << "Content validation PASS: " << content.assets().size() << " images, " << scene.layers.size() << " layers\n" << '\n' << std::flush;
        return 0;
    }
    std::filesystem::path window_config = options.window_config;
    if (window_config.empty()) {
        try { window_config = default_window_config(); }
        catch (const std::exception& e) { std::cerr << "[window] Cannot locate window preferences: " << e.what() << "\n\n" << std::flush; }
    }
    WindowSettings remembered;
    if (!window_config.empty()) {
        try { remembered = load_window_settings(window_config); }
        catch (const std::exception& e) { std::cerr << "[window] Cannot read geometry; using auto: " << e.what() << "\n\n" << std::flush; }
    }
    Window window(options, remembered);
    Renderer renderer;
    // Declared after the GL context: all glyph/texture destructors run before it.
    auto font = create_font();
    font->validate(scene);
    auto persist_font=[&]() {
        try { save_font_settings(font_config,font_settings); return true; }
        catch (const std::exception& e) { std::cerr << "[font] Preview active but preferences were not saved: " << e.what() << "\n\n" << std::flush; return false; }
    };
    if (font_override) persist_font();
    std::cout << "font=" << font->name() << " face=" << font_settings.face << " size=" << std::lround(font_settings.scale*100) << "%\n" << '\n' << std::flush;
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
            std::cout << "font applied=" << font->name() << " face=" << font_settings.face << " size=" << std::lround(font_settings.scale*100) << "%\n" << '\n' << std::flush;
        } catch (const std::exception& e) {
            font_notice_error=true;
            std::cerr << "[font] Selection rejected; previous font retained: " << e.what() << "\n\n" << std::flush;
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
    bool checkpoint_clock_reset{};
    std::string checkpoint_notice;
    Uint64 checkpoint_notice_until{};
    SaveSlots menu_slots(options.save_dir.empty() ? default_save_directory() : options.save_dir);
    std::unique_ptr<SaveMenu> save_menu;
    TitleMenu title_menu(content);
    std::unique_ptr<Gallery> gallery;
    int confirmation{}; // 1 return to title, 2 exit
    std::optional<bool> pending_menu;
    bool pending_quick_save{};
    nlohmann::json captured_thumbnail;
    auto checkpoint_action=[&](bool save, const nlohmann::json* menu_data=nullptr) -> bool {
        try {
            if ((save && !script) || options.text_preview) throw std::runtime_error("A strict game session is required");
            if (save) {
                auto data=script->checkpoint();if (!captured_thumbnail.is_null()) data["thumbnail"]=captured_thumbnail;
                const auto target=save_session(data);checkpoint_notice="进度已保存。";
                std::cout<<"[save] saved "<<target<<"\n\n" << std::flush;
            } else {
                const auto [data,target]=menu_data ? std::pair<nlohmann::json,std::string>{*menu_data,"menu slot"} : load_session();auto candidate_options=options;
                candidate_options.script=data.at("vm").at("program").get<std::string>();
                const auto variant=data.at("vm").at("variant").get<std::string>();
                if (variant!="e" && variant!="j") throw std::runtime_error("Invalid saved timing variant");
                candidate_options.variant=variant[0];candidate_options.start_line=0;
                auto replacement=std::make_unique<ScriptView>(content,candidate_options);replacement->restore(data);
                auto replacement_scene=replacement->scene();font->validate(replacement_scene);
                const auto flags=SDL_GetWindowFlags(window.handle);
                const auto tier=asset_tier(flags & SDL_WINDOW_MAXIMIZED,flags & SDL_WINDOW_FULLSCREEN);
                renderer.preload(content,replacement_scene.layers,tier);
                renderer.preload(content,replacement->presentation().current(),tier);
                renderer.preload(content,replacement->presentation().effects(),tier);
                auto replacement_audio=std::make_unique<AudioOutput>(replacement->presentation().mixer(),!options.silent);
                replacement->presentation().realtime_audio(replacement_audio->active());
#ifdef EF_HAS_FFMPEG
                replacement->presentation().set_movie_supported(true);
                movie.reset();movie_started=0;
#endif
                audio.reset();script.swap(replacement);audio=std::move(replacement_audio);scene=std::move(replacement_scene);
                title_active=false;
                renderer.invalidate_scene();last_tier.reset();checkpoint_clock_reset=true;
                checkpoint_notice="进度已恢复。";std::cout<<"[save] loaded "<<target<<"\n\n" << std::flush;
            }
        } catch (const std::exception& e) {
            checkpoint_notice_until=SDL_GetTicks()+4000;
            checkpoint_notice=save ? "暂时无法保存，请等待对白或演出完成，并检查存档目录。" : "读档失败，当前进度已保留。";
            std::cerr<<"[save] "<<e.what()<<"\n\n" << std::flush;return false;
        }
        checkpoint_notice_until=SDL_GetTicks()+4000;return true;
    };
    auto close_menu=[&] {save_menu.reset();if (audio) audio->pause(false);checkpoint_clock_reset=true;};
    auto return_to_title=[&] {
#ifdef EF_HAS_FFMPEG
        movie.reset();movie_started=0;
#endif
        save_menu.reset();audio.reset();script.reset();
        pending_menu.reset();pending_quick_save=false;captured_thumbnail=nullptr;
        scene=Scene{};scene.title="ef - the first tale.";
        renderer.clear_cache();renderer.invalidate_scene();last_tier.reset();
        title_active=true;title_menu.reset();checkpoint_clock_reset=true;
        checkpoint_notice_until=0;
        std::cout<<"[title] opened; session released"<<"\n\n" << std::flush;
    };
    auto start_chapter=[&](int index) {
        try {
            auto candidate_options=options;
            candidate_options.script=chapters.at(static_cast<std::size_t>(index)).script;
            candidate_options.chapter_entry=true;candidate_options.start_line=0;candidate_options.text_preview=false;
            auto replacement=std::make_unique<ScriptView>(content,candidate_options);
            auto replacement_scene=replacement->scene();font->validate(replacement_scene);
            auto replacement_audio=std::make_unique<AudioOutput>(replacement->presentation().mixer(),!options.silent);
            replacement->presentation().realtime_audio(replacement_audio->active());
#ifdef EF_HAS_FFMPEG
            replacement->presentation().set_movie_supported(true);
            movie.reset();
#endif
            audio.reset();script=std::move(replacement);audio=std::move(replacement_audio);scene=std::move(replacement_scene);
            title_active=false;renderer.invalidate_scene();last_tier.reset();checkpoint_clock_reset=true;
            std::cout<<"[title] chapter "<<index<<" started"<<"\n\n" << std::flush;
        } catch (const std::exception& e) {
            checkpoint_notice="章节启动失败，请检查内容包。";checkpoint_notice_until=SDL_GetTicks()+4000;
            std::cerr<<"[title] "<<e.what()<<"\n\n" << std::flush;
        }
    };
    if (title_active) std::cout<<"[title] opened"<<"\n\n" << std::flush;
    while (running) {
        const auto frame_start = SDL_GetTicksNS();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window.track(event);
            std::optional<Point> menu_point;
            if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_MOTION) {
                int ww{},wh{},pw{},ph{};SDL_GetWindowSize(window.handle,&ww,&wh);SDL_GetWindowSizeInPixels(window.handle,&pw,&ph);
                const float x=event.type==SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
                const float y=event.type==SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
                if (ww>0 && wh>0) menu_point=fit_viewport(pw,ph).to_design(x*static_cast<float>(pw)/static_cast<float>(ww),y*static_cast<float>(ph)/static_cast<float>(wh));
            }
            if (confirmation) {
                bool accept=false,cancel=false;
                if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    accept=event.key.key==SDLK_RETURN || event.key.key==SDLK_Y;
                    cancel=event.key.key==SDLK_ESCAPE || event.key.key==SDLK_N;
                }
                if (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT && menu_point) {
                    accept=SaveMenuRect{240,310,390,355}.contains(*menu_point);
                    cancel=SaveMenuRect{410,310,560,355}.contains(*menu_point);
                }
                if (accept) {const int action=confirmation;confirmation=0;if (action==2) running=false;else return_to_title();}
                if (cancel) {confirmation=0;if (audio && !save_menu) audio->pause(false);checkpoint_clock_reset=true;std::cout<<"[title] confirmation cancelled"<<"\n\n" << std::flush;}
                continue;
            }
            if (event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (frontend) {confirmation=2;if (audio) audio->pause(true);continue;}
            }
            if (event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {running=false;continue;}
            if (gallery && !(event.type==SDL_EVENT_KEY_DOWN && (event.key.key==SDLK_F10 || event.key.key==SDLK_F11))) {
                if (gallery->event(event,menu_point)) {gallery.reset();title_menu.reset();std::cout<<"[gallery] returned to title\n"<<std::flush;}
                continue;
            }
            if (title_active && !gallery && !save_menu && !(event.type==SDL_EVENT_KEY_DOWN && (event.key.key==SDLK_F10 || event.key.key==SDLK_F11 || (event.key.key>=SDLK_F5 && event.key.key<=SDLK_F9))) && event.type!=SDL_EVENT_DROP_FILE) {
                const int action=title_menu.event(event,menu_point);
                if (action==1 || (action>=10 && action<=13)) start_chapter(action==1 ? 0 : action-10);
                if (action==2) {
                    try {
                        std::optional<SaveSlot> latest;std::int64_t sequence=-1;
                        const auto listing=menu_slots.list(content);
                        for (const auto& row:listing.at("slots")) {
                            if (row.at("status")=="compatible" && row.at("sequence").get<std::int64_t>()>sequence) {
                                latest=parse_save_slot(row.at("slot").get<std::string>());sequence=row.at("sequence").get<std::int64_t>();
                            }
                        }
                        if (latest) {const auto data=menu_slots.load(*latest);checkpoint_action(false,&data);}
                        else {checkpoint_notice="暂无可继续的存档。可选择新游戏或任意章节。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cout<<"[title] no compatible save"<<"\n\n" << std::flush;}
                    } catch (const std::exception& e) {checkpoint_notice="无法读取存档目录。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cerr<<"[title] "<<e.what()<<"\n\n" << std::flush;}
                }
                if (action==3) {
                    try {save_menu=std::make_unique<SaveMenu>(content,menu_slots,false,nullptr,false);std::cout<<"[save-menu] opened title load"<<"\n\n" << std::flush;}
                    catch (const std::exception& e) {checkpoint_notice="读档菜单无法打开，请检查内容包。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cerr<<"[title] "<<e.what()<<"\n\n" << std::flush;}
                }
                if (action==5) {
                    try {renderer.clear_cache();gallery=std::make_unique<Gallery>(content);}
                    catch (const std::exception& e) {checkpoint_notice="图鉴无法打开，请检查内容包。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cerr<<"[gallery] "<<e.what()<<'\n';}
                }
                if (action==4) confirmation=2;
                if (action==20) checked(SDL_SetWindowFullscreen(window.handle,!(SDL_GetWindowFlags(window.handle)&SDL_WINDOW_FULLSCREEN)),"Title fullscreen");
                if (action>=21 && action<=24) {
                    auto candidate=font_settings;
                    if (action==21 || action==22) candidate.scale=static_cast<float>(std::clamp(std::lround(candidate.scale*100)+(action==22 ? 5 : -5),75L,150L))/100;
                    if (action==23) candidate={};
                    if (action==24) {
                        const auto old=candidate.appearance.value_or(TextAppearance{});TextAppearance next;next.baseline_offset=old.baseline_offset;
                        if (old.outline_width==0) {next.shadow_x=next.shadow_y=1;if (old.shadow_x!=0 || old.shadow_y!=0) next.outline_width=1;}
                        candidate.appearance=next;
                    }
                    change_font(candidate);
                }
                if (action==25 || action==26) options.text_speed=std::clamp(options.text_speed+(action==26 ? 10 : -10),10,200);
                continue;
            }
            if (frontend && script && !save_menu && event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key==SDLK_ESCAPE) {
                confirmation=1;if (audio) audio->pause(true);std::cout<<"[title] confirm return"<<"\n\n" << std::flush;continue;
            }
            if (save_menu && event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key==SDLK_F10 || event.key.key==SDLK_F11)) {
                const auto flags=SDL_GetWindowFlags(window.handle);
                if (event.key.key==SDLK_F11) checked(SDL_SetWindowFullscreen(window.handle,!(flags & SDL_WINDOW_FULLSCREEN)),"Toggle fullscreen");
                else if (!(flags & SDL_WINDOW_FULLSCREEN)) {
                    if (flags & SDL_WINDOW_MAXIMIZED) checked(SDL_RestoreWindow(window.handle),"Restore window");
                    else checked(SDL_MaximizeWindow(window.handle),"Maximize window");
                }
                continue;
            }
            if (save_menu) {
                std::optional<Point> point;
                if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
                    int ww{},wh{},pw{},ph{};SDL_GetWindowSize(window.handle,&ww,&wh);SDL_GetWindowSizeInPixels(window.handle,&pw,&ph);
                    if (ww>0 && wh>0) point=fit_viewport(pw,ph).to_design(event.button.x*static_cast<float>(pw)/static_cast<float>(ww),event.button.y*static_cast<float>(ph)/static_cast<float>(wh));
                }
                if (event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) {running=false;continue;}
                const auto result=save_menu->event(event,point);
                if (result<0) close_menu();
                if (result==2) {
                    try {
                        const bool saving=!save_menu->saving();nlohmann::json data;
                        if (saving) {data=script->checkpoint();data["thumbnail"]=captured_thumbnail;}
                        auto candidate=std::make_unique<SaveMenu>(content,menu_slots,saving,std::move(data));save_menu=std::move(candidate);
                    } catch (const std::exception& e) {checkpoint_notice="当前演出尚不能保存。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cerr<<"[save-menu] "<<e.what()<<"\n\n" << std::flush;}
                }
                if (result==1) {
                    try {
                        const auto id=save_menu->selected_slot();
                        if (save_menu->saving()) {
                            menu_slots.save_manual(id.number,save_menu->snapshot);
                            checkpoint_notice="进度已保存。";checkpoint_notice_until=SDL_GetTicks()+4000;
                            std::cout<<"[save] saved menu slot "<<save_slot_name(id)<<"\n\n" << std::flush;close_menu();
                        } else {const auto data=menu_slots.load(id);if (checkpoint_action(false,&data)) close_menu();}
                    } catch (const std::exception& e) {
                        checkpoint_notice="存档操作失败，当前进度已保留。";checkpoint_notice_until=SDL_GetTicks()+4000;std::cerr<<"[save] "<<e.what()<<"\n\n" << std::flush;
                    }
                }
                continue;
            }
            if (pending_menu || pending_quick_save) continue;
            if (script && !options.text_preview &&
                ((event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key==SDLK_F4 || event.key.key==SDLK_F12)) ||
                 (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_RIGHT))) {
                if (!script->presentation().movie_active()) pending_menu=!(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_F12);
                continue;
            }
            if (script && event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key==SDLK_F2 || event.key.key==SDLK_F3)) {
                if (event.key.key==SDLK_F2) pending_quick_save=true;else checkpoint_action(false);continue;
            }
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
                if (gallery) break;
                if (script) { std::cerr << "[script] Restart the application to reload a script session\n" << '\n' << std::flush; break; }
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
                    std::cout << "Content reloaded\n" << '\n' << std::flush;
                } catch (const std::exception& e) { std::cerr << "[reload] " << e.what() << "\n\n" << std::flush; }
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
                      << " fullscreen=" << !!(flags & SDL_WINDOW_FULLSCREEN) << "\n\n" << std::flush;
            last_tier = tier;
        }
        int steps=0;
        if (checkpoint_clock_reset) {timer=FixedStep{};previous=SDL_GetTicksNS();checkpoint_clock_reset=false;}
        else {steps=timer.advance(static_cast<double>(frame_start-previous)/1e9);previous=frame_start;}
        ticks += static_cast<std::uint64_t>(steps);
        int width{}, height{};
        checked(SDL_GetWindowSizeInPixels(window.handle, &width, &height), "Framebuffer size");
        const auto view = fit_viewport(width, height);
        if (script) {
            if (!confirmation && !save_menu && !pending_menu && !pending_quick_save) for (int i = 0; i < steps; ++i) script->update(FixedStep::interval);
            scene = script->scene();
        }
#ifdef EF_HAS_FFMPEG
        if (script && !options.text_preview) {
            auto& presentation = script->presentation();
            if (presentation.movie_active()) {
                const auto& request = *presentation.movie_request();
                if (!movie) {
                    const auto texture = movie_texture_size(request.width, request.height,
                        view.width, view.height, options.movie_texture_cap);
                    const int texture_width = texture.width > 0 ? texture.width : request.width;
                    const int texture_height = texture.height > 0 ? texture.height : request.height;
                    movie = std::make_unique<MoviePlayer>(content.resolve(request.path),
                        texture_width, texture_height, audio ? audio.get() : nullptr);
                    movie_started = SDL_GetTicksNS();
                    std::cerr << "[movie] start " << request.asset << " dest " << request.width << "x" << request.height
                              << " texture " << texture_width << "x" << texture_height << "\n" << '\n' << std::flush;
                }
                const auto now = SDL_GetTicksNS();
                const double delta = movie_started == 0 ? 0.0 : static_cast<double>(now - movie_started) / 1e9;
                movie_started = now;
                if (!confirmation) movie->update(delta);
                if (movie->finished()) {
                    const std::string asset = request.asset;
                    presentation.finish_movie();
                    movie.reset();
                    std::cerr << "[movie] finished " << asset << "\n" << '\n' << std::flush;
                }
            } else if (movie) movie.reset();
        }
#endif
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
            if (!pending_menu && !pending_quick_save && !save_menu && SDL_GetTicks()<font_notice_until) {
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
        if (pending_menu || pending_quick_save) {
            try {
                captured_thumbnail=encode_save_thumbnail(renderer.save_thumbnail());
                if (pending_menu) {
                    if (audio) audio->pause(true);
                    nlohmann::json data;
                    if (*pending_menu) {data=script->checkpoint();data["thumbnail"]=captured_thumbnail;}
                    auto candidate=std::make_unique<SaveMenu>(content,menu_slots,*pending_menu,std::move(data));
                    save_menu=std::move(candidate);
                    std::cout<<"[save-menu] opened "<<(*pending_menu ? "save" : "load")<<"\n\n" << std::flush;
                } else checkpoint_action(true);
            } catch (const std::exception& e) {
                if (audio && !save_menu) audio->pause(false);
                checkpoint_notice="暂时无法打开存档菜单，请等待对白或演出完成，并更新 content 布局。";checkpoint_notice_until=SDL_GetTicks()+4000;
                std::cerr<<"[save-menu] "<<e.what()<<"\n\n" << std::flush;
            }
            pending_menu.reset();pending_quick_save=false;
        }
        if (frontend && script && script->runtime().state()==ScriptState::ended && !confirmation) return_to_title();
        if (title_active && !gallery) title_menu.draw(renderer,*font,tier,view.scale(),static_cast<int>(std::lround(font_settings.scale*100)),options.text_speed,flags & SDL_WINDOW_FULLSCREEN);
        if (gallery) gallery->draw(renderer,*font,tier,view.scale());
        if (save_menu) save_menu->draw(renderer,*font,tier,view.scale());
        if (confirmation) {
            Layer box;box.kind=LayerKind::rectangle;box.width=800;box.height=600;box.color={0,0,0,.7f};renderer.rectangle(box);
            box.x=200;box.y=235;box.width=400;box.height=140;box.color={.06f,.08f,.18f,1};renderer.rectangle(box);
            Layer text;text.kind=LayerKind::text;text.x=225;text.y=260;text.width=355;text.font_size=20;
            text.text=confirmation==1 ? "返回标题？未保存的进度将丢失。" : "退出游戏？";font->draw(renderer,text,view.scale());
            text.x=245;text.y=320;text.width=150;text.font_size=18;text.text="确定（Enter）";font->draw(renderer,text,view.scale());
            text.x=415;text.text="取消（Esc）";font->draw(renderer,text,view.scale());
        }
        if (SDL_GetTicks()<checkpoint_notice_until) {
            Layer box;box.kind=LayerKind::rectangle;box.x=10;box.y=100;box.width=780;box.height=42;box.color={0,0,0,.9f};renderer.rectangle(box);
            Layer notice;notice.kind=LayerKind::text;notice.x=20;notice.y=108;notice.width=760;notice.font_size=18;notice.text=checkpoint_notice;font->draw(renderer,notice,view.scale());
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
                std::cout << "Window smoke PASS: 4x -> borderless 8x -> restored 4x\n" << '\n' << std::flush;
            }
            if (SDL_GetTicksNS() - started > 15'000'000'000ULL) throw std::runtime_error("Window smoke timed out");
        }
        if (finished && !options.screenshot.empty()) renderer.capture(options.screenshot, width, height);
        checked(SDL_GL_SwapWindow(window.handle), "Swap buffers");
        if (finished) break;
        const auto elapsed = SDL_GetTicksNS() - frame_start;
        if (elapsed < 16'666'667ULL) SDL_DelayNS(16'666'667ULL - elapsed);
    }
    if (!window_config.empty()) {
        try { save_window_settings(window_config, window.saveable_settings()); }
        catch (const std::exception& e) { std::cerr << "[window] Geometry not saved: " << e.what() << "\n\n" << std::flush; }
    }
    std::cout << "Exit: frames=" << frames << " fixed_ticks=" << ticks << "\n\n" << std::flush;
    return 0;
}
}
