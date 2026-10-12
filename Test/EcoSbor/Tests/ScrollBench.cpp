#include "Launch/EcoScene.h"
#include <chrono>
#include <cstdio>
#include <unordered_set>
#include <algorithm>
int main()
{
    Eco::Scene scene; Beast::Window w; w.scale = 2.625f; w.width = w.contentWidth = 1080; w.height = w.contentHeight = 1800;
    scene.ui.BuildFrame(w, 0);
    std::vector<double> times; std::vector<Beast::RHITextureHandle> previous;
    unsigned fresh = 0;
    for (int frame = 0; frame < 32; ++frame)
    {
        const auto start = std::chrono::steady_clock::now();
        scene.pages[0]->SetOffset(20 + frame * 3.7f);
        const auto& commands = scene.ui.BuildFrame(w, 0);
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        std::unordered_set<const Beast::RHITexture*> seen, old;
        for (auto& texture : previous) old.insert(texture.get());
        std::vector<Beast::RHITextureHandle> next;
        for (const auto& c : commands.GetCommands()) if (auto* draw = std::get_if<Beast::RHIDrawTriangles>(&c))
            if (seen.insert(draw->texture.get()).second) { next.push_back(draw->texture); if (frame && !old.contains(draw->texture.get())) ++fresh; }
        previous = std::move(next);
    }
    std::sort(times.begin(), times.end());
    std::printf("CPU scroll frame median %.3f ms, p95 %.3f ms; textures new since previous frame: %u over 31 transitions\n", times[16], times[30], fresh);
}
