#include "Launch/ButtonScene.h"

namespace { constexpr auto Backend = Beast::RHIBackend::Vulkan; }

class ButtonDemoApplication final : public Beast::Application
{
    std::unique_ptr<Beast::RHI> renderer_;
    std::unique_ptr<ButtonScene> scene_;
    bool firstFrame_ = true;
public:
    Beast::WindowDefinition Configure() const override
    {
        Beast::WindowDefinition window;
        window.title = Backend == Beast::RHIBackend::OpenGL ? "Beast UI - OpenGL" : "Beast UI - Vulkan";
        window.width = 560; window.height = 700; window.gpuRendering = true;
        return window;
    }
    void OnStart() override { scene_ = std::make_unique<ButtonScene>(); }
    void OnEvent(const Beast::Event& event) override
    {
        // Cancel gestures on focus/surface loss before discarding the GPU device.
        if (scene_) scene_->ui.HandleEvent(event, GetWindow());
        if (event.type == Beast::EventType::SurfaceCreated)
        { renderer_ = Beast::RHI::Create(GetWindow(), {Backend, true, true}); firstFrame_ = true; }
        if (event.type == Beast::EventType::SurfaceDestroyed) renderer_.reset();
        if (event.type == Beast::EventType::KeyDown && event.key == Beast::Key::Escape) RequestExit();
    }
    void OnUpdate(double seconds) override
    {
        if (!renderer_) return;
        if (scene_->ui.Render(*renderer_, GetWindow(), seconds) && firstFrame_)
        {
            const auto bounds = scene_->primary->GetBounds(); const auto& window = GetWindow();
            Beast::Log(("ButtonDemo: frame ready, primary center physical=" +
                std::to_string(window.contentX + (bounds.x + bounds.width / 2) * window.scale) + "," +
                std::to_string(window.contentY + (bounds.y + bounds.height / 2) * window.scale)).c_str());
            firstFrame_ = false;
        }
    }
};
BEAST_APPLICATION(ButtonDemoApplication)
