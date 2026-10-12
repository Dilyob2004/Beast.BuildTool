#include "RHI/Private/RHIInternal.h"

namespace Beast
{
    std::unique_ptr<RHI> RHI::Create(const Window& window, RHIOptions options)
    {
        if (!window.HasSurface()) throw std::invalid_argument("RHI requires a live native window.");
        switch (options.backend)
        {
        case RHIBackend::OpenGL: return Detail::CreateOpenGLRHI(window, options);
        case RHIBackend::Vulkan: return Detail::CreateVulkanRHI(window, options);
        default: throw std::invalid_argument("Unknown RHI backend.");
        }
    }
}
