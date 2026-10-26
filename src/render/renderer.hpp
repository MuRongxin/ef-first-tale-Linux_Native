#pragma once
#include "core/display.hpp"
#include "data/content.hpp"
#include "data/image.hpp"
#include "scene/scene.hpp"
#include "scene/shake.hpp"
#include <GL/glew.h>
#include <cstdint>
#include <array>
#include <map>
#include <set>

namespace ef {
class Texture {
public:
    Texture() = default;
    explicit Texture(const Image& image);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    [[nodiscard]] GLuint id() const { return id_; }
private:
    GLuint id_{};
};
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void begin(int pixel_width, int pixel_height, Viewport view, Color background);
    void quad(const Texture& texture, float x, float y, float w, float h, Color color);
    void rectangle(const Layer& layer);
    void image(const Content& content, const Layer& layer, AssetTier tier);
    // Layers are composed on opaque black before the complete scenes are blended.
    // Text layers are ignored here; callers render text after the stage.
    void stage(const Content& content, const std::vector<Layer>& previous, const std::vector<Layer>& current,
               float progress, std::uint64_t revision, AssetTier tier, const std::string& mask = {});
    void shake(const ShakeFrame& frame,std::uint64_t serial);
    void clear_cache();
    void capture(const std::filesystem::path& path, int width, int height) const;
private:
    void cleanup();
    void quad_id(GLuint texture, float x, float y, float w, float h, Color color, bool flip_y, std::array<float,4> uv = {0,0,1,1}, GLuint screen = 0, std::array<float,4> screen_map = {}, GLuint overlay = 0, std::array<float,4> overlay_map = {}, std::array<int,3> hsv = {}, GLuint old = 0, GLuint mask = 0, float phase = 0);
    void opaque_scene(const Content& content, const std::vector<Layer>& layers, AssetTier tier);
    struct Target {
        GLuint framebuffer{}, texture{};
        int width{}, height{};
        ~Target();
        void reset();
        void resize(int w, int h);
        Target() = default;
        Target(const Target&) = delete;
        Target& operator=(const Target&) = delete;
    };
    struct Entry { Texture texture; std::size_t bytes{}; std::uint64_t used{}; };
    Entry& texture(const Content& content,const std::string& asset,AssetTier tier,bool strip=false,GLuint pinned=0);
    std::map<std::string, Entry> cache_,strips_;
    std::set<std::string> rejected_,strip_rejected_,reported_;
    std::size_t cache_bytes_{},strip_bytes_{};
    static constexpr std::size_t strip_budget_=4ULL*1024*1024;
    std::uint64_t clock_{};
    // Keep both 4x walking panoramas and an 8x frame resident together.
    static constexpr std::size_t budget_ = 512ULL * 1024 * 1024;
    GLuint program_{}, vao_{}, vbo_{};
    GLint rect_uniform_{}, color_uniform_{}, flip_uniform_{}, uv_uniform_{};
    GLint screen_uniform_{},screen_map_uniform_{};
    GLint hsv_uniform_{},hsv_enabled_{},transition_uniform_{},phase_uniform_{};
    GLint overlay_uniform_{},overlay_map_uniform_{};
    int max_texture_size_{};
    Texture white_;
    Target previous_target_, current_target_;
    Target shake_source_,shake_history_,shake_next_;
    GLuint shake_program_{};
    std::uint64_t shake_serial_{},shake_sample_{~std::uint64_t{0}};
    std::array<GLint,4> shake_view_{};
    bool shake_valid_{};
    bool snapshot_valid_{};
    std::uint64_t snapshot_revision_{};
    AssetTier snapshot_tier_{AssetTier::x4};
    std::array<GLint, 4> snapshot_view_{};
    static constexpr std::uint64_t composite_budget_ = 256ULL * 1024 * 1024;
};
}
