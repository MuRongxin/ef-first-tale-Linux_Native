#include "render/renderer.hpp"
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace ef {
namespace {
void checked_gl(const char* operation) {
    if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(error));
}
struct StageState {
    GLint draw{}, read{};
    std::array<GLint, 4> viewport{}, scissor{};
    std::array<GLfloat, 4> clear{};
    GLint src_rgb{}, dst_rgb{}, src_alpha{}, dst_alpha{}, equation_rgb{}, equation_alpha{};
    bool scissored{}, blended{};
    StageState() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_VIEWPORT, viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
        glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb); glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation_rgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha);
        scissored = glIsEnabled(GL_SCISSOR_TEST); blended = glIsEnabled(GL_BLEND);
    }
    void target() const {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        if (scissored) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }
    ~StageState() {
        target();
        glBlendFuncSeparate(static_cast<GLenum>(src_rgb), static_cast<GLenum>(dst_rgb), static_cast<GLenum>(src_alpha), static_cast<GLenum>(dst_alpha));
        glBlendEquationSeparate(static_cast<GLenum>(equation_rgb), static_cast<GLenum>(equation_alpha));
        if (blended) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glClearColor(clear[0],clear[1],clear[2],clear[3]);
    }
};
GLuint shader(GLenum kind, const char* source) {
    const GLuint id = glCreateShader(kind);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint success{};
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[4096]{};
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        glDeleteShader(id);
        throw std::runtime_error(std::string("Shader compilation: ") + log);
    }
    return id;
}
}
Texture::Texture(const Image& image) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    if (const auto error = glGetError(); error != GL_NO_ERROR) {
        glDeleteTextures(1, &id_);
        id_ = 0;
        throw std::runtime_error("Texture upload failed: OpenGL error " + std::to_string(error));
    }
}
Texture::~Texture() { if (id_) glDeleteTextures(1, &id_); }
Texture::Texture(Texture&& other) noexcept : id_(std::exchange(other.id_, 0)) {}
Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (id_) glDeleteTextures(1, &id_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}
Renderer::Target::~Target() { reset(); }
void Renderer::Target::reset() {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (texture) glDeleteTextures(1, &texture);
    framebuffer = texture = 0;
    width = height = 0;
}
void Renderer::Target::resize(int w, int h) {
    if (framebuffer && width == w && height == h) return;
    reset();
    try {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        checked_gl("Allocate stage texture");
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete stage framebuffer");
        checked_gl("Allocate stage framebuffer");
        width = w; height = h;
    } catch (...) { reset(); throw; }
}
Renderer::Renderer() {
    try {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        // GLEW loads core GL before its optional GLX extension pass. SDL can own
        // an EGL context (Wayland/X11-EGL); this renderer never uses GLX APIs.
        const bool core_without_glx = status == GLEW_ERROR_NO_GLX_DISPLAY &&
            SDL_GL_GetCurrentContext() && GLEW_VERSION_3_3;
        if (status != GLEW_OK && !core_without_glx)
            throw std::runtime_error("GLEW initialization (" + std::to_string(status) + "): " +
                                     reinterpret_cast<const char*>(glewGetErrorString(status)));
        if (core_without_glx) std::cerr << "[video] Using SDL EGL/core context without GLX extensions\n";
        // GLEW probes a legacy extension enum on some core contexts.
        while (glGetError() != GL_NO_ERROR) {}
        if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 Core is required");
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size_);
        std::cout << "OpenGL " << glGetString(GL_VERSION) << "; max_texture=" << max_texture_size_ << '\n';
        constexpr const char* vertex_source = R"(#version 330 core
layout(location = 0) in vec2 position;
uniform vec4 rect;
uniform int flip_y;
out vec2 uv;
void main() {
    vec2 p = rect.xy + position * rect.zw;
    gl_Position = vec4(p.x / 400.0 - 1.0, 1.0 - p.y / 300.0, 0.0, 1.0);
    uv = vec2(position.x, flip_y != 0 ? 1.0-position.y : position.y);
})";
        constexpr const char* fragment_source = R"(#version 330 core
