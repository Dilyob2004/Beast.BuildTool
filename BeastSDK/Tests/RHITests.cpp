#include "Beast.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace Beast;
namespace
{
    void Check(bool value) { if (!value) throw std::runtime_error("RHI test failed"); }
    template<class T> void Reject(T&& operation)
    {
        bool rejected = false;
        try { operation(); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected);
    }
    void TestCommands()
    {
        RHICommandList commands;
        Check(!commands.IsReady());
        Reject([&] { commands.EndRenderPass(); });
        Reject([&] { commands.ClearRect({0, 0, 5, 5}, {}); });
        Reject([&] { commands.BeginRenderPass({std::numeric_limits<float>::quiet_NaN(), 0, 0, 1}); });
        commands.BeginRenderPass({0.1f, 0.2f, 0.3f, 1});
        Reject([&] { commands.BeginRenderPass({}); });
        Reject([&] { commands.ClearRect({0, 0, -1, 2}, {}); });
        commands.ClearRect({-5, -5, 20, 20}, {1, 0, 0, 1});
        commands.EndRenderPass(); Check(commands.IsReady());
        Check(commands.GetRectangles().size() == 1);
        Reject([&] { commands.ClearRect({}, {}); });
        commands.Reset(); Check(!commands.IsReady() && commands.GetRectangles().empty());
        Reject([&] { commands.ClearFrameBuffer({-0.1f, 0, 0, 1}); });
        Check(!commands.IsReady());
        commands.ClearFrameBuffer({0.1f, 0.1f, 0.1f, 1.0f});
        Check(commands.IsReady() && commands.GetRectangles().empty());
        Check(commands.GetClearColor().r == 0.1f && commands.GetClearColor().g == 0.1f &&
            commands.GetClearColor().b == 0.1f && commands.GetClearColor().a == 1.0f);
        Reject([&] { commands.ClearFrameBuffer({}); });
        commands.Reset(); commands.BeginRenderPass({});
        Reject([&] { commands.ClearFrameBuffer({}); });
        commands.EndRenderPass();
        Reject([&] { RHI::Create(Window{}); });
    }
#if defined(_WIN32)
    class RenderTest final : public Application
    {
        RHIOptions options_;
        std::unique_ptr<RHI> renderer_;
        RHICommandList commands_;
        int ticks_ = 0;
    public:
        int frames = 0, surfaces = 0;
        explicit RenderTest(RHIBackend backend) { options_.backend = backend; options_.validation = true; }
        WindowDefinition Configure() const override
        { WindowDefinition result; result.title = "Beast RHI test"; result.gpuRendering = true; return result; }
        void OnEvent(const Event& event) override
        {
            if (event.type == EventType::SurfaceCreated)
            { renderer_ = RHI::Create(GetWindow(), options_); ++surfaces; }
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        }
        void OnUpdate(double) override
        {
            Check(renderer_->GetBackend() == options_.backend);
            commands_.Reset(); commands_.BeginRenderPass({0.04f, 0.07f, 0.12f, 1});
            commands_.ClearRect({-5, 40, 400, 200}, {0.1f, 0.7f, 0.9f, 1});
            commands_.ClearRect({80, 80, 220, 100}, {1, 0.45f, 0.1f, 1});
            commands_.ClearRect({std::numeric_limits<int>::max(), 0, 5, 5}, {});
            commands_.EndRenderPass();
            if (renderer_->Submit(GetWindow(), commands_)) ++frames;
            if (++ticks_ == 10)
                SetWindowPos(static_cast<HWND>(GetWindow().nativeHandle), nullptr, 0, 0, 720, 520, SWP_NOMOVE | SWP_NOZORDER);
            if (ticks_ == 20)
            {
                // Recreate the backend on the same live HWND (pixel format is already set).
                renderer_.reset(); renderer_ = RHI::Create(GetWindow(), options_);
            }
            if (ticks_ == 40) { Check(frames >= 30); RequestExit(); }
        }
    };
#endif
}
int main()
{
    try
    {
        TestCommands();
#if defined(_WIN32)
        RenderTest gl(RHIBackend::OpenGL); Check(Run(gl) == 0); Check(gl.frames >= 30);
        RenderTest vk(RHIBackend::Vulkan); Check(Run(vk) == 0); Check(vk.frames >= 30);
#endif
        std::puts("RHI command list and backend tests passed."); return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
