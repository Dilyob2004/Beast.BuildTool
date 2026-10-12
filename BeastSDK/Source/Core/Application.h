#pragma once
#include "Core/Window.h"
#include "Input/Input.h"

namespace Beast
{
    class Application
    {
    public:
        virtual ~Application() = default;
        virtual WindowDefinition Configure() const { return {}; }
        virtual void OnStart() {}
        virtual void OnEvent(const Event&) {}
        // Return true if navigation handled Back. The default exits the app.
        virtual bool OnBack() { return false; }
        virtual void OnUpdate(double deltaSeconds) { (void)deltaSeconds; }
        virtual void OnStop() {}

        const Window& GetWindow() const { return window_; }
        const Input& GetInput() const { return input_; }
        void RequestExit() { exitRequested_ = true; }
        bool IsExitRequested() const { return exitRequested_; }

        // Platform adapter API; callbacks and updates execute on the Run thread.
        Window& PlatformWindow() { return window_; }
        void BeginFrame() { input_.BeginFrame(); }
        void Dispatch(const Event& event);
    private:
        Window window_;
        Input input_;
        bool exitRequested_ = false;
    };

    // Windows: nativeContext=nullptr. Android: android_app* supplied by entry macro.
    int Run(Application& application, void* nativeContext = nullptr);
    void Log(const char* message);
}
