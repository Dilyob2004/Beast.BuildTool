#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include <cstdio>
#include <source_location>

using namespace Beast;
using namespace Beast::UI;
namespace {
    void Check(bool value, std::source_location at = std::source_location::current()) {
        if (!value) throw std::runtime_error("Glyph prefetch assertion at line " + std::to_string(at.line()));
    }
    Window View(float scale = 1) {
        Window w; w.scale = scale;
        w.width = w.contentWidth = static_cast<int>(240 * scale);
        w.height = w.contentHeight = static_cast<int>(240 * scale);
        return w;
    }
    void Label(Canvas& c, Point p, const std::string& text = "Ближайший Qz", float size = 16) {
        Text label(text, size); label.Layout({p.x, p.y, 160, 32}); label.Paint(c);
    }
    void Drain(Canvas& c, const Window& w, RHICommandList& commands) {
        for (int i = 0; i < 512; ++i) {
            const auto before = c.GetTextCacheStats();
            const bool uploaded = c.WarmText();
            Check(c.GetTextCacheStats().sealedBytes - before.sealedBytes <= 512 * 1024);
            c.Begin(commands, w, {}); c.End();
            for (const auto& cmd : commands.GetCommands()) {
                const auto* draw = std::get_if<RHIDrawTriangles>(&cmd); Check(draw);
                Check(draw->texture->IsReady() && draw->vertices.size() == 3);
                for (const auto& v : draw->vertices) Check(v.x == 0 && v.y == 0 && v.color.a == 0);
            }
            Check(uploaded == !commands.GetCommands().empty());
            Beast::Detail::CheckSubmission(w, nullptr, commands);
        }
        Check(!c.WarmText());
    }
    void Neighborhood() {
        for (const auto axis : {Axis::Vertical, Axis::Horizontal}) for (float scale : {1.f, 1.25f, 2.625f}) {
            Canvas c; auto w = View(scale); RHICommandList commands;
            const Point near = axis == Axis::Vertical ? Point{20, 160} : Point{160, 20};
            const Point far = axis == Axis::Vertical ? Point{20, 230} : Point{230, 20};
            c.DiscoverWarmText(true); c.Begin(commands, w, {});
            c.PushClip({20, 20, 120, 120}); c.PushScrollOffset({}, false, axis);
            Label(c, near); Label(c, far, "Far", 19);
            c.PopScrollOffset(); c.PopClip(); c.End();
            Check(commands.GetCommands().empty() && c.GetTextCacheStats().glyphMisses == 0);
            Drain(c, w, commands);
            const auto warmed = c.GetTextCacheStats(); Check(warmed.glyphMisses > 0);
            c.DiscoverWarmText(false);
            for (int phase = 0; phase < 8; ++phase) {
                c.Begin(commands, w, {}); c.PushScrollOffset({}, false, axis);
                Point p{20, 20}; (axis == Axis::Vertical ? p.y : p.x) += phase / (8.f * scale);
                Label(c, p); c.PopScrollOffset(); c.End();
                Check(!commands.GetCommands().empty());
                Check(c.GetTextCacheStats().glyphMisses == warmed.glyphMisses);
                Check(c.GetTextCacheStats().pagesCreated == warmed.pagesCreated);
            }
            c.Begin(commands, w, {}); Label(c, {20, 20}, "Far", 19); c.End();
            Check(c.GetTextCacheStats().glyphMisses > warmed.glyphMisses);
        }
        std::puts("Nearby labels: both axes / fractional DPI / widget clips / no offscreen draws / all 8 phases hot; distant labels stay cold.");
    }
    void Scopes() {
        for (int mode = 0; mode < 5; ++mode) {
            Canvas c; auto w = View(); RHICommandList commands;
            c.DiscoverWarmText(mode != 0); c.Begin(commands, w, {});
            c.PushClip({20, 20, 120, 120}); c.PushScrollOffset({}, mode == 1);
            if (mode == 2) c.PushClip({20, 20, 120, 120});
            if (mode == 3) { c.SaveLayer({20, 20, 120, 120}); c.ClipRRect({20, 20, 120, 120}, 8); }
            if (mode == 4) c.PopScrollOffset(); // No reserve leaks outside its scope.
            Label(c, {20, 160});
            if (mode == 2) c.PopClip();
            if (mode == 3) c.Restore();
            if (mode != 4) c.PopScrollOffset();
            c.PopClip(); c.End();
            for (int i = 0; i < 8; ++i) Check(!c.WarmText());
            Check(c.GetTextCacheStats().glyphMisses == 0);
        }
        std::puts("Prefetch disabled / pixel-aligned / explicit clipping / rounded layers / Save-Restore isolation passed.");
    }
    void Budget() {
        UI::Detail::GlyphAtlas atlas; std::vector<RHITextureHandle> pages;
        const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        atlas.QueueWarm(text, 80, {}, FontFace::Bold, Axis::Vertical);
        std::uint64_t uploaded = 0;
        for (int i = 0; i < 512; ++i) {
            const auto before = atlas.GetStats().sealedBytes;
            atlas.WarmStep(pages);
            const auto bytes = atlas.GetStats().sealedBytes - before;
            Check(bytes <= 512 * 1024); uploaded += bytes;
            Check(atlas.GetStats().pageBytes <= 16 * 1024 * 1024);
        }
        Check(uploaded > 512 * 1024 && !atlas.WarmStep(pages));
        const auto warmed = atlas.GetStats(); std::vector<UI::Detail::GlyphAtlas::Quad> quads;
        for (int i = 0; i < 8; ++i) {
            atlas.BeginFrame(); Check(atlas.Resolve(text, 80, {0, i / 8.f}, FontFace::Bold, false, quads)); atlas.EndFrame();
        }
        Check(atlas.GetStats().glyphMisses == warmed.glyphMisses && atlas.GetStats().fallbackLines == 0);
        atlas.QueueWarm("cancel", 20, {}, FontFace::Regular, Axis::Vertical);
        atlas.SetBudget(0); Check(!atlas.WarmStep(pages));
        std::puts("Cold long label: incremental completion, <=512 KiB staging per step, unchanged atlas budget, no partial fallback, cancellation passed.");
    }
    class List final : public Widget {
        Size OnMeasure(const Constraints&) const override { return {240, 2400}; }
        void OnLayout() override {
            const auto r = GetBounds(); float y = r.y;
            for (const auto& child : Children()) { child->Layout({r.x, y, 220, 32}); y += 80; }
        }
    public:
        List() { for (int i = 0; i < 30; ++i) Adopt(std::make_shared<Text>("Строка " + std::to_string(i), 14.f + i * .25f)); }
    };
    void Gesture() {
        Context ui; auto scroll = std::make_shared<ScrollView>(std::make_shared<List>());
        scroll->SetRasterization(ScrollRasterization::Subpixel);
        ui.SetRoot(scroll); ui.SetTextWarmupEnabled(true); auto w = View(); ui.BuildFrame(w, 0);
        Event e; e.type = EventType::TouchDown; e.x = 10; e.y = 150; ui.HandleEvent(e, w); ui.BuildFrame(w, 0);
        const auto held = ui.GetTextCacheStats();
        for (int i = 0; i < 20; ++i) ui.BuildFrame(w, 1.0 / 60);
        Check(ui.GetTextCacheStats().glyphMisses == held.glyphMisses);
        e.type = EventType::TouchCancel; ui.HandleEvent(e, w);
        scroll->SetOffset(700); scroll->Fling(900);
        for (int i = 0; i < 900; ++i) ui.BuildFrame(w, 1.0 / 60);
        const auto idle = ui.GetFrameRevision(), warmed = ui.GetTextCacheStats().glyphMisses;
        for (int i = 0; i < 30; ++i) ui.BuildFrame(w, 1.0 / 60);
        Check(ui.GetFrameRevision() == idle && warmed > held.glyphMisses);
        // A small scroll reveals the prepared neighborhood, including labels
        // which were invisible when kinetic motion finished.
        scroll->SetOffset(scroll->GetOffset() + 60.375f); ui.BuildFrame(w, 0);
        Check(ui.GetTextCacheStats().glyphMisses == warmed);
        std::puts("Context: no warm rasterization while held, neighborhood refilled after kinetic motion, finite idle completion.");
    }
}
int main() {
    try { Neighborhood(); Scopes(); Budget(); Gesture(); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
