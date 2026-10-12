#pragma once
#include "RHI/RHI.h"
#include <cstdint>

namespace Beast::Detail
{
    class OpenGLContext
    {
    public:
        virtual ~OpenGLContext() = default;
        virtual void* Load(const char* name) = 0;
        virtual void* LoadBase(const char* name) { return Load(name); }
        virtual void MakeCurrent() = 0;
        virtual bool Present() = 0;
        virtual void GetSize(int& width, int& height) = 0;
        // Changes before old GL objects become invalid, even if recovery throws.
        // A recovered context has an empty, unshared GL object namespace.
        virtual std::uint64_t GetGeneration() const noexcept { return 1; }
    };
    std::unique_ptr<OpenGLContext> CreateOpenGLContext(const Window&, RHIOptions);
}
