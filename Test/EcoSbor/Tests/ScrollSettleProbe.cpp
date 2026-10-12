#include "Launch/EcoScene.h"
#include <chrono>
#include <cstdio>
int main()
{
    Eco::Scene scene; Beast::Window w; w.scale = 2.625f;
    w.width = w.contentWidth = 1080; w.height = w.contentHeight = 1800;
    scene.ui.BuildFrame(w, 0); auto scroll = scene.pages[0];
    scroll->SetRasterization(Beast::UI::ScrollRasterization::PixelAligned); // Legacy-mode regression.
    scroll->DragBy(-160); scene.ui.BuildFrame(w, 0); scroll->Fling(0);
    int paints = 0, redundant = 0, settledFrame = -1;
    for (int i = 0; i < 100; ++i) {
        const auto oldPixel = std::round(scroll->GetOffset() * w.scale);
        auto before = scene.ui.GetRepaintCacheStats(); auto text = scene.ui.GetTextCacheStats();
        auto revision = scene.ui.GetFrameRevision(); auto start = std::chrono::steady_clock::now();
        scene.ui.BuildFrame(w, 1.0 / 60);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        auto after = scene.ui.GetRepaintCacheStats(); auto glyph = scene.ui.GetTextCacheStats();
        const bool painted = scene.ui.GetFrameRevision() != revision;
        if (settledFrame < 0 && scroll->GetOffset() == 0) settledFrame = i;
        paints += painted; redundant += painted && oldPixel == std::round(scroll->GetOffset() * w.scale);
        if (i < 20 || i > 38) continue;
        std::printf("%d offset=%.6f pixel=%.0f paint=%d ms=%.3f cacheMiss=%llu glyphMiss=%llu pageVersion=%llu\n", i,
            scroll->GetOffset(), std::round(scroll->GetOffset() * w.scale), scene.ui.GetFrameRevision() != revision, ms,
            static_cast<unsigned long long>(after.misses-before.misses), static_cast<unsigned long long>(glyph.glyphMisses-text.glyphMisses),
            static_cast<unsigned long long>(glyph.pagesCreated-text.pagesCreated));
    }
    std::printf("Settle: %d repaints, %d redundant same-pixel repaints; exact rest at frame %d (%.1f ms).\n",
        paints, redundant, settledFrame, (settledFrame + 1) * 1000.0 / 60);
    return redundant || settledFrame < 0 || settledFrame > 27 ? 1 : 0;
}
