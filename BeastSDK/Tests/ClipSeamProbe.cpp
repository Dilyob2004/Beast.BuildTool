// Regression for adjacent fractional hard clips; GPU coverage is separately
// verified by UIShapeTests, including horizontal and vertical boundaries.
#include "UI/Canvas.h"
#include <cstdio>

int main()
{
    using namespace Beast;
    using namespace Beast::UI;
    Canvas canvas;
    for (float scale : {1.0f, 1.25f, 1.5f, 2.625f})
    {
        Window window; window.width = window.contentWidth = 160;
        window.height = window.contentHeight = 80; window.scale = scale;
        RHICommandList commands;
        canvas.Begin(commands, window, {});
        for (const Rect rect : {Rect{0, 0, 10.25f, 20}, Rect{10.25f, 0, 29.75f, 20}})
        {
            canvas.PushClip(rect);
            canvas.RoundedRect(rect, 0, {0, 1, 0, 1});
            canvas.PopClip();
        }
        canvas.End();
        const auto& first = std::get<RHIDrawTriangles>(commands.GetCommands().at(0));
        const auto& second = std::get<RHIDrawTriangles>(commands.GetCommands().at(1));
        const auto a = first.scissor.value(), b = second.scissor.value();
        if (a.x + a.width != b.x) return 1;
        std::printf("scale=%.3f: adjacent edge=%.5fpx, first scissor=[%d,%d), second=[%d,%d), gap=%dpx\n",
            scale, 10.25f * scale, a.x, a.x + a.width, b.x, b.x + b.width, std::max(0, b.x - a.x - a.width));
    }
    // Decimal geometry around half-pixel ties, with nested intersections.
    for (float scale : {.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.625f, 3.f, 4.f})
        for (int i = 0; i < 1000; ++i) {
            Window w; w.width = w.contentWidth = 800; w.height = w.contentHeight = 300; w.scale = scale;
            const float x = i * .1f, width = 10.2f; const float edge = x + width;
            RHICommandList commands; canvas.Begin(commands, w, {});
            canvas.PushClip({0, 0, 180, 60});
            for (const Rect rect : {Rect{x, 0, width, 20}, Rect{edge, 0, 20, 20}}) {
                canvas.PushClip(rect); canvas.RoundedRect(rect, 0, {0, 1, 0, 1}); canvas.PopClip();
            }
            canvas.PopClip(); canvas.End();
            const auto a = *std::get<RHIDrawTriangles>(commands.GetCommands()[0]).scissor;
            const auto b = *std::get<RHIDrawTriangles>(commands.GetCommands()[1]).scissor;
            if (a.x + a.width != b.x) { std::printf("Half-pixel mismatch at %.5f scale %.3f\n", edge, scale); return 1; }
        }
    std::puts("9000 decimal/nested clip boundaries have neither holes nor overlap.");
}
