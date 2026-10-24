#pragma once
#include "core/display.hpp"
#include "data/content.hpp"
#include "data/image.hpp"
#include "scene/scene.hpp"
#include <GL/glew.h>
#include <cstdint>
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
    void clear_cache();
    void capture(const std::filesystem::path& path, int width, int height) const;
private:
    void cleanup();
    struct Entry { Texture texture; std::size_t bytes{}; std::uint64_t used{}; };
    std::map<std::string, Entry> cache_;
    std::set<std::string> rejected_, reported_;
    std::size_t cache_bytes_{};
    std::uint64_t clock_{};
    static constexpr std::size_t budget_ = 256ULL * 1024 * 1024;
    GLuint program_{}, vao_{}, vbo_{};
    GLint rect_uniform_{}, color_uniform_{};
    int max_texture_size_{};
    Texture white_;
};
}
