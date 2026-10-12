#pragma once
#include "Beast.h"
#include "BeastShaders.generated.h"
#include <array>

// Shared by the interactive demo and GPU regression tests.
class ResourceScene
{
    Beast::RHIPipelineHandle opaque_, alpha_, premultiplied_;
    Beast::RHITextureHandle checker_, white_;
public:
    ResourceScene()
    {
        using namespace Beast;
        auto vertex = BeastShaders::UIVertex(), fragment = BeastShaders::UIFragment();
        opaque_ = RHI::CreatePipeline({vertex, fragment, RHIBlend::Opaque});
        alpha_ = RHI::CreatePipeline({vertex, fragment, RHIBlend::StraightAlpha});
        premultiplied_ = RHI::CreatePipeline({vertex, fragment, RHIBlend::PremultipliedAlpha});
        checker_ = RHI::CreateTexture({2, 2, {
            255, 0, 0, 255,     0, 255, 0, 255, // Top: red, green.
            0, 0, 255, 255,     255, 255, 0, 255 // Bottom: blue, yellow.
        }, RHIFilter::Nearest});
        white_ = RHI::CreateTexture({1, 1, {255, 255, 255, 255}, RHIFilter::Linear});
    }
    static std::array<Beast::RHIVertex, 6> Quad(float left, float top, float right, float bottom,
        Beast::RHIColor color = {1, 1, 1, 1})
    {
        return {{{left, top, 0, 0, color}, {right, top, 1, 0, color}, {right, bottom, 1, 1, color},
            {left, top, 0, 0, color}, {right, bottom, 1, 1, color}, {left, bottom, 0, 1, color}}};
    }
    void Record(Beast::RHICommandList& commands, int width, int height) const
    {
        commands.Reset(); commands.BeginRenderPass({0.1f, 0.1f, 0.1f, 1});
        commands.DrawTriangles(opaque_, checker_, Quad(-0.8f, -0.8f, 0.8f, 0.8f));
        // Clear and draw order must be preserved; this patch stays magenta.
        commands.ClearRect({width / 5, height / 5, width / 10, height / 10}, {1, 0, 1, 1});
        commands.DrawTriangles(alpha_, white_, Quad(-0.6f, -0.15f, 0.6f, 0.15f, {1, 1, 1, 0.5f}));
        commands.DrawTriangles(premultiplied_, white_, Quad(-0.6f, 0.3f, 0.6f, 0.5f, {0.25f, 0.25f, 0.25f, 0.25f}));
        commands.DrawTriangles(opaque_, white_, Quad(-1, -1, 1, 1, {0, 1, 1, 1}),
            Beast::RHIRect{width * 7 / 10, height * 7 / 10, width / 10, height / 10});
        commands.EndRenderPass();
    }
};
