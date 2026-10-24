#include "render/renderer.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace ef {
namespace {
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
Renderer::Renderer() {
    try {
        glewExperimental = GL_TRUE;
        const auto status = glewInit();
        if (status != GLEW_OK) throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(status)));
        // GLEW probes a legacy extension enum on some core contexts.
        while (glGetError() != GL_NO_ERROR) {}
        if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 Core is required");
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size_);
        std::cout << "OpenGL " << glGetString(GL_VERSION) << "; max_texture=" << max_texture_size_ << '\n';
        constexpr const char* vertex_source = R"(#version 330 core
layout(location = 0) in vec2 position;
uniform vec4 rect;
out vec2 uv;
void main() {
    vec2 p = rect.xy + position * rect.zw;
    gl_Position = vec4(p.x / 400.0 - 1.0, 1.0 - p.y / 300.0, 0.0, 1.0);
    uv = position;
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
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id());
    glUniform4f(rect_uniform_, x, y, w, h);
    glUniform4fv(color_uniform_, 1, color.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::rectangle(const Layer& layer) { quad(white_, layer.x, layer.y, layer.width, layer.height, layer.color); }
void Renderer::clear_cache() {
    cache_.clear();
    cache_bytes_ = 0;
    rejected_.clear();
    reported_.clear();
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
