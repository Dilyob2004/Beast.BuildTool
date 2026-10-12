#include "Beast.h"

namespace
{
    // Change OpenGL to Vulkan here, then rebuild for Windows or Android.
    constexpr Beast::RHIBackend Backend = Beast::RHIBackend::Vulkan;
    constexpr Beast::RHIColor Background{0.1f, 0.1f, 0.1f, 1.0f};
}

class DemoAppApplication final : public Beast::Application
{
public:
    Beast::WindowDefinition Configure() const override
    {
        Beast::WindowDefinition window;
        window.title = Backend == Beast::RHIBackend::OpenGL ? "DemoApp - OpenGL" : "DemoApp - Vulkan";
        window.gpuRendering = true;
        return window;
    }

    void OnStart() override
    {
        Beast::Log(Backend == Beast::RHIBackend::OpenGL ? "DemoApp backend: OpenGL" : "DemoApp backend: Vulkan");
        // Constant color: record once and reuse the closed command list.
        commands_.ClearFrameBuffer(Background);
    }
    void OnStop() override { Beast::Log("DemoApp: stopped"); }

    void OnEvent(const Beast::Event& event) override
    {
        using Beast::EventType;
        switch (event.type)
        {
        case EventType::SurfaceCreated:
        {
            Beast::RHIOptions options;
            options.backend = Backend;
            options.vsync = true;
            options.validation = true; // Vulkan validation when installed.
            renderer_ = Beast::RHI::Create(GetWindow(), options);
            firstFrame_ = true;
            Beast::Log("DemoApp: RHI surface created");
            break;
        }
        case EventType::SurfaceDestroyed:
            renderer_.reset();
            Beast::Log("DemoApp: RHI surface destroyed");
            break;
        case EventType::KeyDown:
            if (event.key == Beast::Key::Escape) RequestExit();
            break;
        default: break;
        }
    }

    void OnUpdate(double) override
    {
        if (!renderer_ || !renderer_->Submit(GetWindow(), commands_)) return;
        if (firstFrame_)
        {
            Beast::Log("DemoApp: frame presented, ClearFrameBuffer(0.1, 0.1, 0.1, 1.0)");
            firstFrame_ = false;
        }
    }
private:
    Beast::RHICommandList commands_;
    std::unique_ptr<Beast::RHI> renderer_;
    bool firstFrame_ = true;
};

BEAST_APPLICATION(DemoAppApplication)
