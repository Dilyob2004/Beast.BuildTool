#pragma once
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/glcorearb.h>
#include <GL/wglext.h>

#define BEAST_WGL_ENTRYPOINTS_DLL(X) \
    X(decltype(&::wglCreateContext), wglCreateContext) \
    X(decltype(&::wglDeleteContext), wglDeleteContext) \
    X(decltype(&::wglMakeCurrent), wglMakeCurrent) \
    X(decltype(&::wglGetProcAddress), wglGetProcAddress)

namespace Beast::Detail
{
    struct WindowsOpenGLFunctions
    {
#define BEAST_DECLARE_WGL(Type, Name) Type Name = nullptr;
        BEAST_WGL_ENTRYPOINTS_DLL(BEAST_DECLARE_WGL)
#undef BEAST_DECLARE_WGL
        PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
        PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr; // Optional.

        void LoadLibraryFunctions(HMODULE library);
        void LoadContextFunctions(HDC dc);
        void* Resolve(const char* name) const;
        void* ResolveBase(const char* name) const;
        void Reset() noexcept { *this = {}; }
    private:
        HMODULE library_ = nullptr; // Borrowed from the context owner.
    };
}
#endif
