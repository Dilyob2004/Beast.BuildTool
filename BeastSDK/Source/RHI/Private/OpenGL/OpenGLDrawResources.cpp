#include "RHI/Private/OpenGL/OpenGLDrawResources.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/RHIInternal.h"
#include <cstddef>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Beast::Detail
{
    namespace
    {
        void Check(const OpenGLFunctions& gl, const char* operation)
        {
            const GLenum error = gl.glGetError();
            if (error != GL_NO_ERROR) throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
        }
    }
    void OpenGLDrawResources::Initialize(OpenGLContext& context)
    {
        if (loaded_) return;
        Functions next;
        // Windows 1.1 exports (textures/draw) must come from opengl32.dll;
        // WindowsOpenGLContext::LoadBase also handles modern entry points.
#define BEAST_LOAD_DRAW(type, name) \
        next.name = reinterpret_cast<type>(context.LoadBase(#name)); \
        if (!next.name) next.name = reinterpret_cast<type>(context.Load(#name)); \
        if (!next.name) throw std::runtime_error("Missing OpenGL function: " #name);
        BEAST_GL_DRAW_FUNCTIONS(BEAST_LOAD_DRAW)
#undef BEAST_LOAD_DRAW
        fn_ = next;
        loaded_ = true;
        fn_.glGenVertexArrays(1, &vao_);
        fn_.glGenBuffers(1, &vertices_);
        if (!vao_ || !vertices_) { Release(); throw std::runtime_error("Cannot create GL vertex resources."); }
        static_assert(sizeof(RHIVertex) == 32 && offsetof(RHIVertex, color) == 16);
        fn_.glBindVertexArray(vao_);
        fn_.glBindBuffer(GL_ARRAY_BUFFER, vertices_);
        fn_.glEnableVertexAttribArray(0); fn_.glEnableVertexAttribArray(1); fn_.glEnableVertexAttribArray(2);
        fn_.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RHIVertex), nullptr);
        fn_.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RHIVertex), reinterpret_cast<const void*>(offsetof(RHIVertex, u)));
        fn_.glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RHIVertex), reinterpret_cast<const void*>(offsetof(RHIVertex, color)));
    }
    GLuint OpenGLDrawResources::Shader(const RHIShaderDesc& desc)
    {
#if defined(__ANDROID__)
        const auto& source = desc.glslES300;
#else
        const auto& source = desc.glsl460;
#endif
        if (source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) throw std::runtime_error("GLSL source is too large.");
        const GLuint shader = fn_.glCreateShader(desc.stage == RHIShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        if (!shader) throw std::runtime_error("Cannot create GL shader: " + desc.name);
        try
        {
            const char* text = source.c_str(); const GLint length = static_cast<GLint>(source.size());
            fn_.glShaderSource(shader, 1, &text, &length); fn_.glCompileShader(shader);
            GLint okay = 0; fn_.glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
            if (!okay)
            {
                GLint size = 0; fn_.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
                std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0');
                fn_.glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
                throw std::runtime_error("GLSL compile failed [" + desc.name + "]: " + log);
            }
            return shader;
        }
        catch (...) { fn_.glDeleteShader(shader); throw; }
    }
    GLuint OpenGLDrawResources::Pipeline(const RHIPipelineHandle& pipeline)
    {
        if (auto found = pipelines_.find(pipeline.get()); found != pipelines_.end()) return found->second.id;
        GLuint vertex = 0, fragment = 0, program = 0;
        try
        {
            const auto& desc = pipeline->GetDesc();
            vertex = Shader(desc.vertexShader->GetDesc()); fragment = Shader(desc.fragmentShader->GetDesc());
            program = fn_.glCreateProgram();
            if (!program) throw std::runtime_error("Cannot create GL program.");
            fn_.glAttachShader(program, vertex); fn_.glAttachShader(program, fragment); fn_.glLinkProgram(program);
            GLint okay = 0; fn_.glGetProgramiv(program, GL_LINK_STATUS, &okay);
            if (!okay)
            {
                GLint size = 0; fn_.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
                std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0');
                fn_.glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
                throw std::runtime_error("GLSL pipeline link failed: " + log);
            }
            const GLint sampler = fn_.glGetUniformLocation(program, desc.fragmentShader->GetDesc().combinedSampler.c_str());
            if (sampler < 0) throw std::runtime_error("GLSL combined sampler not found.");
            fn_.glUseProgram(program); fn_.glUniform1i(sampler, 0);
            const auto& uniform = desc.fragmentShader->GetDesc().constantsUniform;
            const GLint constants = uniform.empty() ? -1 : fn_.glGetUniformLocation(program, uniform.c_str());
            if (!uniform.empty() && constants < 0) throw std::runtime_error("GLSL draw constants not found: " + uniform);
            pipelines_.emplace(pipeline.get(), Native<RHIPipeline>{pipeline, program, constants});
        }
        catch (...)
        {
            if (program) fn_.glDeleteProgram(program);
            if (fragment) fn_.glDeleteShader(fragment);
            if (vertex) fn_.glDeleteShader(vertex);
            throw;
        }
        fn_.glDeleteShader(vertex); fn_.glDeleteShader(fragment);
        return program;
    }
    GLuint OpenGLDrawResources::Texture(const RHITextureHandle& texture, const OpenGLFunctions& gl)
    {
        if (auto found = textures_.find(texture.get()); found != textures_.end()) return found->second.id;
        const auto& desc = texture->GetDesc();
        GLint limit = 0; gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (desc.width > static_cast<std::uint32_t>(limit) || desc.height > static_cast<std::uint32_t>(limit))
            throw std::runtime_error("Texture exceeds GL_MAX_TEXTURE_SIZE.");
        NativeTexture native;
        if (texture->GetContent() && layerPool_.Take(desc.width, desc.height, native))
        {
            native.owner = texture; ++stats_.reuses;
            try { RenderLayer(native, texture, gl); textures_.emplace(texture.get(), native); return native.id; }
            catch (...) { Destroy(native); throw; }
        }
        GLuint id = 0; fn_.glGenTextures(1, &id);
        if (!id) throw std::runtime_error("Cannot create GL texture.");
        native.owner = texture; native.id = id; native.width = desc.width; native.height = desc.height;
        native.bytes = std::uint64_t(desc.width) * desc.height * 4;
        try
        {
            fn_.glActiveTexture(GL_TEXTURE0); fn_.glBindTexture(GL_TEXTURE_2D, id);
            const GLint filter = desc.filter == RHIFilter::Linear ? GL_LINEAR : GL_NEAREST;
            fn_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            fn_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            fn_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            fn_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            fn_.glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // RGBA8 rows are always multiples of four.
            fn_.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                0, GL_RGBA, GL_UNSIGNED_BYTE, texture->GetContent() ? nullptr : desc.pixels.data());
            Check(gl, "Texture upload");
            if (texture->GetContent()) { ++stats_.allocations; RenderLayer(native, texture, gl); }
            textures_.emplace(texture.get(), native);
            return id;
        }
        catch (...) { Destroy(native); throw; }
    }
    void OpenGLDrawResources::Destroy(NativeTexture& texture) noexcept
    {
        if (texture.framebuffer) fn_.glDeleteFramebuffers(1, &texture.framebuffer);
        if (texture.id) fn_.glDeleteTextures(1, &texture.id);
        texture = {};
    }
    void OpenGLDrawResources::RenderLayer(NativeTexture& texture, const RHITextureHandle& handle, const OpenGLFunctions& gl)
    {
        GLint read = 0, draw = 0, viewport[4]{}, scissor[4]{}, enabled = 0;
        gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read); gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        gl.glGetIntegerv(GL_VIEWPORT, viewport); gl.glGetIntegerv(GL_SCISSOR_BOX, scissor); gl.glGetIntegerv(GL_SCISSOR_TEST, &enabled);
        const bool fresh = !texture.framebuffer;
        if (fresh) fn_.glGenFramebuffers(1, &texture.framebuffer);
        const auto target = texture.framebuffer;
        const auto restore = [&] {
            fn_.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read)); fn_.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
            gl.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); gl.glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
            if (enabled) gl.glEnable(GL_SCISSOR_TEST); else gl.glDisable(GL_SCISSOR_TEST);
        };
        try
        {
            if (!target) throw std::runtime_error("Cannot allocate layer framebuffer.");
            fn_.glBindFramebuffer(GL_FRAMEBUFFER, target);
            if (fresh) {
                fn_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
                if (fn_.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Incomplete layer framebuffer.");
            }
            const int width = static_cast<int>(handle->GetDesc().width), height = static_cast<int>(handle->GetDesc().height);
            gl.glViewport(0, 0, width, height); gl.glDisable(GL_SCISSOR_TEST);
            const auto clear = handle->GetContent()->GetClearColor(); gl.glClearColor(clear.r, clear.g, clear.b, clear.a); gl.glClear(GL_COLOR_BUFFER_BIT);
            gl.glEnable(GL_SCISSOR_TEST);
            for (const auto& command : handle->GetContent()->GetCommands())
            {
                if (const auto* triangles = std::get_if<RHIDrawTriangles>(&command))
                {
                    const auto rect = Clip(triangles->scissor.value_or(RHIRect{0, 0, width, height}), width, height);
                    if (!rect.width || !rect.height) continue;
                    gl.glScissor(rect.x, rect.y, rect.width, rect.height); Draw(*triangles, gl, true);
                }
                else
                {
                    const auto& c = std::get<RHIClearRect>(command); const auto rect = Clip(c.rect, width, height);
                    gl.glScissor(rect.x, rect.y, rect.width, rect.height); gl.glClearColor(c.color.r, c.color.g, c.color.b, c.color.a); gl.glClear(GL_COLOR_BUFFER_BIT);
                }
            }
            Check(gl, "Render layer");
            ++stats_.renders;
        }
        catch (...) { restore(); throw; }
        restore();
    }
    void OpenGLDrawResources::Draw(const RHIDrawTriangles& draw, const OpenGLFunctions& gl, bool layer)
    {
        const GLuint program = Pipeline(draw.pipeline), texture = Texture(draw.texture, gl);
        fn_.glUseProgram(program); fn_.glActiveTexture(GL_TEXTURE0); fn_.glBindTexture(GL_TEXTURE_2D, texture);
        const GLint constants = pipelines_.at(draw.pipeline.get()).constants;
        if (constants >= 0) fn_.glUniform4fv(constants, 8, draw.constants.data());
        gl.glDisable(GL_DEPTH_TEST); gl.glDisable(GL_CULL_FACE);
        const auto blend = draw.pipeline->GetDesc().blend;
        if (blend == RHIBlend::Opaque) gl.glDisable(GL_BLEND);
        else
        {
            gl.glEnable(GL_BLEND); fn_.glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            fn_.glBlendFuncSeparate(blend == RHIBlend::StraightAlpha ? GL_SRC_ALPHA : GL_ONE,
                GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        fn_.glBindVertexArray(vao_); fn_.glBindBuffer(GL_ARRAY_BUFFER, vertices_);
        // Orphaning preserves earlier draws' storage without stalling the GPU.
        // Render textures store logical top row at UV=0 just like uploaded images.
        std::vector<RHIVertex> flipped;
        if (layer) { flipped = draw.vertices; for (auto& vertex : flipped) vertex.y = -vertex.y; }
        fn_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(draw.vertices.size() * sizeof(RHIVertex)), layer ? flipped.data() : draw.vertices.data(), GL_STREAM_DRAW);
        fn_.glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(draw.vertices.size()));
        Check(gl, "DrawTriangles");
    }
    void OpenGLDrawResources::Collect()
    {
        for (auto it = pipelines_.begin(); it != pipelines_.end();)
            if (it->second.owner.expired()) { fn_.glDeleteProgram(it->second.id); it = pipelines_.erase(it); } else ++it;
        for (auto it = textures_.begin(); it != textures_.end();)
            if (it->second.owner.expired()) {
                auto native = std::move(it->second); it = textures_.erase(it); native.owner.reset();
                if (native.framebuffer) layerPool_.Put(std::move(native), poolBudget_, stats_, [this](auto& t) { Destroy(t); });
                else Destroy(native);
            } else ++it;
    }
    void OpenGLDrawResources::Release() noexcept
    {
        if (loaded_)
        {
            fn_.glUseProgram(0); fn_.glBindVertexArray(0); fn_.glBindBuffer(GL_ARRAY_BUFFER, 0); fn_.glBindTexture(GL_TEXTURE_2D, 0);
            for (const auto& item : pipelines_) fn_.glDeleteProgram(item.second.id);
            for (auto& item : textures_) Destroy(item.second);
            layerPool_.Clear([this](auto& t) { Destroy(t); });
            if (vertices_) fn_.glDeleteBuffers(1, &vertices_);
            if (vao_) fn_.glDeleteVertexArrays(1, &vao_);
        }
        Abandon();
    }
    void OpenGLDrawResources::Abandon() noexcept
    { pipelines_.clear(); textures_.clear(); layerPool_.Abandon(); stats_ = {}; vao_ = vertices_ = 0; fn_ = {}; loaded_ = false; }
}
