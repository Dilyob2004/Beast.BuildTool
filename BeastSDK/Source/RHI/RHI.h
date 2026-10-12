#pragma once
#include "Core/Window.h"
#include "RHI/RHICommandList.h"
#include "RHI/RHIDiagnostics.h"
#include <memory>

namespace Beast
{
    // OpenGL requires desktop 4.6 core on Windows; Android uses OpenGL ES 3.0+.
    enum class RHIBackend { OpenGL, Vulkan };
    struct RHIOptions
    {
        RHIBackend backend = RHIBackend::OpenGL;
        bool vsync = true;
        bool validation = false; // Vulkan validation when installed; logged if unavailable.
        bool profiling = false; // Optional asynchronous GPU timestamps and CPU submit timer.
        std::uint64_t layerPoolBytes = 32ull * 1024 * 1024; // Idle storage only; zero disables pooling.
    };

    // Create/submit/destroy on the window's Run thread. Destroy before the
    // SurfaceDestroyed callback returns; recreate on the next SurfaceCreated.
    class RHI
    {
    public:
        virtual ~RHI() = default;
        static std::unique_ptr<RHI> Create(const Window& window, RHIOptions options = {});
        // Immutable, backend-independent handles. Native GPU objects are created
        // on first Submit and cached per RHI. Commands retain resource ownership.
        static RHIShaderHandle CreateShader(RHIShaderDesc desc);
        static RHITextureHandle CreateTexture(RHITextureDesc desc);
        // Immutable offscreen pass, rendered on first use and recreated after
        // surface/device loss. UV row zero is top on both backends. No CPU pixels.
        static RHITextureHandle CreateLayerTexture(std::uint32_t width, std::uint32_t height, RHICommandList content);
        static RHIPipelineHandle CreatePipeline(RHIPipelineDesc desc);
        virtual RHIBackend GetBackend() const noexcept = 0;
        virtual RHIFrameStats GetFrameStats() const noexcept { return {}; }
        // false means GPU work is still pending or the surface is resizing/recovering. Fatal
        // driver/device errors throw and are caught by the application loop.
        virtual bool Submit(const Window& window, const RHICommandList& commands) = 0;
    };
}
