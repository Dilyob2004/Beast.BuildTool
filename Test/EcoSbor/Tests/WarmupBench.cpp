#include "Launch/EcoScene.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <stdexcept>

struct Result { std::uint64_t glyphs, pages; double median, p95; };
Result Run(bool warm, bool reveal)
{
    Eco::Scene scene;
    Beast::Window w; w.scale = 2.625f; w.width = w.contentWidth = 1080; w.height = w.contentHeight = 1800;
    scene.ui.SetTextWarmupEnabled(warm); scene.ui.BuildFrame(w, 0);
    if (warm) {
        for (int i = 0; i < 360; ++i) scene.ui.BuildFrame(w, 1.0 / 60);
        const auto settled = scene.ui.GetFrameRevision();
        for (int i = 0; i < 30; ++i) scene.ui.BuildFrame(w, 1.0 / 60);
        if (scene.ui.GetFrameRevision() != settled) throw std::runtime_error("Warmup must finish and return to idle.");
    }
    const auto before = scene.ui.GetTextCacheStats();
    std::vector<double> times;
    // Real event routing suppresses speculative work during a gesture.
    Beast::Event touch; touch.type = Beast::EventType::TouchDown; touch.x = 80; touch.y = 700;
    scene.ui.HandleEvent(touch, w);
    for (int frame = 0; frame < 32; ++frame) {
        touch.type = Beast::EventType::TouchMove; touch.y += reveal ? -10.7f : 33.3f;
        scene.ui.HandleEvent(touch, w);
        const auto start = std::chrono::steady_clock::now();
        scene.ui.BuildFrame(w, 1.0 / 60);
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    const auto after = scene.ui.GetTextCacheStats();
    std::sort(times.begin(), times.end());
    return {after.glyphMisses - before.glyphMisses, after.pagesCreated - before.pagesCreated, times[16], times[30]};
}
int main()
{
    try {
        for (bool reveal : {false, true}) {
            std::printf("Scenario: %s (cold, then prewarmed)\n", reveal ? "reveal nearby content" : "overscroll");
            const auto cold = Run(false, reveal), warm = Run(true, reveal);
            for (const auto& r : {cold, warm}) std::printf("glyph misses=%llu page versions=%llu CPU median=%.3f p95=%.3f ms\n",
                static_cast<unsigned long long>(r.glyphs), static_cast<unsigned long long>(r.pages), r.median, r.p95);
            if (warm.glyphs >= cold.glyphs || warm.pages >= cold.pages) throw std::runtime_error("Idle warmup did not reduce cold scroll work.");
        }
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
