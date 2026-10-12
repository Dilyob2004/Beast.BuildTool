#pragma once
#include "Core/Application.h"
#include "UI/UI.h"

namespace Beast::UI
{
    // Persistent content, independent of an Android surface or RHI.
    class Scene
    {
    public:
        Context ui;
        virtual ~Scene() = default;
        // Return true when navigation consumed Back; false exits the root page.
        virtual bool OnBack() { return false; }
        // Unconsumed input/lifecycle events; Back navigation uses OnBack above.
        // Return true to consume input (including overriding default Escape).
        virtual bool OnEvent(const Event&) { return false; }
        virtual void OnUpdate(double) {}
        // Explicit, versioned application state for development hot reload.
        // Do not serialize object addresses or raw C++ class memory.
        virtual std::string SaveReloadState() const { return {}; }
        virtual bool RestoreReloadState(const std::string& state, const Window&) { return state.empty(); }
        // Stop/join application-owned tasks and detach external callbacks here.
        // False refuses replacement. Called only after the candidate is ready.
        virtual bool QuiesceForReload() noexcept { return true; }
    };

    // Ready-to-run UI host. Beast::Application remains available for custom
    // render loops. Native surface management does not belong in Scene.
    class Application : public Beast::Application
    {
        RHIOptions options_;
        std::unique_ptr<Scene> scene_;
        std::unique_ptr<RHI> renderer_;
        bool firstFrame_ = true, suspended_ = false;
        std::uint64_t submittedRevision_ = 0;
    protected:
        virtual WindowDefinition ConfigureWindow() const { return {}; }
        virtual std::unique_ptr<Scene> CreateScene() = 0;
        virtual void BeforeSceneUpdate() {}
        void ReplaceScene(std::unique_ptr<Scene> scene);
        // Injection point for specialized renderers and lifecycle tests.
        virtual std::unique_ptr<RHI> CreateRenderer(const Window& window, RHIOptions options)
        { return RHI::Create(window, options); }
    public:
        explicit Application(RHIOptions options = {}) : options_(options) {}
        Scene* GetScene() noexcept { return scene_.get(); }
        const Scene* GetScene() const noexcept { return scene_.get(); }
        RHIFrameStats GetRenderStats() const noexcept { return renderer_ ? renderer_->GetFrameStats() : RHIFrameStats{}; }
        WindowDefinition Configure() const final;
        void OnStart() final;
        void OnEvent(const Event& event) final;
        bool OnBack() final;
        void OnUpdate(double deltaSeconds) final;
        void OnStop() final;
    };
    struct AppOptions
    {
        std::string title = "Beast Application";
        int width = 960, height = 640;
        RHIBackend backend = RHIBackend::OpenGL;
        bool vsync = true, validation = false, profiling = false;
    };
    // Standard host for a Scene. Override Application directly only for custom
    // window policy/renderers; ordinary applications need no lifecycle boilerplate.
    template<class SceneType> class SceneApplication : public Application
    {
        AppOptions options_;
        WindowDefinition ConfigureWindow() const override
        {
            WindowDefinition w; w.title = options_.title; w.width = options_.width; w.height = options_.height; return w;
        }
        std::unique_ptr<Scene> CreateScene() override { return std::make_unique<SceneType>(); }
    public:
        explicit SceneApplication(AppOptions options = {})
            : Application({options.backend, options.vsync, options.validation, options.profiling}), options_(std::move(options)) {}
    };
}
