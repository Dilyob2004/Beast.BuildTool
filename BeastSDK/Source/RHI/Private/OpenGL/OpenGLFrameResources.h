#pragma once
#include "RHI/Private/OpenGL/OpenGLFunctions.h"

namespace Beast::Detail
{
    // Private window-sized target, not a public RHI texture/framebuffer resource.
    // All operations require the owning context current. The owner must Release
    // before destroying that context, or Abandon after the context was lost.
    class OpenGLFrameResources
    {
        GLuint framebuffer_ = 0, color_ = 0;
        GLsync submitted_ = nullptr;
        int width_ = 0, height_ = 0;
        bool recording_ = false;
        void Resize(const OpenGLFunctions& gl, int width, int height);
    public:
        OpenGLFrameResources() = default;
        OpenGLFrameResources(const OpenGLFrameResources&) = delete;
        OpenGLFrameResources& operator=(const OpenGLFrameResources&) = delete;
        // Requests a 1 ms wait for the previous frame; false lets the event loop run.
        // Does not resize/reuse the target until the previous fence has signaled.
        bool BeginFrame(const OpenGLFunctions& gl, int width, int height);
        // Copies the completed color target into the window backbuffer, inserts
        // a fence and flushes. The caller still owns SwapBuffers/eglSwapBuffers.
        void EndFrame(const OpenGLFunctions& gl);
        void Release(const OpenGLFunctions& gl) noexcept;
        // Never delete old names/sync objects in a replacement GL context.
        void Abandon() noexcept;
    };
}
