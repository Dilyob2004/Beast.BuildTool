#include "Beast.h"
#include "Launch/ResourceScene.h"

namespace
{
    // Select the same backend for Windows and Android; rebuild after changing it.
    constexpr Beast::RHIBackend Backend = Beast::RHIBackend::Vulkan;
}

class ResourcesDemoApplication final : public Beast::Application
{
public:
    Beast::WindowDefinition Configure() const override
    {
        Beast::WindowDefinition window;
        window.title = Backend == Beast::RHIBackend::OpenGL ? "ResourcesDemo - OpenGL" : "ResourcesDemo - Vulkan";
        window.gpuRendering = true;
        return window;
    }

    void OnStart() override { scene_ = std::make_unique<ResourceScene>(); Beast::Log("ResourcesDemo: resources ready"); }
    void OnStop() override { Beast::Log("ResourcesDemo: stopped"); }

    void OnEvent(const Beast::Event& event) override
    {
        using Beast::EventType;
        switch (event.type)
        {
        case EventType::SurfaceCreated:
        {
            Beast::RHIOptions options;
            options.backend = Backend;
            options.validation = true;
            renderer_ = Beast::RHI::Create(GetWindow(), options);
            firstFrame_ = true;
            Beast::Log("ResourcesDemo: RHI surface created");
            break;
        }
        case EventType::SurfaceDestroyed:
            renderer_.reset();
            Beast::Log("RHI surface destroyed");
            break;
        case EventType::Resumed: Beast::Log("Resumed"); break;
        case EventType::Suspended: Beast::Log("Suspended"); break;
        case EventType::FocusGained: Beast::Log("Focus gained"); break;
        case EventType::FocusLost: Beast::Log("Focus lost"); break;
        case EventType::KeyDown:
            if (event.key == Beast::Key::Escape) RequestExit();
            break;
        default: break;
        }
    }

    void OnUpdate(double deltaSeconds) override
    {
        (void)deltaSeconds;
        if (!renderer_) return;
        const auto& window = GetWindow();
        if (!commands_.IsReady() || width_ != window.width || height_ != window.height)
        {
            scene_->Record(commands_, window.width, window.height);
            width_ = window.width; height_ = window.height;
        }
        if (renderer_->Submit(window, commands_) && firstFrame_)
        { Beast::Log("ResourcesDemo: textured frame presented (3 pipelines, alpha, scissor)"); firstFrame_ = false; }
    }
private:
    Beast::RHICommandList commands_;
    std::unique_ptr<Beast::RHI> renderer_;
    std::unique_ptr<ResourceScene> scene_;
    int width_ = 0, height_ = 0;
    bool firstFrame_ = true;
};

BEAST_APPLICATION(ResourcesDemoApplication)
