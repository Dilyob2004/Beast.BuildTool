#include "RHI/Private/OpenGL/OpenGLFrameResources.h"
#include <stdexcept>
#include <string>

namespace Beast::Detail
{
    namespace
    {
        void CheckError(const OpenGLFunctions& gl, const char* operation)
        {
            const GLenum error = gl.glGetError();
            if (error != GL_NO_ERROR)
                throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(error));
        }
    }
    void OpenGLFrameResources::Resize(const OpenGLFunctions& gl, int width, int height)
    {
        if (framebuffer_ && width == width_ && height == height_) return;
        GLint limit = 0;
        gl.glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &limit);
        if (width > limit || height > limit)
            throw std::runtime_error("OpenGL window target exceeds GL_MAX_RENDERBUFFER_SIZE: " + std::to_string(limit));

        GLuint framebuffer = 0, color = 0;
        try
        {
            gl.glGenRenderbuffers(1, &color);
            if (!color) throw std::runtime_error("Cannot allocate OpenGL color renderbuffer.");
            gl.glBindRenderbuffer(GL_RENDERBUFFER, color);
            gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            gl.glGenFramebuffers(1, &framebuffer);
            if (!framebuffer) throw std::runtime_error("Cannot allocate OpenGL framebuffer.");
            gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
            const GLenum attachment = GL_COLOR_ATTACHMENT0;
            gl.glDrawBuffers(1, &attachment);
            gl.glReadBuffer(attachment);
            const GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
            CheckError(gl, "Creating OpenGL window framebuffer");
            if (status != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Incomplete OpenGL window framebuffer: " + std::to_string(status));
            gl.glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
        catch (...)
        {
            gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.glBindRenderbuffer(GL_RENDERBUFFER, 0);
            if (framebuffer) gl.glDeleteFramebuffers(1, &framebuffer);
            if (color) gl.glDeleteRenderbuffers(1, &color);
            throw;
        }
        // Commit only a complete replacement. The old target survives allocation failure.
        if (framebuffer_) gl.glDeleteFramebuffers(1, &framebuffer_);
        if (color_) gl.glDeleteRenderbuffers(1, &color_);
        framebuffer_ = framebuffer; color_ = color;
        width_ = width; height_ = height;
    }
    bool OpenGLFrameResources::BeginFrame(const OpenGLFunctions& gl, int width, int height)
    {
        if (recording_) throw std::logic_error("OpenGL frame is already recording.");
        if (width <= 0 || height <= 0) return false;
        if (submitted_)
        {
            const GLenum status = gl.glClientWaitSync(submitted_, 0, 1000000);
            if (status == GL_TIMEOUT_EXPIRED) return false;
            if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
                throw std::runtime_error("OpenGL frame fence wait failed: " + std::to_string(gl.glGetError()));
            gl.glDeleteSync(submitted_); submitted_ = nullptr;
        }
        Resize(gl, width, height);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        recording_ = true;
        return true;
    }
    void OpenGLFrameResources::EndFrame(const OpenGLFunctions& gl)
    {
        if (!recording_) throw std::logic_error("No OpenGL frame is recording.");
        recording_ = false;
        // A previous ClearRect must not clip the full-window copy.
        gl.glDisable(GL_SCISSOR_TEST);
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
        gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        const GLenum back = GL_BACK;
        gl.glDrawBuffers(1, &back);
        gl.glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CheckError(gl, "Rendering/blitting OpenGL frame");
        submitted_ = gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!submitted_) throw std::runtime_error("Cannot create OpenGL frame fence: " + std::to_string(gl.glGetError()));
        // Submit the fence even if the caller delays/skips presentation. No glFinish.
        gl.glFlush();
        CheckError(gl, "Submitting OpenGL frame fence");
    }
    void OpenGLFrameResources::Release(const OpenGLFunctions& gl) noexcept
    {
        // GL retains storage referenced by queued commands; deletion need not
        // synchronously drain the GPU. These objects never leave this context.
        if (submitted_) gl.glDeleteSync(submitted_);
        if (framebuffer_) gl.glDeleteFramebuffers(1, &framebuffer_);
        if (color_) gl.glDeleteRenderbuffers(1, &color_);
        Abandon();
    }
    void OpenGLFrameResources::Abandon() noexcept
    {
        framebuffer_ = color_ = 0; submitted_ = nullptr;
        width_ = height_ = 0; recording_ = false;
    }
}
