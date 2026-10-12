#pragma once
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/RHIDiagnostics.h"
#include <chrono>
#if defined(__ANDROID__)
#include <GLES2/gl2ext.h>
#endif

namespace Beast::Detail
{
    // Optional diagnostics, deliberately separate from required GL entry points.
    class OpenGLTimer
    {
        PFNGLGENQUERIESPROC generate_ = nullptr;
        PFNGLDELETEQUERIESPROC destroy_ = nullptr;
        PFNGLGETQUERYIVPROC query_ = nullptr;
        PFNGLGETQUERYOBJECTUIVPROC available_ = nullptr;
#if defined(__ANDROID__)
        PFNGLQUERYCOUNTEREXTPROC counter_ = nullptr;
        PFNGLGETQUERYOBJECTUI64VEXTPROC result_ = nullptr;
#else
        PFNGLQUERYCOUNTERPROC counter_ = nullptr;
        PFNGLGETQUERYOBJECTUI64VPROC result_ = nullptr;
#endif
        GLuint ids_[2]{};
        unsigned bits_ = 0;
        bool initialized_ = false, pending_ = false, recording_ = false, invalid_ = false;
        std::uint64_t frame_ = 0;
        std::chrono::steady_clock::time_point started_;
    public:
        void Initialize(OpenGLContext& context, const OpenGLFunctions& gl);
        void Resolve(const OpenGLFunctions& gl, RHIFrameStats& stats);
        void Begin(std::uint64_t frame);
        void End();
        void Release() noexcept;
        void Abandon() noexcept { *this = {}; }
    };
}
