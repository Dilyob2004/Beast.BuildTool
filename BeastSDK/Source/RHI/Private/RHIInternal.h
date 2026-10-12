#pragma once
#include "RHI/RHI.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>
#include <chrono>

namespace Beast::Detail
{
    class OpenGLContext;
    std::unique_ptr<RHI> CreateOpenGLRHI(const Window&, RHIOptions);
    // Internal context injection also allows deterministic context-loss tests.
    std::unique_ptr<RHI> CreateOpenGLRHI(const Window&, std::unique_ptr<OpenGLContext>, RHIOptions = {});
    class SubmitCpuTimer
    {
        RHIFrameStats& stats_;
        bool enabled_;
        std::chrono::steady_clock::time_point start_;
    public:
        SubmitCpuTimer(RHIFrameStats& stats, bool enabled) : stats_(stats), enabled_(enabled)
        { if (enabled_) start_ = std::chrono::steady_clock::now(); }
        ~SubmitCpuTimer() { if (enabled_) stats_.submitCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count(); }
    };
    std::unique_ptr<RHI> CreateVulkanRHI(const Window&, RHIOptions);
    inline RHIRect Clip(RHIRect rect, int width, int height)
    {
        const auto left = std::clamp<std::int64_t>(rect.x, 0, width);
        const auto top = std::clamp<std::int64_t>(rect.y, 0, height);
        const auto right = std::clamp<std::int64_t>(std::int64_t{rect.x} + rect.width, left, width);
        const auto bottom = std::clamp<std::int64_t>(std::int64_t{rect.y} + rect.height, top, height);
        return {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left), static_cast<int>(bottom - top)};
    }
    inline void CheckSubmission(const Window& window, void* expected, const RHICommandList& commands)
    {
        if (window.nativeHandle != expected) throw std::logic_error("RHI belongs to a different native surface.");
        if (!commands.IsReady()) throw std::logic_error("Finish RHICommandList before submitting.");
        std::unordered_set<const RHITexture*> visited;
        std::vector<const RHICommandList*> lists{&commands};
        std::uint64_t bytes = 0, vertices = 0;
        while (!lists.empty()) {
            const auto* list = lists.back(); lists.pop_back();
            for (const auto& command : list->GetCommands()) if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                if (!draw->texture->IsReady()) throw std::logic_error("Seal staged textures before submitting commands.");
                vertices += draw->vertices.size();
                if (vertices > 10000000) throw std::invalid_argument("Frame exceeds ten million vertices including layers.");
                if (draw->texture->GetContent() && visited.insert(draw->texture.get()).second) {
                    const auto& desc = draw->texture->GetDesc(); bytes += std::uint64_t(desc.width) * desc.height * 4;
                    if (bytes > 256ull * 1024 * 1024) throw std::invalid_argument("Frame layers exceed 256 MiB.");
                    lists.push_back(draw->texture->GetContent());
                }
            }
        }
    }
}
