#pragma once
#include "RHI/RHIResources.h"
#include <optional>
#include <span>
#include <variant>
#include <array>

namespace Beast
{
    // Top-left origin, physical pixels. Clipped to the current drawable size.
    struct RHIRect { int x = 0, y = 0, width = 0, height = 0; };
    struct RHIClearRect { RHIRect rect; RHIColor color; };
    // Optional fragment-stage constants: Vulkan push constants / GL vec4[8].
    // 128 bytes fits the Vulkan baseline; recorded by value for safe replay.
    using RHIDrawConstants = std::array<float, 32>;
    struct RHIDrawTriangles
    {
        RHIPipelineHandle pipeline;
        RHITextureHandle texture;
        std::vector<RHIVertex> vertices;
        std::optional<RHIRect> scissor;
        RHIDrawConstants constants{};
    };
    using RHICommand = std::variant<RHIClearRect, RHIDrawTriangles>;

    // CPU-side recording, independent of the selected GPU backend. One color
    // pass per list for now. A finished list can be submitted more than once.
    class RHICommandList
    {
    public:
        void Reset() noexcept;
        // Record and close a single clear-only pass. Requires an empty list;
        // call Reset before re-recording. The same closed list can be reused.
        void ClearFrameBuffer(RHIColor color);
        void BeginRenderPass(RHIColor clearColor);
        void ClearRect(RHIRect rect, RHIColor color);
        void DrawTriangles(RHIPipelineHandle pipeline, RHITextureHandle texture,
            std::span<const RHIVertex> vertices, std::optional<RHIRect> scissor = {}, const RHIDrawConstants& constants = {});
        void EndRenderPass();
        bool IsReady() const noexcept { return state_ == State::Closed; }
        RHIColor GetClearColor() const noexcept { return clearColor_; }
        const std::vector<RHIClearRect>& GetRectangles() const noexcept { return rectangles_; }
        const std::vector<RHICommand>& GetCommands() const noexcept { return commands_; }
    private:
        enum class State { Empty, Recording, Closed };
        State state_ = State::Empty;
        RHIColor clearColor_;
        std::vector<RHIClearRect> rectangles_;
        std::vector<RHICommand> commands_;
        std::size_t vertexCount_ = 0;
    };
}
