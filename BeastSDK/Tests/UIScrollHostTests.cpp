#include "Beast.h"
#include "UI/Canvas.h"
#include "UI/Private/ScrollSampler.h"
#include "Platform/Android/FrameClock.h"
#include "Platform/Android/SafeArea.h"
#include <cstdio>
#include <source_location>
using namespace Beast;
using namespace Beast::UI;
namespace
{
    void Check(bool value, std::source_location at = std::source_location::current())
    { if (!value) throw std::runtime_error("Scroll/host assertion at line " + std::to_string(at.line())); }
    Window View() { Window w; w.width = w.contentWidth = 320; w.height = w.contentHeight = 300; return w; }
    void Sampling()
    {
        UI::Detail::ScrollSampler sample;
        sample.Push(10, 1); sample.Push(26, 1.016);
        Check(std::abs(sample.Sample(1.016) - 18) < .001f);
        Check(std::abs(sample.Sample(1.028) - 30) < .001f);
        Check(std::abs(sample.Sample(1.050) - 34) < .001f); // Never predict indefinitely.
        sample.Push(30, 1.032); Check(sample.Latest() == 30);
        sample.Push(3, .5); Check(sample.Sample(.6) == 3); // Reset on out-of-order input.
        sample.Push(8, 3); Check(sample.Sample(3.1) == 8); // Reset after a gap.
        sample.Reset(); Check(!sample.HasValue());
        sample.Push(0, 1); sample.Push(1000, 1.002); Check(sample.Sample(2) <= 1008);
        sample.Push(std::numeric_limits<float>::infinity(), 2); Check(!sample.HasValue());
        std::puts("Scroll resampling: interpolation, bounded prediction, stale clocks, pause, reset and finite positions passed.");
    }
    void Motion()
    {
        float reference = -1;
        for (int hz : {30, 60, 90, 120})
        {
            MotionValue motion; motion.AnimateTo(1, .4f);
            for (int i = 0; i < hz / 5; ++i) motion.Tick(1.f / hz);
            if (reference < 0) reference = motion.Get();
            Check(std::abs(motion.Get() - reference) < .00001f);
            const float before = motion.Get(); motion.AnimateTo(0, .2f);
            Check(motion.Get() == before); motion.Tick(.05f); Check(motion.Get() < before);
            motion.Tick(1); Check(motion.Get() == 0 && !motion.Tick(1));
            motion.AnimateTo(1, 0); Check(motion.Get() == 1 && !motion.Tick(1));
        }
        auto opacity = std::make_shared<Opacity>(std::make_shared<Text>("Fade"), 1.f);
        Context ui; ui.SetRoot(opacity); auto w = View(); ui.BuildFrame(w, 0);
        opacity->AnimateTo(0); ui.BuildFrame(w, .05);
        Check(opacity->GetOpacity() > 0 && opacity->GetOpacity() < 1);
        for (int i = 0; i < 10; ++i) ui.BuildFrame(w, .05);
        Check(opacity->GetOpacity() == 0);
        const auto revision = ui.GetFrameRevision(); ui.BuildFrame(w, .05);
        Check(ui.GetFrameRevision() == revision);
        opacity->AnimateTo(1); ui.BuildFrame(w, .05); opacity->SetOpacity(.5f);
        ui.BuildFrame(w, .05); Check(opacity->GetOpacity() == .5f);
        std::puts("Motion refresh-rate independence, interruption, exact stop and animated opacity passed.");
    }
    void SafeArea()
    {
        auto w = View();
        auto pack = [](std::uint64_t l, std::uint64_t t, std::uint64_t r, std::uint64_t b)
        { return l | (t << 16) | (r << 32) | (b << 48); };
        Beast::Detail::ApplySafeInsets(w, pack(0, 24, 0, 30));
        Check(w.contentX == 0 && w.contentY == 24 && w.contentWidth == 320 && w.contentHeight == 246);
        Beast::Detail::ApplySafeInsets(w, pack(40, 0, 32, 0));
        Check(w.contentX == 40 && w.contentY == 0 && w.contentWidth == 248 && w.contentHeight == 300);
        Beast::Detail::ApplySafeInsets(w, pack(400, 400, 400, 400));
        Check(w.contentWidth == 0 && w.contentHeight == 0);
        Beast::Detail::ApplySafeInsets(w, 0);
        Check(w.contentX == 0 && w.contentY == 0 && w.contentWidth == 320 && w.contentHeight == 300);
        std::puts("Android safe-area portrait, landscape, oversized and cleared insets passed.");
    }
    struct Content final : Widget
    {
        mutable int measures = 0; int layouts = 0;
        Size OnMeasure(const Constraints&) const override { ++measures; return {1000, 1000}; }
        void OnLayout() override { ++layouts; }
    };
    std::shared_ptr<ScrollView> Scroll(Axis axis = Axis::Vertical, ScrollPhysics physics = ScrollPhysics::Bouncing)
    { auto scroll = std::make_shared<ScrollView>(std::make_shared<Content>(), axis, physics); scroll->Layout({0, 0, 320, 300}); return scroll; }
    void Settle(ScrollView& scroll, float step = 1.f / 60)
    { for (int i = 0; i < static_cast<int>(2 / step); ++i) scroll.Tick(step); }
    void SamplingLifecycle()
    {
        for (auto axis : {Axis::Vertical, Axis::Horizontal}) for (bool cancel : {false, true}) {
            Context ui; auto s = Scroll(axis); s->SetRasterization(ScrollRasterization::Subpixel); ui.SetRoot(s);
            auto w = View(); w.frameTimeSeconds = 1; ui.BuildFrame(w, 0);
            Event e; e.type = EventType::TouchDown; e.x = e.y = 100; e.timestamp = 1; e.pointerId = 7;
            ui.HandleEvent(e, w);
            e.type = EventType::TouchMove; (axis == Axis::Vertical ? e.y : e.x) = 140; e.timestamp = 1.016;
            ui.HandleEvent(e, w); const float raw = s->GetOffset();
            w.frameTimeSeconds = 1.028; ui.BuildFrame(w, 0);
            Check(s->GetOffset() < raw && s->GetOffset() >= raw - 8);
            // The next real event is integrated from the raw position, not from prediction.
            e.timestamp = 1.032; (axis == Axis::Vertical ? e.y : e.x) = 180; ui.HandleEvent(e, w);
            auto reference = Scroll(axis); reference->BeginDrag(); reference->DragBy(-80);
            Check(std::abs(s->GetOffset() - reference->GetOffset()) < .001f);
            w.frameTimeSeconds = 1.044; ui.BuildFrame(w, 0); const float shown = s->GetOffset();
            e.type = cancel ? EventType::TouchCancel : EventType::TouchUp; e.timestamp = 1.045; ui.HandleEvent(e, w);
            w.frameTimeSeconds = 1.052; ui.BuildFrame(w, 0); Check(s->GetOffset() == shown); // No release snap.
            for (int i = 0; i < 180; ++i) { w.frameTimeSeconds += 1.0 / 60; ui.BuildFrame(w, 1.0 / 60); }
            Check(s->GetOffset() == 0);
        }
        std::puts("Resampled touch routing: both axes, raw integration, release/cancel continuity and exact spring rest passed.");
        Context clampedUi; auto clamped = Scroll(Axis::Vertical, ScrollPhysics::Clamping);
        clampedUi.SetRoot(clamped); auto w = View(); w.frameTimeSeconds = 1; clampedUi.BuildFrame(w, 0);
        clamped->SetOffset(10);
        Event e; e.type = EventType::TouchDown; e.x = e.y = 100; e.timestamp = 1; clampedUi.HandleEvent(e, w);
        e.type = EventType::TouchMove; e.y = 115; e.timestamp = 1.016; clampedUi.HandleEvent(e, w);
        w.frameTimeSeconds = 1.04; clampedUi.BuildFrame(w, 0); Check(clamped->GetOffset() == 0);
    }
    void Physics()
    {
        for (auto axis : {Axis::Vertical, Axis::Horizontal})
        {
            auto s = Scroll(axis); s->BeginDrag(); s->DragBy(-120);
            const float stretched = s->GetOffset(); Check(stretched < 0 && stretched > -120);
            s->Tick(.1f); Check(s->GetOffset() == stretched); // held, no spring fighting the finger
            s->DragBy(120); Check(std::abs(s->GetOffset()) < .001f); // reversible
            s->DragBy(-120); s->Fling(0); s->Tick(.1f); Check(s->GetOffset() > stretched && s->GetOffset() < 0);
            Settle(*s); Check(s->GetOffset() == 0);
            s->SetOffset(s->GetExtent()); s->DragBy(120); Check(s->GetOffset() > s->GetExtent());
            s->Fling(700); Settle(*s); Check(s->GetOffset() == s->GetExtent());
            s->SetOffset(10); s->Fling(-1800); s->Tick(.033f); Check(s->GetOffset() < 0);
            Settle(*s); Check(s->GetOffset() == 0);
            s->DragBy(-60); const float held = s->GetOffset(); s->Fling(0); s->BeginDrag(); s->Tick(.1f); Check(s->GetOffset() == held);
            s->Stop(); Check(s->GetOffset() == 0);
            auto a = Scroll(axis), b = Scroll(axis); a->DragBy(-120);
            for (int i = 0; i < 12; ++i) b->DragBy(-10);
            Check(std::abs(a->GetOffset() - b->GetOffset()) < .001f);
        }
        auto clamped = Scroll(Axis::Vertical, ScrollPhysics::Clamping);
        clamped->DragBy(-100); Check(clamped->GetOffset() == 0); clamped->Fling(-4000); Settle(*clamped); Check(clamped->GetOffset() == 0);
        auto shortScroll = std::make_shared<ScrollView>(std::make_shared<Text>("Short")); shortScroll->Layout({0, 0, 320, 300});
        Check(shortScroll->CanDrag() && shortScroll->GetExtent() == 0); shortScroll->DragBy(-80); Check(shortScroll->GetOffset() < 0);
        shortScroll->Fling(0); Settle(*shortScroll); Check(shortScroll->GetOffset() == 0);
        for (bool edge : {false, true})
        {
            std::vector<float> positions;
            for (int hz : {30, 60, 90, 120})
            {
                auto s = Scroll(); if (edge) s->SetOffset(s->GetExtent() - 12); else s->DragBy(-120);
                s->Fling(edge ? 1800.f : 0.f);
                for (int i = 0; i < hz / 5; ++i) s->Tick(1.f / hz);
                positions.push_back(s->GetOffset()); Settle(*s, 1.f / hz); Check(s->GetOffset() == (edge ? s->GetExtent() : 0));
            }
            for (float position : positions) Check(std::abs(position - positions.front()) < .01f);
        }
        Context ui; auto content = std::make_shared<Content>(); auto scroll = std::make_shared<ScrollView>(content); ui.SetRoot(scroll);
        auto w = View(); ui.BuildFrame(w, 0); const int measured = content->measures, laidOut = content->layouts;
        for (int i = 0; i < 60; ++i) { scroll->SetOffset(i * 2.3f); ui.BuildFrame(w, 0); }
        Check(content->measures == measured && content->layouts == laidOut);
        Check(std::abs(content->GetBounds().y + scroll->GetOffset()) < .01f);
        Event down; down.type = EventType::TouchDown; down.x = down.y = 100; down.pointerId = 3;
        scroll->SetOffset(0); ui.HandleEvent(down, w);
        auto move = down; move.type = EventType::TouchMove; move.y = 200; ui.HandleEvent(move, w); Check(scroll->GetOffset() < 0);
        auto cancel = move; cancel.type = EventType::TouchCancel; ui.HandleEvent(cancel, w);
        for (int i = 0; i < 120; ++i) ui.BuildFrame(w, 1.0 / 60); Check(scroll->GetOffset() == 0);
        scroll->DragBy(-70); scroll->Fling(400); ui.HandleEvent(Event{EventType::FocusLost}, w); ui.BuildFrame(w, .1); Check(scroll->GetOffset() == 0);
        scroll->DragBy(-70); w.height = w.contentHeight = 250; ui.BuildFrame(w, 0); Check(scroll->GetOffset() == 0);
        down.timestamp = 1; ui.HandleEvent(down, w); move.y = 60; move.timestamp = 1.016; ui.HandleEvent(move, w);
        auto release = move; release.type = EventType::TouchUp; release.timestamp = 1.3; ui.HandleEvent(release, w);
        const float paused = scroll->GetOffset(); ui.BuildFrame(w, .1); Check(scroll->GetOffset() == paused); // queued release after a real pause
        std::puts("Elastic edges, both axes, short content, cancel/regrab/resize, clamping, 30/60/120 Hz spring and paint-only movement passed.");
    }
    void TextReuse()
    {
        for (float scale : {.75f, 1.f, 1.25f, 1.5f, 2.625f, 4.f})
        {
            Canvas canvas; RHICommandList commands; auto w = View(); w.scale = scale;
            RHITextureHandle original;
            for (int i = 0; i < 40; ++i)
            {
                const float offset = i * .37f;
                canvas.Begin(commands, w, {}); canvas.PushClip({0, 0, 80, 80}); canvas.PushScrollOffset({0, offset});
                canvas.Text({13.37f, 22.73f - offset}, "Чёткость", 14, {1, 1, 1, 1});
                canvas.PopScrollOffset(); canvas.PopClip(); canvas.End();
                const auto& draw = std::get<RHIDrawTriangles>(commands.GetCommands().front());
                if (!original) original = draw.texture;
                Check(draw.texture == original);
                const auto& texture = draw.texture->GetDesc();
                for (std::size_t q = 0; q < draw.vertices.size(); q += 6)
                {
                    const auto a = draw.vertices[q], b = draw.vertices[q + 2];
                    const float width = (b.x - a.x) * w.width / 2, texels = (b.u - a.u) * texture.width;
                    Check(std::abs(width - texels) < .001f && texture.filter == RHIFilter::Nearest);
                }
            }
        }
        std::puts("Scroll text cache reuses the same native-resolution texture at 75-400% DPI, including clipped lines.");
    }
    void SettlePaint()
    {
        for (float scale : {.75f, 1.f, 1.25f, 2.625f, 4.f}) for (auto axis : {Axis::Vertical, Axis::Horizontal})
        {
            Context ui; auto content = std::make_shared<Content>();
            auto scroll = std::make_shared<ScrollView>(content, axis); ui.SetRoot(scroll);
            auto w = View(); w.scale = scale; ui.BuildFrame(w, 0);
            scroll->DragBy(-120); ui.BuildFrame(w, 0); scroll->Fling(0);
            int skipped = 0;
            for (int i = 0; i < 180; ++i) {
                const auto revision = ui.GetFrameRevision(); const float before = scroll->GetOffset();
                ui.BuildFrame(w, 1.0 / 120);
                const bool movedPixel = std::round(before * scale) != std::round(scroll->GetOffset() * scale);
                Check((ui.GetFrameRevision() != revision) == movedPixel);
                if (before != scroll->GetOffset() && !movedPixel) ++skipped;
                const auto bounds = content->GetBounds();
                Check(std::abs((axis == Axis::Vertical ? bounds.y : bounds.x) + scroll->GetOffset()) < .01f);
            }
            Check(skipped > 0 && scroll->GetOffset() == 0);
            const auto revision = ui.GetFrameRevision(); w.scale *= 1.1f; ui.BuildFrame(w, 0);
            Check(ui.GetFrameRevision() > revision); // A changed DPI cannot use the stale scale.
        }
        std::puts("Spring tail: only changed physical offsets repaint; logical hit geometry and DPI updates preserved.");
    }
    void ReleaseVelocity()
    {
        Context ui; auto actual = Scroll(); auto reference = Scroll();
        ui.SetRoot(actual); auto window = View(); ui.BuildFrame(window, 0);
        Event input; input.type = EventType::TouchDown; input.x = 100; input.y = 40; input.timestamp = 1; input.pointerId = 2;
        ui.HandleEvent(input, window); reference->BeginDrag(); float speed = 0;
        for (int i = 1; i <= 4; ++i)
        {
            input.type = EventType::TouchMove; input.y += 40; input.timestamp = 1 + i * .016;
            ui.HandleEvent(input, window);
            const float before = reference->GetOffset(); reference->DragBy(-40);
            speed += (1 - std::exp(-.016f / .04f)) * ((reference->GetOffset() - before) / .016f - speed);
        }
        input.type = EventType::TouchUp; input.timestamp += .001; ui.HandleEvent(input, window);
        reference->Fling(speed);
        for (int i = 0; i < 60; ++i) { ui.BuildFrame(window, 1.0 / 60); reference->Tick(1.f / 60); Check(std::abs(actual->GetOffset() - reference->GetOffset()) < .001f); }
        std::puts("Rubber release uses content velocity, not unresisted finger velocity.");
    }
    void PausedRelease()
    {
        for (bool touch : {false, true}) {
            Context ui; auto s = Scroll(); s->SetRasterization(ScrollRasterization::Subpixel); ui.SetRoot(s);
            auto w = View(); ui.BuildFrame(w, 0);
            Event e; e.type = touch ? EventType::TouchDown : EventType::MouseDown;
            e.x = e.y = 100; e.timestamp = 1; ui.HandleEvent(e, w);
            e.type = touch ? EventType::TouchMove : EventType::MouseMove; e.y += 40; e.timestamp = 1.016; ui.HandleEvent(e, w);
            e.y += 40; e.timestamp += .016; ui.HandleEvent(e, w);
            // Holding still for 80 ms must not revive the old outward velocity.
            const float held = s->GetOffset();
            e.type = touch ? EventType::TouchUp : EventType::MouseUp; e.timestamp += .080;
            ui.HandleEvent(e, w); ui.BuildFrame(w, .001);
            std::printf("Paused release first step: %.6f dp (%s)\n", s->GetOffset() - held, touch ? "touch" : "mouse");
            Check(s->GetOffset() >= held);
        }
    }
    void ReleaseFrameTiming()
    {
        for (bool touch : {false, true}) for (auto axis : {Axis::Vertical, Axis::Horizontal})
        for (bool cancel : {false, true}) for (int hz : {30, 60, 120})
        {
            Context ui; auto actual = Scroll(axis), reference = Scroll(axis);
            actual->SetRasterization(ScrollRasterization::Subpixel); reference->SetRasterization(ScrollRasterization::Subpixel);
            ui.SetRoot(actual); auto w = View(); w.frameTimeSeconds = 1; ui.BuildFrame(w, 0);
            Event e; e.type = touch ? EventType::TouchDown : EventType::MouseDown;
            e.x = e.y = 40; e.timestamp = 1; ui.HandleEvent(e, w); reference->BeginDrag();
            float speed = 0;
            for (int i = 1; i <= 2; ++i) {
                e.type = touch ? EventType::TouchMove : EventType::MouseMove;
                (axis == Axis::Vertical ? e.y : e.x) += 40; e.timestamp = 1 + i * .016;
                ui.HandleEvent(e, w);
                const float before = reference->GetOffset(); reference->DragBy(-40);
                speed += (1 - std::exp(-.016f / .04f)) * ((reference->GetOffset() - before) / .016f - speed);
            }
            // The resampling delay lands exactly on the last real sample.
            w.frameTimeSeconds = 1.040; ui.BuildFrame(w, 0);
            Check(std::abs(actual->GetOffset() - reference->GetOffset()) < .001f);
            const double step = 1.0 / hz;
            const double lastMove = w.frameTimeSeconds + step - .002;
            const float moveDelta = static_cast<float>(lastMove - e.timestamp);
            e.timestamp = lastMove; (axis == Axis::Vertical ? e.y : e.x) += 40;
            ui.HandleEvent(e, w);
            const float before = reference->GetOffset(); reference->DragBy(-40);
            speed += (1 - std::exp(-moveDelta / .04f)) * ((reference->GetOffset() - before) / moveDelta - speed);
            e.timestamp = w.frameTimeSeconds + step - .001;
            e.type = cancel ? (touch ? EventType::TouchCancel : EventType::MouseCancel)
                            : (touch ? EventType::TouchUp : EventType::MouseUp);
            ui.HandleEvent(e, w); reference->Fling(cancel ? 0.f : speed);
            w.frameTimeSeconds += step; ui.BuildFrame(w, step); reference->Tick(.001f);
            const float error = std::abs(actual->GetOffset() - reference->GetOffset());
            if (error >= .001f) std::printf("Release frame error: %.6f dp (%d Hz, %s, %s)\n", error, hz, touch ? "touch" : "mouse", cancel ? "cancel" : "up");
            Check(error < .001f);
            for (int i = 0; i < hz; ++i) {
                w.frameTimeSeconds += step; ui.BuildFrame(w, step); reference->Tick(static_cast<float>(step));
                Check(std::abs(actual->GetOffset() - reference->GetOffset()) < .001f);
            }
        }
        std::puts("Release/cancel first spring step uses event time, not pre-release frame time: mouse/touch, both axes, 30/60/120 Hz passed.");
    }
    void ReleaseClockLifecycle()
    {
        struct TickContent final : Widget {
            float seconds = 0;
            Size OnMeasure(const Constraints&) const override { return {1000, 1000}; }
            bool OnTick(float dt) override { seconds = dt; return false; }
        };
        for (int action = 0; action < 3; ++action) {
            Context ui; auto child = std::make_shared<TickContent>();
            auto s = std::make_shared<ScrollView>(child); s->SetRasterization(ScrollRasterization::Subpixel); ui.SetRoot(s);
            auto w = View(); w.frameTimeSeconds = 1; ui.BuildFrame(w, 0);
            Event e; e.type = EventType::MouseDown; e.x = e.y = 100; e.timestamp = 1; ui.HandleEvent(e, w);
            e.type = EventType::MouseMove; e.y += 80; e.timestamp = 1.016; ui.HandleEvent(e, w);
            const float held = s->GetOffset();
            e.type = EventType::MouseCancel; e.timestamp = 1.050; ui.HandleEvent(e, w);
            w.frameTimeSeconds = 1.040; ui.BuildFrame(w, .016);
            Check(s->GetOffset() == held && child->seconds == .016f); // Future release waits; child animation does not.
            if (action == 1) {
                e.type = EventType::MouseDown; e.timestamp = 1.051; ui.HandleEvent(e, w);
            } else if (action == 2) s->Fling(0); // Programmatic animation supersedes a pending input clock.
            auto reference = Scroll(); reference->SetRasterization(ScrollRasterization::Subpixel); reference->DragBy(-80); reference->Fling(0);
            w.frameTimeSeconds = 1.060; ui.BuildFrame(w, .020);
            if (action == 1) Check(s->GetOffset() == held);
            else {
                reference->Tick(action == 0 ? .010f : .020f);
                Check(std::abs(s->GetOffset() - reference->GetOffset()) < .001f);
            }
            Check(child->seconds == .020f);
        }
        // No hover animation is armed by repeated mouse moves during a drag.
        Context ui; auto button = std::make_shared<Button>("Drag target", [] {});
        auto s = std::make_shared<ScrollView>(button); ui.SetRoot(s); auto w = View(); ui.BuildFrame(w, 0);
        Event e; e.type = EventType::MouseDown; e.x = e.y = 20; e.timestamp = 1; ui.HandleEvent(e, w);
        e.type = EventType::MouseMove; e.y = 40; e.timestamp = 1.016; ui.HandleEvent(e, w);
        Check(!button->IsHovered());
        e.timestamp = 1.032; ui.HandleEvent(e, w); Check(!button->IsHovered());
        e.type = EventType::MouseUp; ui.HandleEvent(e, w);
        e.type = EventType::MouseMove; ui.HandleEvent(e, w); Check(button->IsHovered());
        std::puts("Release clock: future event, child animation, regrab, programmatic restart and drag-hover suppression passed.");
    }
    void SubpixelScroll()
    {
        for (float scale : {.75f, 1.f, 1.25f, 2.625f, 4.f}) for (auto axis : {Axis::Vertical, Axis::Horizontal})
        {
            Context ui; auto scroll = Scroll(axis);
            scroll->SetRasterization(ScrollRasterization::Subpixel); ui.SetRoot(scroll);
            auto w = View(); w.scale = scale; ui.BuildFrame(w, 0);
            scroll->DragBy(-120); ui.BuildFrame(w, 0); scroll->Fling(0);
            int fractionalFrames = 0;
            for (int i = 0; i < 240; ++i)
            {
                const auto revision = ui.GetFrameRevision(); const float before = scroll->GetOffset();
                ui.BuildFrame(w, 1.0 / 120);
                Check((ui.GetFrameRevision() != revision) == (before != scroll->GetOffset()));
                if (before != scroll->GetOffset() && std::round(before * scale) == std::round(scroll->GetOffset() * scale))
                    ++fractionalFrames;
            }
            Check(fractionalFrames > 0 && scroll->GetOffset() == 0);
            scroll->SetRasterization(ScrollRasterization::PixelAligned);
            const auto revision = ui.GetFrameRevision(); ui.BuildFrame(w, 0); Check(ui.GetFrameRevision() > revision);
        }
        auto shapePosition = [](float offset, bool aligned) {
            Canvas canvas; RHICommandList list; auto w = View();
            canvas.Begin(list, w, {});
            canvas.PushScrollOffset({0, offset}, aligned);
            canvas.RoundedRect({20, 30 - offset, 100, 50}, 8, {1, 0, 0, 1});
            canvas.PopScrollOffset();
            canvas.RoundedRect({20, 100.2f, 100, 50}, 8, {1, 0, 0, 1});
            canvas.End();
            std::vector<float> y;
            for (const auto& command : list.GetCommands())
                if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) y.push_back(draw->vertices.front().y);
            return y;
        };
        const auto a = shapePosition(.2f, false), b = shapePosition(.3f, false);
        Check(a.size() == 2 && b.size() == 2 && a[0] != b[0] && a[1] == b[1]);
        const auto c = shapePosition(.2f, true), d = shapePosition(.3f, true); Check(c[0] == d[0]);
        Canvas textCanvas; RHICommandList textCommands; auto viewport = View();
        for (int i = 0; i < 256; ++i)
        {
            const float offset = i / 64.f;
            textCanvas.Begin(textCommands, viewport, {});
            textCanvas.PushScrollOffset({0, offset}, false);
            textCanvas.Text({20, 30 - offset}, "Native pixels", 16, {0, 0, 0, 1});
            textCanvas.PopScrollOffset(); textCanvas.End();
        }
        Check(textCanvas.GetTextCacheStats().runMisses <= 8);
        std::puts("Subpixel scroll repaints fractional movement, preserves shape precision and restores the outside pixel grid.");
    }
    void SpringTolerance()
    {
        for (float scale : {.75f, 1.f, 1.25f, 2.625f, 4.f})
        for (int hz : {30, 60, 90, 120})
        for (auto axis : {Axis::Vertical, Axis::Horizontal})
        for (bool trailing : {false, true})
        {
            Context ui; auto scroll = Scroll(axis); ui.SetRoot(scroll);
            auto window = View(); window.scale = scale; ui.BuildFrame(window, 0);
            const float target = trailing ? scroll->GetExtent() : 0.f;
            scroll->SetOffset(target); scroll->DragBy(trailing ? 120.f : -120.f);
            ui.BuildFrame(window, 0); scroll->Fling(0);
            double displacement = scroll->GetOffset() - target, velocity = 0;
            bool ended = false;
            for (int i = 0; i < hz * 2; ++i)
            {
                const double dt = 1.0 / hz, b = velocity + 18 * displacement, decay = std::exp(-18 * dt);
                displacement = (displacement + b * dt) * decay;
                velocity = (velocity - 18 * b * dt) * decay;
                const bool done = std::abs(displacement) * scale < 1 && std::abs(velocity) * scale < 20;
                ui.BuildFrame(window, dt);
                if (done)
                {
                    Check(scroll->GetOffset() == target);
                    const auto revision = ui.GetFrameRevision(); ui.BuildFrame(window, dt);
                    Check(ui.GetFrameRevision() == revision); ended = true; break;
                }
                Check(std::abs(scroll->GetOffset() - target - displacement) < .003);
            }
            Check(ended);
            // Being near an edge is not sufficient when moving quickly through it.
            scroll->SetOffset(0); scroll->DragBy(-.2f / scale); ui.BuildFrame(window, 0);
            scroll->Fling(100.f / scale); ui.BuildFrame(window, .0001);
            Check(scroll->GetOffset() != 0);
        }
        std::puts("Spring ends at physical-pixel distance/velocity tolerance; DPI, axes, edges and fast crossings passed.");
    }
    void FrameTiming()
    {
        for (int hz : {30, 60, 90, 120})
        {
            Beast::Detail::FrameClock clock; const std::int64_t start = 1'000'000'000;
            Check(clock.Step(start) == 0); double total = 0;
            for (int i = 1; i <= hz; ++i) total += clock.Step(start + std::int64_t(std::llround(i * 1e9 / hz)));
            Check(std::abs(total - 1) < 1e-8);
            Check(clock.Step(start) == 0); // stale callback
            Check(clock.Step(9'000'000'000) == .1); // long stall bounded
            clock.Reset(); Check(clock.Step(20'000'000'000) == 0); // resume does not animate through suspension
        }
        std::puts("Display clock: 30/60/90/120 Hz, stale timestamps, stall and resume passed.");
    }
    struct Stats { int created = 0, destroyed = 0, submitted = 0, updates = 0; };
    struct FakeRenderer final : RHI
    {
        Stats& stats; RHIBackend backend;
        FakeRenderer(Stats& s, RHIBackend b) : stats(s), backend(b) { ++stats.created; }
        ~FakeRenderer() override { ++stats.destroyed; }
        RHIBackend GetBackend() const noexcept override { return backend; }
        bool Submit(const Window&, const RHICommandList& list) override { Check(list.IsReady()); ++stats.submitted; return true; }
    };
    struct TestScene final : UI::Scene
    {
        Stats& stats; int clicks = 0, backDepth = 1;
        std::shared_ptr<Button> button;
        explicit TestScene(Stats& s) : stats(s)
        { button = std::make_shared<Button>("Test", [this] { ++clicks; }); ui.SetRoot(button); }
        bool OnBack() override { if (!backDepth) return false; --backDepth; return true; }
        void OnUpdate(double) override { ++stats.updates; }
    };
    struct Host final : UI::Application
    {
        Stats& stats;
        explicit Host(Stats& s) : UI::Application({RHIBackend::Vulkan, true, true}), stats(s) { PlatformWindow() = View(); }
        std::unique_ptr<UI::Scene> CreateScene() override { return std::make_unique<TestScene>(stats); }
        std::unique_ptr<RHI> CreateRenderer(const Window&, RHIOptions options) override
        { Check(options.backend == RHIBackend::Vulkan && options.vsync && options.validation); return std::make_unique<FakeRenderer>(stats, options.backend); }
    };
    void Lifecycle()
    {
        Stats stats; Host host(stats); Check(host.Configure().gpuRendering); host.OnStart();
        auto* scene = static_cast<TestScene*>(host.GetScene()); host.OnUpdate(.016); Check(stats.submitted == 0);
        host.Dispatch(Event{EventType::SurfaceCreated}); host.OnUpdate(.016); Check(stats.created == 1 && stats.submitted == 1);
        Event down; down.type = EventType::MouseDown; down.x = down.y = 20; host.Dispatch(down); Check(scene->button->IsPressed());
        auto up = down; up.type = EventType::MouseUp; host.Dispatch(up); Check(scene->clicks == 1);
        host.Dispatch(down); host.Dispatch(Event{EventType::SurfaceDestroyed}); Check(!scene->button->IsPressed() && stats.destroyed == 1);
        host.OnUpdate(.016); Check(stats.submitted == 1 && scene == host.GetScene());
        host.Dispatch(Event{EventType::SurfaceCreated}); host.OnUpdate(.016); Check(stats.created == 2 && stats.submitted == 2 && scene->clicks == 1);
        host.Dispatch(Event{EventType::Suspended}); host.OnUpdate(.016); Check(stats.submitted == 2 && stats.updates == 2);
        host.Dispatch(Event{EventType::Resumed}); host.OnUpdate(.016); Check(stats.submitted == 3);
        Event back; back.type = EventType::KeyDown; back.key = Key::Back; back.repeat = true; host.Dispatch(back); Check(scene->backDepth == 1);
        back.repeat = false; host.Dispatch(back); Check(scene->backDepth == 0 && !host.IsExitRequested());
        auto backUp = back; backUp.type = EventType::KeyUp; host.Dispatch(backUp); Check(!host.IsExitRequested());
        host.Dispatch(back); Check(host.IsExitRequested()); host.OnStop(); host.OnStop(); Check(!host.GetScene() && stats.destroyed == 2);
        Stats otherStats; Host other(otherStats); other.OnStart(); back.key = Key::Escape; back.repeat = true; other.Dispatch(back); Check(!other.IsExitRequested());
        back.repeat = false; other.Dispatch(back); Check(other.IsExitRequested()); other.OnStop();
        std::puts("SDK UI host: input, scene preservation, RHI create/destroy/recreate, suspend/resume, Back/Escape and cleanup passed.");
    }
    void IdlePresentation()
    {
        Stats stats; Host host(stats); host.OnStart(); host.Dispatch(Event{EventType::SurfaceCreated});
        host.OnUpdate(.016); const auto revision = host.GetScene()->ui.GetFrameRevision();
        for (int i = 0; i < 120; ++i) host.OnUpdate(1.0 / 120);
        Check(stats.submitted == 1 && stats.updates == 121 && host.GetScene()->ui.GetFrameRevision() == revision);
        host.Dispatch(Event{EventType::Resized}); host.OnUpdate(.016); Check(stats.submitted == 2);
        host.Dispatch(Event{EventType::SurfaceDestroyed}); host.Dispatch(Event{EventType::SurfaceCreated});
        host.OnUpdate(.016); Check(stats.submitted == 3); host.OnStop();
        std::puts("Idle UI skips GPU submission; scene updates, redraw and surface restoration remain active.");
    }
    class NativeScene final : public UI::Scene
    {
        Beast::Application& host_;
        std::shared_ptr<ScrollView> scroll_;
        int frame_ = 0;
    public:
        explicit NativeScene(Beast::Application& host) : host_(host)
        {
            auto column = std::make_shared<Column>();
            for (int i = 0; i < 40; ++i) column->Add(std::make_shared<Text>("SDK scroll " + std::to_string(i)));
            scroll_ = std::make_shared<ScrollView>(column); ui.SetRoot(scroll_);
        }
        void OnUpdate(double) override
        {
            if (frame_ == 2) scroll_->DragBy(-120);
            if (frame_ == 5) scroll_->Fling(0);
            if (++frame_ == 45) host_.RequestExit();
        }
    };
    class NativeHost final : public UI::Application
    {
        std::unique_ptr<UI::Scene> CreateScene() override { return std::make_unique<NativeScene>(*this); }
        WindowDefinition ConfigureWindow() const override { WindowDefinition w; w.width = 320; w.height = 400; w.title = "Beast SDK UI host test"; return w; }
    public:
        explicit NativeHost(RHIBackend api) : UI::Application({api, true, true}) {}
    };
}
int main()
{
    try {
        Sampling(); SamplingLifecycle(); Motion(); SafeArea(); FrameTiming(); Physics(); ReleaseVelocity(); PausedRelease(); ReleaseFrameTiming(); ReleaseClockLifecycle(); SpringTolerance(); SubpixelScroll(); TextReuse(); SettlePaint(); Lifecycle(); IdlePresentation();
        for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { NativeHost host(api); Check(Run(host) == 0 && !host.GetScene()); }
        std::puts("SDK UI host real OpenGL/Vulkan surface, rendering, overscroll animation and shutdown passed."); return 0;
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
