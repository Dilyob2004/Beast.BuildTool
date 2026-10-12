#if defined(_WIN32)
#include "RHI/Private/Windows/WindowsOpenGLFunctions.h"
#include <stdexcept>
#include <string_view>

namespace Beast::Detail
{
    void WindowsOpenGLFunctions::LoadLibraryFunctions(HMODULE library)
    {
        Reset();
        if (!library) throw std::invalid_argument("A loaded opengl32.dll is required.");
        WindowsOpenGLFunctions loaded;
        loaded.library_ = library;
#define BEAST_LOAD_WGL(Type, Name) \
        loaded.Name = reinterpret_cast<Type>(GetProcAddress(library, #Name)); \
        if (!loaded.Name) throw std::runtime_error("Missing required WGL entry point: " #Name);
        BEAST_WGL_ENTRYPOINTS_DLL(BEAST_LOAD_WGL)
#undef BEAST_LOAD_WGL
        *this = loaded;
    }
    void* WindowsOpenGLFunctions::ResolveBase(const char* name) const
    { return library_ ? reinterpret_cast<void*>(GetProcAddress(library_, name)) : nullptr; }
    void* WindowsOpenGLFunctions::Resolve(const char* name) const
    {
        if (!wglGetProcAddress) return nullptr;
        auto pointer = wglGetProcAddress(name);
        // WGL drivers may use these sentinel values for unsupported functions.
        if (!pointer || pointer == reinterpret_cast<PROC>(1) || pointer == reinterpret_cast<PROC>(2) ||
            pointer == reinterpret_cast<PROC>(3) || pointer == reinterpret_cast<PROC>(-1))
            return ResolveBase(name);
        return reinterpret_cast<void*>(pointer);
    }
    void WindowsOpenGLFunctions::LoadContextFunctions(HDC dc)
    {
        wglCreateContextAttribsARB = nullptr;
        wglSwapIntervalEXT = nullptr;
        const auto create = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(Resolve("wglCreateContextAttribsARB"));
        if (!create) throw std::runtime_error("Missing required WGL entry point: wglCreateContextAttribsARB");
        const auto extensionsARB = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGARBPROC>(Resolve("wglGetExtensionsStringARB"));
        const auto extensionsEXT = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGEXTPROC>(Resolve("wglGetExtensionsStringEXT"));
        const char* extensions = extensionsARB ? extensionsARB(dc) : nullptr;
        if (!extensions && extensionsEXT) extensions = extensionsEXT();
        // A non-null extension function alone does not establish support.
        std::string_view names = extensions ? extensions : "";
        while (!names.empty())
        {
            const auto end = names.find(' ');
            if (names.substr(0, end) == "WGL_EXT_swap_control")
            {
                wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(Resolve("wglSwapIntervalEXT"));
                break;
            }
            if (end == std::string_view::npos) break;
            names.remove_prefix(end + 1);
        }
        wglCreateContextAttribsARB = create;
    }
}
#endif
