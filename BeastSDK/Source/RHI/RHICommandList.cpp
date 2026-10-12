#include "RHI/RHICommandList.h"
#include <cmath>
#include <stdexcept>

namespace Beast
{
    namespace
    {
        void Validate(RHIColor color)
        {
            for (float value : {color.r, color.g, color.b, color.a})
                if (!std::isfinite(value) || value < 0 || value > 1)
                    throw std::invalid_argument("RHI colors must be finite values in [0, 1].");
        }
    }
    void RHICommandList::Reset() noexcept { state_ = State::Empty; rectangles_.clear(); commands_.clear(); vertexCount_ = 0; }
    void RHICommandList::ClearFrameBuffer(RHIColor color)
    {
        BeginRenderPass(color);
        EndRenderPass();
    }
    void RHICommandList::BeginRenderPass(RHIColor clearColor)
    {
        if (state_ != State::Empty) throw std::logic_error("Reset RHICommandList before beginning another pass.");
        Validate(clearColor);
        clearColor_ = clearColor;
        state_ = State::Recording;
    }
    void RHICommandList::ClearRect(RHIRect rect, RHIColor color)
    {
        if (state_ != State::Recording) throw std::logic_error("ClearRect requires an active render pass.");
        if (rect.width < 0 || rect.height < 0) throw std::invalid_argument("Negative RHI rectangle size.");
        Validate(color);
        commands_.emplace_back(RHIClearRect{rect, color});
        try { rectangles_.push_back({rect, color}); }
        catch (...) { commands_.pop_back(); throw; }
    }
    void RHICommandList::DrawTriangles(RHIPipelineHandle pipeline, RHITextureHandle texture,
        std::span<const RHIVertex> vertices, std::optional<RHIRect> scissor, const RHIDrawConstants& constants)
    {
        if (state_ != State::Recording) throw std::logic_error("DrawTriangles requires an active render pass.");
        if (!pipeline || !texture) throw std::invalid_argument("DrawTriangles requires a pipeline and texture.");
        if (vertices.empty() || vertices.size() % 3 || vertices.size() > 1000000)
            throw std::invalid_argument("Expected a nonempty triangle list, at most one million vertices per draw.");
        if (vertices.size() > 10000000 - vertexCount_) throw std::invalid_argument("Maximum ten million vertices per command list.");
        if (scissor && (scissor->width < 0 || scissor->height < 0)) throw std::invalid_argument("Negative scissor size.");
        for (float value : constants)
            if (!std::isfinite(value)) throw std::invalid_argument("Draw constants must be finite.");
        for (const auto& vertex : vertices)
        {
            for (float value : {vertex.x, vertex.y, vertex.u, vertex.v})
                if (!std::isfinite(value)) throw std::invalid_argument("Vertex coordinates must be finite.");
            Validate(vertex.color);
        }
        commands_.emplace_back(RHIDrawTriangles{std::move(pipeline), std::move(texture),
            {vertices.begin(), vertices.end()}, scissor, constants});
        vertexCount_ += vertices.size();
    }
    void RHICommandList::EndRenderPass()
    {
        if (state_ != State::Recording) throw std::logic_error("No active RHI render pass.");
        state_ = State::Closed;
    }
}
