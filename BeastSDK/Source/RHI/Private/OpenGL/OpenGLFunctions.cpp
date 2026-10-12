#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include <stdexcept>
#include <string>

namespace Beast::Detail
{
    void OpenGLFunctions::Load(OpenGLContext& context)
    {
        Reset();
        OpenGLFunctions loaded;
#define BEAST_LOAD_GL_BASE(Type, Name) \
        loaded.Name = reinterpret_cast<Type>(context.LoadBase(#Name)); \
        if (!loaded.Name) throw std::runtime_error("Missing required OpenGL entry point: " #Name);
        BEAST_GL_ENTRYPOINTS_BASE(BEAST_LOAD_GL_BASE)
#undef BEAST_LOAD_GL_BASE
#define BEAST_LOAD_GL_CORE(Type, Name) \
        loaded.Name = reinterpret_cast<Type>(context.Load(#Name)); \
        if (!loaded.Name) throw std::runtime_error("Missing required OpenGL entry point: " #Name);
        BEAST_GL_ENTRYPOINTS_CORE(BEAST_LOAD_GL_CORE)
#undef BEAST_LOAD_GL_CORE
        *this = loaded;
    }
}
