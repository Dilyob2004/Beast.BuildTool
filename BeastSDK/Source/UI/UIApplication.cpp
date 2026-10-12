#include "UI/Application.h"
#include "Core/Trace.h"
#include <chrono>
#include <cmath>
#include <cstdio>

namespace Beast::UI
{
    WindowDefinition Application::Configure() const
    { auto window = ConfigureWindow(); window.gpuRendering = true; return window; }

    void Application::OnStart()
    {
        if (scene_) throw std::logic_error("UI Application is already started.");
        scene_ = CreateScene();
        if (!scene_) throw std::logic_error("UI Application requires a Scene.");
        scene_->ui.SetTextWarmupEnabled(true);
        suspended_ = false;
    }
    void Application::OnEvent(const Event& event)
    {
        const bool handled = scene_ && scene_->ui.HandleEvent(event, GetWindow());
        if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        if (event.type == EventType::SurfaceCreated && scene_)
        {
            renderer_.reset();
            renderer_ = CreateRenderer(GetWindow(), options_); firstFrame_ = true;
            if (!renderer_) throw std::runtime_error("UI Application could not create RHI.");
        }
        if (event.type == EventType::Suspended) suspended_ = true;
        if (event.type == EventType::Resumed) { suspended_ = false; firstFrame_ = true; }
        if (event.type == EventType::FocusGained || event.type == EventType::Resized) firstFrame_ = true;
        if (event.type == EventType::KeyDown && event.key == Key::Back) return; // Core dispatches OnBack exactly once.
        if (handled || (scene_ && scene_->OnEvent(event))) return;
        if (event.type != EventType::KeyDown || event.repeat) return;
        if (event.key == Key::Escape) RequestExit();
    }
    bool Application::OnBack() { return scene_ && (scene_->ui.DismissTextInput() || scene_->OnBack()); }
    void Application::ReplaceScene(std::unique_ptr<Scene> scene)
    {
        if (!scene) throw std::invalid_argument("Cannot replace UI with an empty scene.");
        scene->ui.SetTextWarmupEnabled(true);
        scene_ = std::move(scene); firstFrame_ = true; submittedRevision_ = 0;
    }
    void Application::OnUpdate(double deltaSeconds)
    {
        if (!scene_ || !renderer_ || suspended_ || IsExitRequested()) return;
        BeforeSceneUpdate(); // Safe point: no scene callback or Submit is in progress.
        Beast::Detail::TraceScope frameTrace("Beast.UI.Frame");
        if (Beast::Detail::TraceEnabled() && std::isfinite(deltaSeconds) && deltaSeconds >= 0 && deltaSeconds <= 60)
            Beast::Detail::TraceCounter("Beast.FrameDeltaUs", static_cast<std::int64_t>(deltaSeconds * 1000000));
        scene_->OnUpdate(deltaSeconds);
        if (!renderer_ || IsExitRequested()) return;
        const auto buildStarted = options_.profiling ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto& commands = scene_->ui.BuildFrame(GetWindow(), deltaSeconds);
        const double buildMs = options_.profiling ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStarted).count() : 0;
        const auto revision = scene_->ui.GetFrameRevision();
        Beast::Detail::TraceCounter("Beast.UI.Revision", static_cast<std::int64_t>(revision));
        if ((firstFrame_ || revision != submittedRevision_) && renderer_->Submit(GetWindow(), commands))
        {
            submittedRevision_ = revision;
            if (firstFrame_) Log("Beast UI: frame ready");
            firstFrame_ = false;
            if (options_.profiling)
            {
                const auto stats = renderer_->GetFrameStats();
                char message[256];
                std::snprintf(message, sizeof(message),
                    "UI profile frame=%llu dt=%.3f build=%.3f submit=%.3f gpu=%.3f gpuFrame=%llu",
                    static_cast<unsigned long long>(stats.submittedFrames), deltaSeconds * 1000,
                    buildMs, stats.submitCpuMs, stats.gpuMs.value_or(-1),
                    static_cast<unsigned long long>(stats.gpuFrame));
                Log(message);
            }
        }
    }
    void Application::OnStop()
    {
        if (scene_) scene_->ui.HandleEvent(Event{EventType::Suspended}, GetWindow());
        renderer_.reset(); scene_.reset();
    }
}
