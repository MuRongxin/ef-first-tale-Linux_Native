#include "render/renderer.hpp"
#include "scene/color_filter.hpp"
#include "scene/transition.hpp"
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
uniform vec4 uv_rect;
out vec2 uv;
void main() {
    vec2 p = rect.xy + position * rect.zw;
    gl_Position = vec4(p.x / 400.0 - 1.0, 1.0 - p.y / 300.0, 0.0, 1.0);
    uv = uv_rect.xy + vec2(position.x, flip_y != 0 ? 1.0-position.y : position.y) * uv_rect.zw;
})";
        constexpr const char* fragment_source = R"(#version 330 core
in vec2 uv;
uniform sampler2D image;
uniform vec4 color;
uniform ivec4 hsv_matrix;
uniform int hsv_enabled;
uniform sampler2D old_image;
uniform sampler2D mask_image;
uniform int transition_enabled;
uniform float mask_phase;
uniform ivec4 transition_region;
uniform sampler2D overlay_image;
uniform int overlay_enabled;
uniform vec4 overlay_map;
uniform sampler2D screen_image;
uniform int screen_enabled;
uniform vec4 screen_map;
out vec4 output_color;
void main() {
    vec4 source = texture(image, uv);
    if (transition_enabled==2) {
        ivec2 pixels=textureSize(image,0);
        vec2 point=floor(vec2(uv.x,1.0-uv.y)*vec2(pixels));
        vec2 offset=vec2(transition_region.xy)*vec2(pixels)/vec2(800,600);
        vec2 extent=vec2(transition_region.zw)*vec2(pixels)/vec2(800,600);
        ivec2 sample_point=ivec2(floor(offset+point*extent/vec2(pixels)));
        sample_point=clamp(sample_point,ivec2(0),pixels-1);
        vec3 next=floor(texelFetch(image,ivec2(sample_point.x,pixels.y-1-sample_point.y),0).rgb*255.0+0.5);
        vec3 old=floor(texture(old_image,uv).rgb*255.0+0.5);
        output_color=vec4(floor((old+next)/2.0)/255.0,1);
        return;
    }
    if (transition_enabled==14 || transition_enabled==16 || transition_enabled==17) {
        vec2 point=vec2(uv.x,1.0-uv.y);
        vec2 extent=vec2(transition_region.zw);
        if (any(lessThanEqual(extent,vec2(0))) || any(greaterThanEqual(point*vec2(800,600),extent))) {
            output_color=vec4(texture(old_image,uv).rgb,1);return;
        }
        ivec2 pixels=textureSize(image,0);
        vec2 pixel=floor(point*vec2(pixels));
        vec2 target=extent*vec2(pixels)/vec2(800,600);
        ivec2 sample_point=clamp(ivec2(floor(pixel*vec2(pixels)/target)),ivec2(0),pixels-1);
        output_color=vec4(texelFetch(image,ivec2(sample_point.x,pixels.y-1-sample_point.y),0).rgb,1);
        return;
    }
    if (transition_enabled == 1) {
        vec3 old=floor(texture(old_image,uv).rgb*255.0+0.5);
        vec3 next=floor(source.rgb*255.0+0.5);
        // The native mask loader inverts RGB before the transition reads its blue byte.
        float mask=255.0-floor(texture(mask_image,vec2(uv.x,1.0-uv.y)).b*255.0+0.5);
        float amount=clamp(mask+mask_phase-256.0,0.0,255.0);
        output_color=vec4((old+floor((next-old)*amount/256.0))/255.0,1);
        return;
    }
    if (hsv_enabled!=0 && source.a>0.0) {
        ivec3 a=ivec3(floor(clamp(source.rgb,0.0,1.0)*255.0+0.5));
        int luma_term=a.r*hsv_matrix.x+a.g*hsv_matrix.y+a.b*hsv_matrix.z;
        source.rgb=vec3(clamp((a*hsv_matrix.w+ivec3(luma_term))>>16,ivec3(0),ivec3(255)))/255.0;
    }
    if (overlay_enabled != 0) {
        float row=(uv.y*overlay_map.y+overlay_map.z)*overlay_map.w;
        vec3 a=floor(clamp(source.rgb,0.0,1.0)*255.0+0.5);
        vec3 b=floor(clamp(texture(overlay_image,vec2(overlay_map.x,row)).rgb,0.0,1.0)*255.0+0.5);
        vec3 low=min(vec3(255),2.0*floor((a*b+255.0)/256.0));
        vec3 high=255.0-min(vec3(255),2.0*floor(((255.0-a)*(255.0-b)+255.0)/256.0));
        source.rgb=mix(low,high,step(vec3(128),a))/255.0;
    }
    if (screen_enabled != 0) {
        // Strip and source share their bottom edge; CLAMP_TO_EDGE repeats the first row above it.
        float row = (uv.y * screen_map.y + screen_map.z) * screen_map.w;
        vec3 strip = texture(screen_image, vec2(screen_map.x, row)).rgb;
        vec3 a = floor(clamp(source.rgb, 0.0, 1.0) * 255.0 + 0.5);
        vec3 b = floor(clamp(strip, 0.0, 1.0) * 255.0 + 0.5);
        source.rgb = (254.0 - floor((255.0-a)*(255.0-b)/256.0)) / 255.0;
    }
    output_color = source * color;
}
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
        uv_uniform_ = glGetUniformLocation(program_, "uv_rect");
        screen_uniform_=glGetUniformLocation(program_,"screen_enabled");
        screen_map_uniform_=glGetUniformLocation(program_,"screen_map");
        hsv_uniform_=glGetUniformLocation(program_,"hsv_matrix");
        hsv_enabled_=glGetUniformLocation(program_,"hsv_enabled");
        transition_uniform_=glGetUniformLocation(program_,"transition_enabled");
        phase_uniform_=glGetUniformLocation(program_,"mask_phase");
        region_uniform_=glGetUniformLocation(program_,"transition_region");
        overlay_uniform_=glGetUniformLocation(program_,"overlay_enabled");
        overlay_map_uniform_=glGetUniformLocation(program_,"overlay_map");
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
        glUniform1i(glGetUniformLocation(program_, "screen_image"), 1);
        glUniform1i(glGetUniformLocation(program_, "overlay_image"), 2);
        glUniform1i(glGetUniformLocation(program_, "old_image"), 3);
        glUniform1i(glGetUniformLocation(program_, "mask_image"), 4);
        white_ = Texture(Image{1, 1, {255, 255, 255, 255}});
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
    } catch (...) { cleanup(); throw; }
}
void Renderer::cleanup() {
    transition_history_.reset();transition_next_.reset();
    shake_source_.reset(); shake_history_.reset(); shake_next_.reset();
    if (shake_program_) glDeleteProgram(shake_program_);
    shake_program_=0;
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
void Renderer::quad_id(GLuint texture, float x, float y, float w, float h, Color color, bool flip_y, std::array<float,4> uv, GLuint screen, std::array<float,4> screen_map, GLuint overlay, std::array<float,4> overlay_map, std::array<int,3> hsv, GLuint old, GLuint mask, float phase,int transition_type,std::array<int,4> region) {
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(screen_uniform_,screen ? 1 : 0);
    if (screen) {
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,screen);
        glUniform4fv(screen_map_uniform_,1,screen_map.data());
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1i(overlay_uniform_,overlay ? 1 : 0);
    if (overlay) {
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,overlay);
        glUniform4fv(overlay_map_uniform_,1,overlay_map.data());
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1i(hsv_enabled_,hsv!=std::array<int,3>{} ? 1 : 0);
    const auto matrix=color_filter_coefficients(hsv);
    glUniform4iv(hsv_uniform_,1,matrix.data());
    glUniform1i(transition_uniform_,mask ? 1 : transition_type);
    if (mask || transition_type) {
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,old);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,mask);
        glActiveTexture(GL_TEXTURE0); glUniform1f(phase_uniform_,phase);
        glUniform4iv(region_uniform_,1,region.data());
    }
    glUniform4f(rect_uniform_, x, y, w, h);
    glUniform4fv(color_uniform_, 1, color.data());
    glUniform1i(flip_uniform_, flip_y ? 1 : 0);
    glUniform4fv(uv_uniform_,1,uv.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::rectangle(const Layer& layer) { quad(white_, layer.x, layer.y, layer.width, layer.height, layer.color); }
void Renderer::clear_cache() {
    transition_history_valid_=false;transition_history_.reset();transition_next_.reset();
    shake_valid_=false;
    shake_source_.reset(); shake_history_.reset(); shake_next_.reset();
    cache_.clear(); strips_.clear();
    cache_bytes_ = strip_bytes_ = 0;
    rejected_.clear(); strip_rejected_.clear();
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
                     float progress, std::uint64_t revision, AssetTier tier, const std::string& mask,int transition_type) {
    if (!std::isfinite(progress)) throw std::runtime_error("Stage progress must be finite");
    if (!supported_transition(transition_type) || (!mask.empty() && transition_type>1) || (transition_type==1 && mask.empty()))
        throw std::runtime_error("Invalid stage transition configuration");
    const int effect=mask.empty() ? transition_type : 1;
    checked_gl("Before stage composite");
    StageState state;
    const int width = state.viewport[2], height = state.viewport[3];
    if (width <= 0 || height <= 0) { snapshot_valid_ = false; return; }
    if (effect!=2) {
        transition_history_valid_=false;transition_history_.reset();transition_next_.reset();
    }
    if (progress >= 1) {
        if (effect==2 && snapshot_valid_ && transition_history_valid_ && snapshot_revision_==revision && snapshot_tier_==tier && snapshot_view_==state.viewport) {
            glDisable(GL_BLEND);quad_id(current_target_.texture,0,0,800,600,{1,1,1,1},true);
        } else opaque_scene(content,current,tier);
        transition_history_valid_=false;transition_history_.reset();transition_next_.reset();
        snapshot_valid_ = false;
        checked_gl("Draw current stage");
        return;
    }
    const auto bytes = static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height)*(effect==2 ? 16 : 8);
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
    if (effect!=2 || rebuild || !transition_history_valid_) render(current_target_,current);
    const int step=static_cast<int>(std::floor(std::max(0.0f,progress)*static_cast<float>(transition_completion(effect))+.0001f));
    if (effect==2) {
        // Native updates are gated by gfx.transitionDrawInterval (default 5 ticks, 0x53adc0);
        // with the only corpus speed=20 the counter advances speed/2 per update -> 10 per sample.
        const int sample=step/10;
        if (rebuild || !transition_history_valid_ || sample<transition_sample_) {
            transition_history_valid_=false;
            transition_history_.resize(width,height);transition_next_.resize(width,height);
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_target_.framebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,transition_history_.framebuffer);
            glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            transition_sample_=0;transition_history_valid_=true;
        }
        while (transition_sample_<sample) {
            ++transition_sample_;
            const int phase=transition_sample_*10;
            glBindFramebuffer(GL_FRAMEBUFFER,transition_next_.framebuffer);
            glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
            quad_id(phase<200 ? previous_target_.texture : current_target_.texture,0,0,800,600,{1,1,1,1},true,
                    {0,0,1,1},0,{},0,{}, {},transition_history_.texture,0,0,2,zoom_transition_region(phase));
            std::swap(transition_history_.texture,transition_next_.texture);
            std::swap(transition_history_.framebuffer,transition_next_.framebuffer);
        }
        state.target();glDisable(GL_BLEND);
        quad_id(transition_history_.texture,0,0,800,600,{1,1,1,1},true);
        checked_gl("Zoom history transition");return;
    }
    state.target();
    if (effect==14 || effect==16 || effect==17) {
        glDisable(GL_BLEND);
        quad_id(current_target_.texture,0,0,800,600,{1,1,1,1},true,{0,0,1,1},0,{},0,{}, {},previous_target_.texture,0,0,effect,transition_region(effect,step));
        checked_gl("Expansion transition");return;
    }
    if (!mask.empty()) {
        const auto mask_id=texture(content,mask,tier).texture.id();
        glDisable(GL_BLEND);
        quad_id(current_target_.texture,0,0,800,600,{1,1,1,1},true,{0,0,1,1},0,{},0,{}, {},previous_target_.texture,mask_id,std::floor(progress*512+0.0001f));
        checked_gl("Masked scene transition");
        return;
    }
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
Renderer::Entry& Renderer::texture(const Content& content,const std::string& asset,AssetTier tier,bool strip,GLuint pinned) {
    auto& cache=strip ? strips_ : cache_;
    auto& rejected=strip ? strip_rejected_ : rejected_;
    auto& cache_bytes=strip ? strip_bytes_ : cache_bytes_;
    const auto budget=strip ? strip_budget_ : budget_;
    for (const auto& candidate : content.candidates(asset, tier)) {
        const auto path = content.resolve(candidate.path).string();
        if (rejected.contains(path)) continue;
        auto found = cache.find(path);
        if (found == cache.end()) {
            bool capacity_conflict=false;
            try {
                const auto bytes = static_cast<std::size_t>(candidate.width) * static_cast<std::size_t>(candidate.height) * 4;
                if (candidate.width > max_texture_size_ || candidate.height > max_texture_size_ || bytes > budget)
                    throw std::runtime_error("Image exceeds GPU texture dimensions or cache budget");
                const auto image = decode_image(path);
                if (image.width != candidate.width || image.height != candidate.height)
                    throw std::runtime_error("Decoded dimensions do not match the content manifest");
                while (cache_bytes + bytes > budget && !cache.empty()) {
                    auto victim=cache.end();
                    for (auto it=cache.begin();it!=cache.end();++it)
                        if (it->second.texture.id()!=pinned && (victim==cache.end() || it->second.used<victim->second.used)) victim=it;
                    if (victim==cache.end()) { capacity_conflict=true; throw std::runtime_error("Combined filter images exceed strip cache budget"); }
                    cache_bytes -= victim->second.bytes;
                    cache.erase(victim);
                }
                found = cache.emplace(path, Entry{Texture(image), bytes, ++clock_}).first;
                cache_bytes += bytes;
            } catch (const std::exception& e) {
                if (!capacity_conflict) rejected.insert(path);
                std::cerr << "[asset fallback] " << path << ": " << e.what() << '\n';
                continue;
            }
        }
        const auto report = asset + ':' + std::to_string(static_cast<int>(tier));
        if (reported_.insert(report).second) {
            std::cout << "asset " << asset << " requested=" << static_cast<int>(tier)
                      << " selected=" << candidate.tier << " pixels=" << candidate.width << 'x' << candidate.height
                      << " design=" << content.asset(asset).width << 'x' << content.asset(asset).height << '\n';
            if (candidate.tier != static_cast<int>(tier)) std::cerr << "[asset fallback] " << asset << " uses tier " << candidate.tier << '\n';
        }
        found->second.used = ++clock_;
        return found->second;
    }
    throw std::runtime_error("No usable image variant for " + asset);
}
void Renderer::image(const Content& content,const Layer& layer,AssetTier tier) {
    auto& primary=texture(content,layer.asset,tier);
    GLuint screen{}; std::array<float,4> screen_map{};
    if (!layer.screen.empty()) {
        const auto& source=content.asset(layer.asset);
        const auto& strip=content.asset(layer.screen);
        screen=texture(content,layer.screen,tier,true).texture.id();
        screen_map={0.5f/static_cast<float>(strip.width),static_cast<float>(source.height),
                    static_cast<float>(strip.height-source.height),1.0f/static_cast<float>(strip.height)};
    }
    GLuint overlay{}; std::array<float,4> overlay_map{};
    if (!layer.overlay.empty()) {
        const auto& source=content.asset(layer.asset);
        const auto& strip=content.asset(layer.overlay);
        overlay=texture(content,layer.overlay,tier,true,screen).texture.id();
        overlay_map={0.5f/static_cast<float>(strip.width),static_cast<float>(source.height),
                    static_cast<float>(strip.height-source.height),1.0f/static_cast<float>(strip.height)};
    }
    std::array<float,4> uv{0,0,1,1};
    if (layer.source) {
        const auto& asset = content.asset(layer.asset);
        const auto& source = *layer.source;
        if (!std::all_of(source.begin(),source.end(),[](float n) { return std::isfinite(n); }) ||
            source[0]<0 || source[1]<0 || source[2]<=0 || source[3]<=0 ||
            source[0]+source[2]>static_cast<float>(asset.width) || source[1]+source[3]>static_cast<float>(asset.height))
            throw std::runtime_error("Image atlas region outside design canvas");
        uv = {source[0]/static_cast<float>(asset.width),source[1]/static_cast<float>(asset.height),source[2]/static_cast<float>(asset.width),source[3]/static_cast<float>(asset.height)};
    }
    quad_id(primary.texture.id(),layer.x,layer.y,layer.width,layer.height,layer.color,false,uv,screen,screen_map,overlay,overlay_map,layer.hsv);
}
void Renderer::shake(const ShakeFrame& frame,std::uint64_t serial) {
    if (!std::isfinite(frame.x) || !std::isfinite(frame.y) || !std::isfinite(frame.crop) ||
        !std::isfinite(frame.angle) || frame.crop<0 || frame.crop>=300)
        throw std::runtime_error("Invalid shake frame");
    StageState state;
    const int w=state.viewport[2],h=state.viewport[3];
    if (w<=0 || h<=0) return;
    if (w>max_texture_size_ || h>max_texture_size_ || static_cast<std::uint64_t>(w)*static_cast<std::uint64_t>(h)*12>composite_budget_)
        throw std::runtime_error("Shake framebuffer exceeds GPU dimensions or 256 MiB budget");
    if (!shake_program_) {
        constexpr auto vertex=R"(#version 330 core
layout(location=0) in vec2 position;
out vec2 uv;
void main() { uv=position; gl_Position=vec4(position.x*2.0-1.0,1.0-position.y*2.0,0,1); }
)";
        constexpr auto fragment=R"(#version 330 core
in vec2 uv;
uniform sampler2D original_image;
uniform sampler2D history_image;
uniform int kind;
uniform vec4 transform;
uniform int accumulate;
out vec4 output_color;
void main() {
    vec2 p=uv*vec2(800,600);
    if (kind==1) p-=transform.xy;
    if (kind==2) p=vec2(transform.z)+uv*(vec2(800,600)-2.0*transform.z);
    if (kind==3) {
        float a=radians(transform.w);
        float c=trunc(cos(a)*1024.0)/1024.0, s=trunc(sin(a)*1024.0)/1024.0;
        vec2 d=p-vec2(400,300);
        p=vec2(c*d.x-s*d.y,s*d.x+c*d.y)+vec2(400,300);
    }
    vec3 value=vec3(0);
    if (all(greaterThanEqual(p,vec2(0))) && all(lessThan(p,vec2(800,600))))
        value=texture(original_image,vec2(p.x/800.0,1.0-p.y/600.0)).rgb;
    if (accumulate!=0) {
        vec3 old=texture(history_image,vec2(uv.x,1.0-uv.y)).rgb;
        value=floor((floor(value*255.0+0.5)+floor(old*255.0+0.5))/2.0)/255.0;
    }
    output_color=vec4(value,1);
}
)";
        const auto vs=shader(GL_VERTEX_SHADER,vertex);
        GLuint fs{};
        try { fs=shader(GL_FRAGMENT_SHADER,fragment); }
        catch (...) { glDeleteShader(vs); throw; }
        shake_program_=glCreateProgram();
        glAttachShader(shake_program_,vs); glAttachShader(shake_program_,fs);
        glLinkProgram(shake_program_); glDeleteShader(vs); glDeleteShader(fs);
        GLint success{}; glGetProgramiv(shake_program_,GL_LINK_STATUS,&success);
        if (!success) { glDeleteProgram(shake_program_); shake_program_=0; throw std::runtime_error("Cannot link shake shader"); }
    }
    glActiveTexture(GL_TEXTURE0);
    const bool capture=!shake_valid_ || shake_serial_!=serial || shake_view_!=state.viewport;
    if (capture) {
        shake_valid_=false;
        for (auto* target : {&shake_source_,&shake_history_,&shake_next_}) {
            target->resize(w,h);
            glBindTexture(GL_TEXTURE_2D,target->texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(state.draw));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,shake_source_.framebuffer);
        glBlitFramebuffer(state.viewport[0],state.viewport[1],state.viewport[0]+w,state.viewport[1]+h,
                          0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,shake_source_.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,shake_history_.framebuffer);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        checked_gl("Capture shake canvas");
        shake_serial_=serial; shake_view_=state.viewport; shake_sample_=~std::uint64_t{0}; shake_valid_=true;
    }
    const auto draw=[&](bool accumulate) {
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glUseProgram(shake_program_); glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,shake_source_.texture);
        glUniform1i(glGetUniformLocation(shake_program_,"original_image"),0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,shake_history_.texture);
        glUniform1i(glGetUniformLocation(shake_program_,"history_image"),1);
        glUniform1i(glGetUniformLocation(shake_program_,"accumulate"),accumulate ? 1 : 0);
        glUniform1i(glGetUniformLocation(shake_program_,"kind"),static_cast<int>(frame.kind));
        glUniform4f(glGetUniformLocation(shake_program_,"transform"),frame.x,frame.y,frame.crop,frame.angle);
        glDrawArrays(GL_TRIANGLES,0,6);
        glActiveTexture(GL_TEXTURE0);
    };
    if (frame.kind==ShakeKind::rotation && frame.sample!=0) {
        if (shake_sample_!=frame.sample) {
            glBindFramebuffer(GL_FRAMEBUFFER,shake_next_.framebuffer); glViewport(0,0,w,h);
            draw(true);
            std::swap(shake_history_.framebuffer,shake_next_.framebuffer);
            std::swap(shake_history_.texture,shake_next_.texture);
            shake_sample_=frame.sample;
        }
        state.target(); glDisable(GL_BLEND);
        quad_id(shake_history_.texture,0,0,800,600,{1,1,1,1},true);
    } else {
        state.target(); draw(false);
    }
    checked_gl("Render shake canvas");
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
