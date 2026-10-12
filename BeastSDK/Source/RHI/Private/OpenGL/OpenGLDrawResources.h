#pragma once
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/RHICommandList.h"
#include <unordered_map>
#include "RHI/Private/LayerTargetPool.h"

// Kept separate from the clear/present table: clear-only applications do not
// initialize shader resources, and deterministic frame tests remain isolated.
#define BEAST_GL_DRAW_FUNCTIONS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM4FVPROC, glUniform4fv) X(PFNGLGENTEXTURESPROC, glGenTextures) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) X(PFNGLBINDTEXTUREPROC, glBindTexture) \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D) X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLDRAWARRAYSPROC, glDrawArrays) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)

namespace Beast::Detail
{
    class OpenGLDrawResources
    {
        struct Functions
        {
#define BEAST_DECLARE_DRAW(type, name) type name = nullptr;
            BEAST_GL_DRAW_FUNCTIONS(BEAST_DECLARE_DRAW)
#undef BEAST_DECLARE_DRAW
        } fn_;
        template<class T> struct Native { std::weak_ptr<const T> owner; GLuint id = 0; GLint constants = -1; };
        std::unordered_map<const RHIPipeline*, Native<RHIPipeline>> pipelines_;
        struct NativeTexture {
            std::weak_ptr<const RHITexture> owner;
            GLuint id = 0, framebuffer = 0;
            std::uint32_t width = 0, height = 0;
            std::uint64_t bytes = 0;
        };
        std::unordered_map<const RHITexture*, NativeTexture> textures_;
        LayerTargetPool<NativeTexture> layerPool_;
        std::uint64_t poolBudget_ = 32ull * 1024 * 1024;
        RHILayerStats stats_;
        GLuint vao_ = 0, vertices_ = 0;
        bool loaded_ = false;
        GLuint Shader(const RHIShaderDesc& desc);
        GLuint Pipeline(const RHIPipelineHandle& pipeline);
        GLuint Texture(const RHITextureHandle& texture, const OpenGLFunctions& gl);
        void RenderLayer(NativeTexture& texture, const RHITextureHandle& handle, const OpenGLFunctions& gl);
        void Destroy(NativeTexture& texture) noexcept;
    public:
        void Initialize(OpenGLContext& context);
        void Collect(); // Only after the previous frame fence has completed.
        void SetPoolBudget(std::uint64_t bytes) noexcept { poolBudget_ = bytes; }
        RHILayerStats GetStats() const noexcept { auto result = stats_; layerPool_.ReadStats(result); return result; }
        void Draw(const RHIDrawTriangles& draw, const OpenGLFunctions& gl, bool layer = false);
        void Release() noexcept;
        void Abandon() noexcept;
    };
}