in vec2 uv;
uniform sampler2D image;
uniform vec4 color;
out vec4 output_color;
void main() { output_color = texture(image, uv) * color; }
)";
        const auto vertex = shader(GL_VERTEX_SHADER, vertex_source);
        GLuint fragment{};
        try { fragment = shader(GL_FRAGMENT_SHADER, fragment_source); }
        catch (...) { glDeleteShader(vertex); throw; }
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint success{};
        glGetProgramiv(program_, GL_LINK_STATUS, &success);
        if (!success) throw std::runtime_error("Cannot link sprite shader");
        rect_uniform_ = glGetUniformLocation(program_, "rect");
        color_uniform_ = glGetUniformLocation(program_, "color");
        flip_uniform_ = glGetUniformLocation(program_, "flip_y");
        constexpr float vertices[] = {0,0, 1,0, 0,1, 0,1, 1,0, 1,1};
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "image"), 0);
        white_ = Texture(Image{1, 1, {255, 255, 255, 255}});
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
    } catch (...) { cleanup(); throw; }
}
void Renderer::cleanup() {
    previous_target_.reset(); current_target_.reset();
    if (program_) glDeleteProgram(program_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    program_ = vao_ = vbo_ = 0;
}
Renderer::~Renderer() { cleanup(); }
void Renderer::begin(int width, int height, Viewport view, Color background) {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(view.x, height - view.y - view.height, view.width, view.height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(view.x, height - view.y - view.height, view.width, view.height);
    glClearColor(background[0], background[1], background[2], background[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}
void Renderer::quad(const Texture& texture, float x, float y, float w, float h, Color color) {
    quad_id(texture.id(), x, y, w, h, color, false);
}
void Renderer::quad_id(GLuint texture, float x, float y, float w, float h, Color color, bool flip_y) {
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform4f(rect_uniform_, x, y, w, h);
    glUniform4fv(color_uniform_, 1, color.data());
    glUniform1i(flip_uniform_, flip_y ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::rectangle(const Layer& layer) { quad(white_, layer.x, layer.y, layer.width, layer.height, layer.color); }
void Renderer::clear_cache() {
    cache_.clear();
    cache_bytes_ = 0;
    rejected_.clear();
    reported_.clear();
    snapshot_valid_ = false;
    previous_target_.reset(); current_target_.reset();
}
void Renderer::opaque_scene(const Content& content, const std::vector<Layer>& layers, AssetTier tier) {
    glDisable(GL_BLEND);
    quad(white_,0,0,800,600,{0,0,0,1});
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    for (const auto& layer : layers) {
        switch (layer.kind) {
        case LayerKind::image: image(content,layer,tier); break;
        case LayerKind::rectangle: rectangle(layer); break;
        case LayerKind::text: break;
        default: throw std::runtime_error("Unknown stage layer kind");
        }
    }
}
void Renderer::stage(const Content& content, const std::vector<Layer>& previous, const std::vector<Layer>& current,
                     float progress, std::uint64_t revision, AssetTier tier) {
    if (!std::isfinite(progress)) throw std::runtime_error("Stage progress must be finite");
    checked_gl("Before stage composite");
    StageState state;
    const int width = state.viewport[2], height = state.viewport[3];
    if (width <= 0 || height <= 0) { snapshot_valid_ = false; return; }
    if (progress >= 1) {
        snapshot_valid_ = false;
        opaque_scene(content,current,tier);
        checked_gl("Draw current stage");
        return;
    }
    const auto bytes = static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height)*8;
    if (width > max_texture_size_ || height > max_texture_size_ || bytes > composite_budget_)
        throw std::runtime_error("Stage framebuffer exceeds GPU dimensions or 256 MiB composite budget");
    const bool rebuild = !snapshot_valid_ || snapshot_revision_ != revision || snapshot_tier_ != tier || snapshot_view_ != state.viewport;
    if (rebuild) snapshot_valid_ = false;
    previous_target_.resize(width,height);
    current_target_.resize(width,height);
    const auto render = [&](const Target& target, const std::vector<Layer>& layers) {
        glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
        glViewport(0,0,width,height);
        glDisable(GL_SCISSOR_TEST);
        opaque_scene(content,layers,tier);
        checked_gl("Compose stage scene");
    };
    if (rebuild) {
        render(previous_target_,previous);
        snapshot_revision_ = revision; snapshot_tier_ = tier; snapshot_view_ = state.viewport;
        snapshot_valid_ = true;
    }
    render(current_target_,current);
    state.target();
    // Both textures are opaque complete scenes; only the new scene's mix alpha
    // changes. Sampling an FBO needs the opposite Y direction from source PNGs.
    glDisable(GL_BLEND);
    quad_id(previous_target_.texture,0,0,800,600,{1,1,1,1},true);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    quad_id(current_target_.texture,0,0,800,600,{1,1,1,std::clamp(progress,0.0f,1.0f)},true);
    checked_gl("Blend complete stage scenes");
}
void Renderer::image(const Content& content, const Layer& layer, AssetTier tier) {
    for (const auto& candidate : content.candidates(layer.asset, tier)) {
        const auto path = content.resolve(candidate.path).string();
        if (rejected_.contains(path)) continue;
        auto found = cache_.find(path);
        if (found == cache_.end()) {
            try {
                const auto bytes = static_cast<std::size_t>(candidate.width) * static_cast<std::size_t>(candidate.height) * 4;
                if (candidate.width > max_texture_size_ || candidate.height > max_texture_size_ || bytes > budget_)
                    throw std::runtime_error("Image exceeds GPU texture limit or 256 MiB texture cache budget");
                const auto image = decode_image(path);
                if (image.width != candidate.width || image.height != candidate.height)
                    throw std::runtime_error("Decoded dimensions do not match the content manifest");
                while (cache_bytes_ + bytes > budget_ && !cache_.empty()) {
                    const auto victim = std::min_element(cache_.begin(), cache_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                    cache_bytes_ -= victim->second.bytes;
                    cache_.erase(victim);
                }
                found = cache_.emplace(path, Entry{Texture(image), bytes, ++clock_}).first;
                cache_bytes_ += bytes;
            } catch (const std::exception& e) {
                rejected_.insert(path);
                std::cerr << "[asset fallback] " << path << ": " << e.what() << '\n';
                continue;
            }
        }
        const auto report = layer.asset + ':' + std::to_string(static_cast<int>(tier));
        if (reported_.insert(report).second) {
            std::cout << "asset " << layer.asset << " requested=" << static_cast<int>(tier)
                      << " selected=" << candidate.tier << " pixels=" << candidate.width << 'x' << candidate.height
                      << " design=" << layer.width << 'x' << layer.height << '\n';
            if (candidate.tier != static_cast<int>(tier)) std::cerr << "[asset fallback] " << layer.asset << " uses tier " << candidate.tier << '\n';
        }
        found->second.used = ++clock_;
        quad(found->second.texture, layer.x, layer.y, layer.width, layer.height, layer.color);
        return;
    }
    throw std::runtime_error("No usable image variant for " + layer.asset);
}
void Renderer::capture(const std::filesystem::path& path, int width, int height) const {
    if (width <= 0 || height <= 0) throw std::runtime_error("Cannot capture an empty framebuffer");
    const auto stride = static_cast<std::size_t>(width) * 3;
    std::vector<unsigned char> pixels(stride * static_cast<std::size_t>(height));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Framebuffer read failed");
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y)
        output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(y) * stride), static_cast<std::streamsize>(stride));
    if (!output) throw std::runtime_error("Cannot write screenshot: " + path.string());
}
}
