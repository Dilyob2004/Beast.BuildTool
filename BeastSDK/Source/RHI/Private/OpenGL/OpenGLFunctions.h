#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GL/glcorearb.h>
#elif defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#error Unsupported OpenGL platform.
#endif

// Single source of truth for the entry points currently used by Beast.
// Base GL functions come directly from opengl32.dll on Windows.
#define BEAST_GL_ENTRYPOINTS_BASE(X) \
    X(PFNGLCLEARCOLORPROC, glClearColor) \
    X(PFNGLCLEARPROC, glClear) \
    X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLSCISSORPROC, glScissor) \
    X(PFNGLVIEWPORTPROC, glViewport) \
    X(PFNGLGETERRORPROC, glGetError) \
    X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLREADBUFFERPROC, glReadBuffer) \
    X(PFNGLFLUSHPROC, glFlush)

// Required by our GL 4.6 / GLES 3.0 baseline, resolved with a current context.
#define BEAST_GL_ENTRYPOINTS_CORE(X) \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLFENCESYNCPROC, glFenceSync) \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync) \
    X(PFNGLDELETESYNCPROC, glDeleteSync)

#define BEAST_GL_ENTRYPOINTS_ALL(X) \
    BEAST_GL_ENTRYPOINTS_BASE(X) \
    BEAST_GL_ENTRYPOINTS_CORE(X)

namespace Beast::Detail
{
    class OpenGLContext;
    struct OpenGLFunctions
    {
#define BEAST_DECLARE_GL(Type, Name) Type Name = nullptr;
        BEAST_GL_ENTRYPOINTS_ALL(BEAST_DECLARE_GL)
#undef BEAST_DECLARE_GL
        // Call only with the owning context current. Missing required entries
        // throw by name and leave the entire table empty, not partially loaded.
        void Load(OpenGLContext& context);
        void Reset() noexcept { *this = {}; }
    };
}
