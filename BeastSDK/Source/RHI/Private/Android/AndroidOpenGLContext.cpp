#if defined(__ANDROID__)
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdexcept>
#include <string>

namespace Beast::Detail
{
    class AndroidOpenGLContext final : public OpenGLContext
    {
        void* eglLibrary_ = nullptr;
        void* glLibrary_ = nullptr;
        EGLDisplay display_ = EGL_NO_DISPLAY;
        EGLSurface surface_ = EGL_NO_SURFACE;
        EGLContext context_ = EGL_NO_CONTEXT;
        EGLConfig config_ = nullptr;
        ANativeWindow* window_;
        bool vsync_;
        std::uint64_t generation_ = 1;
#define BEAST_EGL_FUNCTION(name) decltype(&name) name##_ = nullptr;
        BEAST_EGL_FUNCTION(eglGetDisplay)
        BEAST_EGL_FUNCTION(eglInitialize)
        BEAST_EGL_FUNCTION(eglTerminate)
        BEAST_EGL_FUNCTION(eglBindAPI)
        BEAST_EGL_FUNCTION(eglChooseConfig)
        BEAST_EGL_FUNCTION(eglGetConfigAttrib)
        BEAST_EGL_FUNCTION(eglCreateContext)
        BEAST_EGL_FUNCTION(eglDestroyContext)
        BEAST_EGL_FUNCTION(eglCreateWindowSurface)
        BEAST_EGL_FUNCTION(eglDestroySurface)
        BEAST_EGL_FUNCTION(eglMakeCurrent)
        BEAST_EGL_FUNCTION(eglSwapBuffers)
        BEAST_EGL_FUNCTION(eglSwapInterval)
        BEAST_EGL_FUNCTION(eglQuerySurface)
        BEAST_EGL_FUNCTION(eglGetError)
        BEAST_EGL_FUNCTION(eglGetProcAddress)
#undef BEAST_EGL_FUNCTION
        void Check(EGLBoolean success, const char* operation)
        { if (!success) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(eglGetError_())); }
        template<class T> void LoadEGL(T& function, const char* name)
        {
            function = reinterpret_cast<T>(dlsym(eglLibrary_, name));
            if (!function) throw std::runtime_error(std::string("Missing EGL function: ") + name);
        }
        void CreateDrawable()
        {
            const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
            context_ = eglCreateContext_(display_, config_, EGL_NO_CONTEXT, attributes);
            if (context_ == EGL_NO_CONTEXT) Check(EGL_FALSE, "eglCreateContext ES3");
            surface_ = eglCreateWindowSurface_(display_, config_, window_, nullptr);
            if (surface_ == EGL_NO_SURFACE) Check(EGL_FALSE, "eglCreateWindowSurface");
            MakeCurrent(); Check(eglSwapInterval_(display_, vsync_ ? 1 : 0), "eglSwapInterval");
        }
        void DestroyDrawable() noexcept
        {
            if (display_ == EGL_NO_DISPLAY) return;
            if (context_ != EGL_NO_CONTEXT) ++generation_;
            eglMakeCurrent_(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface_(display_, surface_);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext_(display_, context_);
            surface_ = EGL_NO_SURFACE; context_ = EGL_NO_CONTEXT;
        }
        void Cleanup() noexcept
        {
            DestroyDrawable();
            if (display_ != EGL_NO_DISPLAY) eglTerminate_(display_);
            display_ = EGL_NO_DISPLAY;
            if (glLibrary_) dlclose(glLibrary_);
            if (eglLibrary_) dlclose(eglLibrary_);
        }
    public:
        AndroidOpenGLContext(const Window& window, RHIOptions options)
            : window_(static_cast<ANativeWindow*>(window.nativeHandle)), vsync_(options.vsync)
        {
            try
            {
                eglLibrary_ = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
                glLibrary_ = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
                if (!eglLibrary_ || !glLibrary_) throw std::runtime_error("Android EGL/GLES loader unavailable.");
#define BEAST_LOAD_EGL(name) LoadEGL(name##_, #name);
                BEAST_LOAD_EGL(eglGetDisplay) BEAST_LOAD_EGL(eglInitialize) BEAST_LOAD_EGL(eglTerminate)
                BEAST_LOAD_EGL(eglBindAPI) BEAST_LOAD_EGL(eglChooseConfig) BEAST_LOAD_EGL(eglGetConfigAttrib)
                BEAST_LOAD_EGL(eglCreateContext) BEAST_LOAD_EGL(eglDestroyContext)
                BEAST_LOAD_EGL(eglCreateWindowSurface) BEAST_LOAD_EGL(eglDestroySurface)
                BEAST_LOAD_EGL(eglMakeCurrent) BEAST_LOAD_EGL(eglSwapBuffers) BEAST_LOAD_EGL(eglSwapInterval)
                BEAST_LOAD_EGL(eglQuerySurface) BEAST_LOAD_EGL(eglGetError) BEAST_LOAD_EGL(eglGetProcAddress)
#undef BEAST_LOAD_EGL
                display_ = eglGetDisplay_(EGL_DEFAULT_DISPLAY);
                if (display_ == EGL_NO_DISPLAY) Check(EGL_FALSE, "eglGetDisplay");
                Check(eglInitialize_(display_, nullptr, nullptr), "eglInitialize");
                Check(eglBindAPI_(EGL_OPENGL_ES_API), "eglBindAPI");
                const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
                EGLint count = 0;
                Check(eglChooseConfig_(display_, attributes, &config_, 1, &count), "eglChooseConfig");
                if (!count) throw std::runtime_error("No Android GLES3 window configuration.");
                EGLint format = 0; Check(eglGetConfigAttrib_(display_, config_, EGL_NATIVE_VISUAL_ID, &format), "eglGetConfigAttrib");
                if (ANativeWindow_setBuffersGeometry(window_, 0, 0, format) != 0)
                    throw std::runtime_error("Cannot set EGL native window format.");
                CreateDrawable();
            }
            catch (...) { Cleanup(); throw; }
        }
        ~AndroidOpenGLContext() override { Cleanup(); }
        void* Load(const char* name) override
        {
            if (void* pointer = dlsym(glLibrary_, name)) return pointer;
            return reinterpret_cast<void*>(eglGetProcAddress_(name));
        }
        void MakeCurrent() override { Check(eglMakeCurrent_(display_, surface_, surface_, context_), "eglMakeCurrent"); }
        bool Present() override
        {
            if (eglSwapBuffers_(display_, surface_)) return true;
            const auto error = eglGetError_();
            // RHI observes the generation change and abandons old FBO/sync handles.
            // The next frame recreates its private attachments in the new context.
            if (error == EGL_CONTEXT_LOST || error == EGL_BAD_SURFACE)
            { DestroyDrawable(); CreateDrawable(); return false; }
            throw std::runtime_error("eglSwapBuffers failed: " + std::to_string(error));
        }
        void GetSize(int& width, int& height) override
        {
            Check(eglQuerySurface_(display_, surface_, EGL_WIDTH, &width), "eglQuerySurface width");
            Check(eglQuerySurface_(display_, surface_, EGL_HEIGHT, &height), "eglQuerySurface height");
        }
        std::uint64_t GetGeneration() const noexcept override { return generation_; }
    };
    std::unique_ptr<OpenGLContext> CreateOpenGLContext(const Window& window, RHIOptions options)
    { return std::make_unique<AndroidOpenGLContext>(window, options); }
}
#endif
