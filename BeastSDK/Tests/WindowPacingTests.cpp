#include "Beast.h"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

class Pacing final : public Beast::Application
{
    Beast::RHIBackend backend_;
    std::unique_ptr<Beast::RHI> renderer_;
    Beast::RHICommandList commands_;
    std::chrono::steady_clock::time_point previous_;
    std::vector<double> intervals_;
    unsigned frames_ = 0, attempts_ = 0;
public:
    explicit Pacing(Beast::RHIBackend backend) : backend_(backend) {}
    Beast::WindowDefinition Configure() const override
    { Beast::WindowDefinition w; w.title = "Beast Windows VSync pacing test"; w.width = 420; w.height = 600; w.gpuRendering = true; return w; }
    void OnEvent(const Beast::Event& e) override
    {
        if (e.type == Beast::EventType::SurfaceCreated) renderer_ = Beast::RHI::Create(GetWindow(), {backend_, true, true});
        if (e.type == Beast::EventType::SurfaceDestroyed) renderer_.reset();
    }
    void OnUpdate(double) override
    {
        if (++attempts_ > 600) throw std::runtime_error("Pacing test did not complete.");
        if (!renderer_) return;
        const auto now = std::chrono::steady_clock::now();
        if (frames_ >= 15) intervals_.push_back(std::chrono::duration<double, std::milli>(now - previous_).count());
        previous_ = now;
        commands_.Reset(); commands_.ClearFrameBuffer({.1f, .1f + (frames_ % 60) / 600.f, .1f, 1});
        if (!renderer_->Submit(GetWindow(), commands_)) return;
        if (++frames_ == 135) {
            std::sort(intervals_.begin(), intervals_.end());
            std::printf("Windows %s vsync frame intervals: median=%.3f p95=%.3f max=%.3f ms (%zu samples)\n",
                backend_ == Beast::RHIBackend::Vulkan ? "Vulkan" : "OpenGL", intervals_[intervals_.size()/2],
                intervals_[(intervals_.size()-1)*95/100], intervals_.back(), intervals_.size());
            RequestExit();
        }
    }
};
int main()
{
    try {
        for (auto backend : {Beast::RHIBackend::OpenGL, Beast::RHIBackend::Vulkan}) { Pacing test(backend); if (Beast::Run(test)) return 1; }
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
