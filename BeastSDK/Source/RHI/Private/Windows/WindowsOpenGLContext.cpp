#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/Windows/WindowsOpenGLFunctions.h"
#include <stdexcept>

namespace Beast::Detail
{
    class WindowsOpenGLContext final : public OpenGLContext
    {
        HMODULE library_ = nullptr;
        HWND window_ = nullptr;
        HDC dc_ = nullptr;
        HGLRC context_ = nullptr;
        WindowsOpenGLFunctions wgl_;
        void Cleanup() noexcept
        {
            if (context_) { wgl_.wglMakeCurrent(nullptr, nullptr); wgl_.wglDeleteContext(context_); context_ = nullptr; }
            if (dc_) { ReleaseDC(window_, dc_); dc_ = nullptr; }
            if (library_) { FreeLibrary(library_); library_ = nullptr; }
            wgl_.Reset();
        }
    public:
        WindowsOpenGLContext(const Window& window, RHIOptions options)
        {
            try
            {
                window_ = static_cast<HWND>(window.nativeHandle);
                library_ = LoadLibraryW(L"opengl32.dll");
                if (!library_) throw std::runtime_error("OpenGL loader opengl32.dll is unavailable.");
                wgl_.LoadLibraryFunctions(library_);
                dc_ = GetDC(window_);
                if (!dc_) throw std::runtime_error("Cannot acquire OpenGL window DC.");
                if (!GetPixelFormat(dc_))
                {
                    PIXELFORMATDESCRIPTOR format{};
                    format.nSize = sizeof(format); format.nVersion = 1;
                    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
                    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32; format.cAlphaBits = 8;
                    const int index = ChoosePixelFormat(dc_, &format);
                    if (!index || !SetPixelFormat(dc_, index, &format)) throw std::runtime_error("Cannot set OpenGL pixel format.");
                }
                context_ = wgl_.wglCreateContext(dc_);
                if (!context_) throw std::runtime_error("Cannot create bootstrap WGL context.");
                MakeCurrent();
                wgl_.LoadContextFunctions(dc_);
                const int attributes[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 6,
                    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
                const HGLRC upgraded = wgl_.wglCreateContextAttribsARB(dc_, nullptr, attributes);
                if (!upgraded) throw std::runtime_error("Beast requires OpenGL 4.6 core profile. Context creation failed; check GPU/driver support.");
                wgl_.wglMakeCurrent(nullptr, nullptr); wgl_.wglDeleteContext(context_); context_ = upgraded;
                MakeCurrent();
                wgl_.LoadContextFunctions(dc_);
                if (wgl_.wglSwapIntervalEXT) wgl_.wglSwapIntervalEXT(options.vsync ? 1 : 0);
            }
            catch (...) { Cleanup(); throw; }
        }
        ~WindowsOpenGLContext() override { Cleanup(); }
        void* Load(const char* name) override
        { return wgl_.Resolve(name); }
        void* LoadBase(const char* name) override
        { return wgl_.ResolveBase(name); }
        void MakeCurrent() override
        { if (!wgl_.wglMakeCurrent(dc_, context_)) throw std::runtime_error("wglMakeCurrent failed."); }
        bool Present() override
        { if (!SwapBuffers(dc_)) throw std::runtime_error("OpenGL SwapBuffers failed."); return true; }
        void GetSize(int& width, int& height) override
        { RECT rect{}; GetClientRect(window_, &rect); width = rect.right; height = rect.bottom; }
    };
    std::unique_ptr<OpenGLContext> CreateOpenGLContext(const Window& window, RHIOptions options)
    { return std::make_unique<WindowsOpenGLContext>(window, options); }
}
#endif
