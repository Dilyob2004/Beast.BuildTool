#include "Beast.h"
#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <cstdio>
#include <cstring>
#include <source_location>
using namespace Beast;
using namespace Beast::UI;
using namespace Beast::UI::Build;
namespace {
    constexpr std::uint64_t Budget = 32ull * 1024 * 1024;
    constexpr std::array Scales{.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.625f, 3.f, 4.f};
    void Check(bool value, std::source_location at = std::source_location::current())
    { if (!value) throw std::runtime_error("Repaint assertion line " + std::to_string(at.line())); }
    struct Counter final : Widget {
        mutable int paints = 0;
        bool animated = false;
        void Change() { InvalidatePaint(); }
        Size OnMeasure(const Constraints&) const override { return {120, 60}; }
        bool OnTick(float) override { return animated; }
        void OnPaint(Canvas& c) const override { ++paints; c.RoundedRect(GetBounds(), 12, c.GetTheme().primary); }
    };
    Window View() { Window w; w.width = w.contentWidth = 480; w.height = w.contentHeight = 480; return w; }
    struct TransformHost final : Widget {
        float x = 20, scale = 1;
        explicit TransformHost(WidgetPtr child) { Adopt(std::move(child)); }
        void Change() { InvalidatePaint(); }
        Size OnMeasure(const Constraints&) const override { return {480, 480}; }
        void OnLayout() override { Children().front()->Layout({0, 0, 120, 60}); }
        void OnPaint(Canvas& c) const override { c.Save(); c.Translate(x, 20); c.Scale(scale); Widget::OnPaint(c); c.Restore(); }
    };
    void CpuTests() {
        {
            auto counter = Make<Counter>(); auto scroll = Make<ScrollView>(VBox({Space(20), Cached(counter), Space(1000)}));
            Context context; context.SetRoot(scroll); context.BuildFrame(View(), 0);
            for (int i = 0; i < 80; ++i) {
                scroll->SetOffset(i % 2 ? .00002f : 0.f);
                context.BuildFrame(View(), 0);
            }
            Check(counter->paints == 1); // no rerasterization from near-zero spring drift
        }
        auto w = View(); Context ui;
        auto a = Make<Counter>(), b = Make<Counter>();
        ui.SetRoot(VBox({Cached(a), Cached(b)}));
        const auto frame = [&] { ui.SetRepaintCacheBudget(Budget); ui.BuildFrame(w, 0); };
        frame(); Check(a->paints == 1 && b->paints == 1);
        frame(); Check(a->paints == 1 && b->paints == 1 && ui.GetRepaintCacheStats().hits == 2);
        a->Change(); frame(); Check(a->paints == 2 && b->paints == 1);
        a->animated = true; frame(); Check(a->paints == 3 && b->paints == 1); a->animated = false;
        Theme t; t.primary = {0, 1, 0, 1}; ui.SetTheme(t); frame(); Check(a->paints == 4 && b->paints == 2);
        w.scale = 1.25f; frame(); Check(a->paints == 5 && b->paints == 3);
        w.width = w.contentWidth = 500; frame(); Check(a->paints == 6 && b->paints == 4);
        ui.SetRepaintCacheBudget(0); ui.BuildFrame(w, 0); Check(ui.GetRepaintCacheStats().entries == 0 && a->paints == 7);
        // Animate/focus/disable a cached button without mutating its layout.
        auto button = Make<Button>("Ripple", [] {}); ui.SetRoot(Centered(Cached(button))); frame();
        auto misses = ui.GetRepaintCacheStats().misses;
        Event e{EventType::KeyDown}; e.key = Key::Tab; ui.HandleEvent(e, w); frame();
        Check(ui.GetRepaintCacheStats().misses > misses); misses = ui.GetRepaintCacheStats().misses;
        e.key = Key::Space; ui.HandleEvent(e, w); ui.BuildFrame(w, .05);
        Check(ui.GetRepaintCacheStats().misses > misses); misses = ui.GetRepaintCacheStats().misses;
        e.type = EventType::KeyUp; ui.HandleEvent(e, w); ui.BuildFrame(w, .05);
        Check(ui.GetRepaintCacheStats().misses > misses);
        button->SetEnabled(false); frame(); ui.HandleEvent(Event{EventType::FocusLost}, w); frame();
        // Scroll translation retains a fully visible card, but not a clipped edge.
        a = Make<Counter>(); auto scroll = Make<ScrollView>(VBox({Space(100), Cached(a), Space(1000)}));
        ui.SetRoot(scroll); frame(); auto count = a->paints;
        scroll->SetOffset(4 / w.scale); frame(); Check(a->paints == count);
        scroll->SetOffset(130); frame(); Check(a->paints == count + 1);
        scroll->SetOffset(140); frame(); Check(a->paints == count + 2);
        // Entry count/byte budget, weak ownership and disabled-cache fallback.
        std::vector<WidgetPtr> cards;
        for (int i = 0; i < 70; ++i) cards.push_back(Cached(Sized(Make<Counter>(), 4, 4)));
        ui.SetRoot(VBox(std::move(cards))); frame(); Check(ui.GetRepaintCacheStats().entries <= 64);
        ui.SetRepaintCacheBudget(4096); ui.BuildFrame(w, 0); Check(ui.GetRepaintCacheStats().cachedBytes <= 4096);
        ui.SetRoot(nullptr); ui.BuildFrame(w, 0); Check(ui.GetRepaintCacheStats().entries == 0);
        // An eight-layer subtree must work unchanged when wrapped in a cache hint.
        WidgetPtr nested = Make<Counter>(); for (int i = 0; i < 8; ++i) nested = Make<Opacity>(nested, .9f);
        ui.SetRoot(Cached(nested)); frame(); Check(ui.GetRepaintCacheStats().bypasses > 0);
        a = Make<Counter>(); auto transform = Make<TransformHost>(Cached(a)); ui.SetRoot(transform); frame();
        count = a->paints; transform->x += 4; transform->Change(); frame(); Check(a->paints == count);
        transform->x += .25f; transform->Change(); frame(); Check(a->paints == ++count);
        transform->x += .001f; transform->Change(); frame(); Check(a->paints == ++count); // Even within one 1/64 text phase.
        transform->x = 20;
        transform->scale = 1.5f; transform->Change(); frame(); Check(a->paints == ++count);
        struct Broken final : Widget {
            Size OnMeasure(const Constraints&) const override { return {120, 60}; }
            void OnPaint(Canvas& c) const override { c.Restore(); }
        };
        ui.SetRoot(Cached(Make<Broken>())); bool rejected = false;
        try { frame(); } catch (const std::logic_error&) { rejected = true; } Check(rejected);
        ui.SetRoot(Cached(Make<Counter>())); frame(); // Recovery after a malformed custom painter.
        std::puts("Repaint CPU: reuse, scoped mutations, ticks, theme/DPI/layout, focus/ripple, scroll, budgets, detach and depth fallback passed.");
    }
    class Readback final : public Beast::Detail::OpenGLContext {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_; PFNGLREADPIXELSPROC read_;
        std::function<void(const std::vector<unsigned char>&)> verify_;
        inline static Readback* active_ = nullptr;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
            auto& self = *active_; std::vector<unsigned char> pixels(static_cast<std::size_t>(x1) * y1 * 4);
            self.read_(0, 0, x1, y1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); self.verify_(pixels);
            self.blit_(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        Readback(const Window& w, std::function<void(const std::vector<unsigned char>&)> verify) : verify_(std::move(verify)) {
            actual_ = Beast::Detail::CreateOpenGLContext(w, {});
            blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(actual_->Load("glBlitFramebuffer"));
            read_ = reinterpret_cast<PFNGLREADPIXELSPROC>(actual_->LoadBase("glReadPixels")); Check(blit_ && read_); active_ = this;
        }
        void* Load(const char* name) override { return std::strcmp(name, "glBlitFramebuffer") == 0 ? reinterpret_cast<void*>(Blit) : actual_->Load(name); }
        void* LoadBase(const char* name) override { return actual_->LoadBase(name); }
        void MakeCurrent() override { actual_->MakeCurrent(); }
        bool Present() override { return actual_->Present(); }
        void GetSize(int& w, int& h) override { actual_->GetSize(w, h); }
    };
    class GpuTest final : public Beast::Application {
        RHIBackend backend_; Context cached_, direct_; std::shared_ptr<ScrollView> scroll_, referenceScroll_;
        std::unique_ptr<RHI> rhi_; Window viewport_; const RHICommandList* commands_ = nullptr;
        std::vector<unsigned char> reference_; int attempts_ = 0, maxError_ = 0; bool prepared_ = false;
        std::uint64_t renders_ = 0;
        static WidgetPtr Content() {
            ContainerStyle style; style.height = 80.f; style.padding = EdgeInsets::All(10); style.cornerRadius = 18;
            style.gradient = ContainerStyle::Gradient{{0, .5f, .15f, 1}, {0, .4f, .35f, 1}};
            style.borderColor = {.1f, .3f, .2f, 1}; style.borderWidth = 1;
            style.shadowColor = {0, .3f, 0, .4f}; style.shadowBlur = 8; style.shadowOffset = {0, 4};
            return VBox({Space(32), Inset(Cached(Box(Make<Text>("Sharp text", 14.f, RHIColor{1, 1, 1, 1}), style)), 20),
                Space(24), Cached(Make<Opacity>(Box(Make<Text>("Nested", 14.f), style), .7f)), Space(700)});
        }
        void CreateRenderer() {
            if (backend_ == RHIBackend::OpenGL) rhi_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<Readback>(GetWindow(),
                [this](const auto& pixels) {
                    if (frames % 4 == 0) { reference_ = pixels; return; }
                    Check(reference_.size() == pixels.size()); int error = 0;
                    for (std::size_t i = 0; i < pixels.size(); ++i) if (i % 4 != 3) error = std::max(error, std::abs(int(pixels[i]) - reference_[i]));
                    if (error > 3) std::printf("Repaint pixel mismatch frame=%d scale=%.3f max=%d\n", frames, viewport_.scale, error);
                    Check(error <= 3); maxError_ = std::max(maxError_, error);
                }), {backend_, false, true});
            else rhi_ = RHI::Create(GetWindow(), {backend_, false, true});
        }
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {
            scroll_ = Make<ScrollView>(Content()); referenceScroll_ = Make<ScrollView>(Content());
            cached_.SetRoot(scroll_); direct_.SetRoot(referenceScroll_); direct_.SetRepaintCacheBudget(0);
        }
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 720; w.height = 720; w.title = "Beast repaint cache tests"; w.gpuRendering = true; return w; }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceCreated) CreateRenderer();
            if (e.type == EventType::SurfaceDestroyed) rhi_.reset();
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 1000); if (!rhi_) return;
            const int phase = frames % 4, scenario = frames / 4;
            if (!prepared_) {
                viewport_ = GetWindow(); viewport_.scale = Scales[scenario / 4];
                viewport_.contentX = 7; viewport_.contentY = 13; viewport_.contentWidth -= 14; viewport_.contentHeight -= 26;
                if (phase == 0) {
                    cached_.BuildFrame(viewport_, 0); direct_.BuildFrame(viewport_, 0); // Layout at the new DPI first.
                    const float offset = std::array{0.f, 4.37f, 42.1f, 92.6f}[scenario % 4];
                    scroll_->SetOffset(offset); referenceScroll_->SetOffset(offset);
                    commands_ = &direct_.BuildFrame(viewport_, 0);
                } else {
                    cached_.SetRepaintCacheBudget(Budget); commands_ = &cached_.BuildFrame(viewport_, 0);
                    if (phase == 3) { rhi_.reset(); CreateRenderer(); }
                }
                prepared_ = true;
            }
            if (!rhi_->Submit(viewport_, *commands_)) return;
            const auto renders = rhi_->GetFrameStats().layers.renders;
            if (phase == 2 && viewport_.scale <= 3 && scenario % 4 <= 1)
                Check(renders == renders_); // Fully visible cache hits do not render native layers again.
            renders_ = renders; prepared_ = false;
            if (++frames == static_cast<int>(Scales.size()) * 16) {
                Check(cached_.GetRepaintCacheStats().hits > 0);
                std::printf("Repaint %s: 9 DPI, fractional scroll/edge clips, shadow overflow, text, nested opacity, hits and GPU recreation passed; pixel max error=%d/255 (GL readback only).\n",
                    backend_ == RHIBackend::OpenGL ? "OpenGL" : "Vulkan", maxError_); RequestExit();
            }
        }
    };
}
int main() {
    try {
        CpuTests();
        for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(api); Check(Run(test) == 0 && test.frames == 144); }
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
