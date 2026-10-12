#include "Beast.h"
#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <source_location>
using namespace Beast;
using namespace Beast::UI;
using namespace Beast::UI::Build;
namespace {
    constexpr std::array Scales{.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.625f, 3.f, 4.f};
    void Check(bool value, std::source_location at = std::source_location::current())
    { if (!value) throw std::runtime_error("Layer assertion line " + std::to_string(at.line())); }
    template<class F> void Reject(F f) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } Check(rejected); }
    Window View() { Window w; w.width = w.contentWidth = 960; w.height = w.contentHeight = 640; return w; }
    void Record(Canvas& c, RHICommandList& list, const Window& w, bool yellow = false) {
        Theme theme; theme.background = {1, 1, 1, 1}; c.Begin(list, w, theme);
        c.SaveLayer({10, 10, 80, 60}, .5f);
        c.ClipRRect({10, 10, 80, 60}, 16);
        c.RoundedRect({10, 10, 80, 60}, 0, {1, yellow ? 1.f : 0.f, 0, 1});
        c.RoundedRect({50, 10, 40, 60}, 0, {0, 0, 1, 1}); // Overlap must be blue, never purple.
        c.RoundedRect({10, 10, 80, 10}, 0, {0, 1, 0, 1}); // Asymmetric top tests render-target orientation.
        c.Restore();
        c.RoundedRect({100, 10, 10, 10}, 0, {0, 0, 0, 1}); // Parent scissor/viewport restored.
        c.Save(); c.Translate(110, 90); c.Scale(1.25f);
        c.SaveLayer({0, 0, 60, 40}, .5f);
        c.SaveLayer({0, 0, 60, 40}, .5f);
        c.RoundedRect({0, 0, 60, 40}, 0, {0, 0, 1, 1});
        c.Restore(); c.Restore(); c.Restore(); // Nested alpha=.25, no cumulative transform leakage.
        c.Text({10, 90}, "Layer", 14, {0, 0, 0, 1});
        c.SaveLayer({10, 120, 80, 25}); c.Text({10, 120}, "Layer", 14, {0, 0, 0, 1}); c.Restore();
        c.Save(); c.ClipRect({200, 20, 30, 30}); c.ClipRRect({190, 10, 60, 60}, 20);
        c.RoundedRect({190, 10, 60, 60}, 0, {1, 0, 0, 1}); c.Restore();
        c.SaveLayer({5000, 5000, 50, 50}); c.RoundedRect({5000, 5000, 50, 50}, 0, {1, 0, 0, 1}); c.Restore();
        c.End();
    }
    void CpuTests() {
        auto w = View(); Canvas c; RHICommandList commands;
        Reject([&] { c.Restore(); });
        c.Begin(commands, w, {}); Reject([&] { c.ClipRRect({0, 0, 20, 20}, 5); });
        Reject([&] { c.SaveLayer({0, 0, 10, 10}, 2); });
        Reject([&] { c.Scale(0); }); c.Save(); Reject([&] { c.End(); }); c.Restore(); c.End();
        Record(c, commands, w);
        const auto& first = std::get<RHIDrawTriangles>(commands.GetCommands().front());
        Check(first.texture->GetLayerDepth() == 2 && first.texture->GetDesc().pixels.empty());
        auto retained = first.texture; Record(c, commands, w); Check(retained->GetContent()->IsReady());
        auto malformed = RHICommandList{}; Reject([&] { RHI::CreateLayerTexture(4, 4, malformed); });
        malformed.ClearFrameBuffer({1, 0, 0, .5f}); Reject([&] { RHI::CreateLayerTexture(4, 4, malformed); });
        Context ui; int clicks = 0;
        auto clip = Make<ClipRRect>(Make<Button>("Tap", [&] { ++clicks; }), 40.f);
        ui.SetRoot(Centered(Sized(clip, 80, 80))); ui.BuildFrame(w, 0);
        auto r = clip->GetBounds(); Check(!clip->ContainsPoint({r.x + 1, r.y + 1})); Check(clip->ContainsPoint({r.x + 40, r.y + 40}));
        auto tap = [&](float x, float y) { Event e{EventType::TouchDown}; e.x = x; e.y = y; e.pointerId = 1;
            ui.HandleEvent(e, w); e.type = EventType::TouchUp; ui.HandleEvent(e, w); };
        tap(r.x + 1, r.y + 1); Check(clicks == 0); tap(r.x + 40, r.y + 40); Check(clicks == 1);
        ui.SetRoot(nullptr); auto opacity = Make<Opacity>(Make<Text>("Group"), .5f); ui.SetRoot(opacity);
        Check(!ui.BuildFrame(w, 0).GetCommands().empty()); opacity->SetOpacity(0); Check(ui.BuildFrame(w, 0).GetCommands().empty());
        Reject([&] { opacity->SetOpacity(-1); });
        struct EmptyScene : Scene {};
        SceneApplication<EmptyScene> app({.title = "Simple", .width = 430, .height = 820, .backend = RHIBackend::Vulkan});
        Check(app.Configure().width == 430 && app.Configure().height == 820 && app.Configure().gpuRendering);
        std::puts("Layers CPU: snapshots, invalid state, offscreen culling, rounded hit testing, opacity and simple host passed.");
    }
    // Independent numerical pixel-area reference, only invoked at boundary pixels.
    float Area(float x, float y, float width, float height, float radius) {
        const float dx = std::max(std::abs(x - width / 2) - (width / 2 - radius), 0.f);
        const float dy = std::max(std::abs(y - height / 2) - (height / 2 - radius), 0.f);
        const float distance = std::hypot(dx, dy) - radius;
        if (x >= .5f && y >= .5f && x <= width - .5f && y <= height - .5f && distance < -.71f) return 1;
        if (distance > .71f) return 0;
        float area = 0;
        for (int i = 0; i < 256; ++i) {
            const float px = x - .5f + (i + .5f) / 256;
            if (px < 0 || px > width) continue;
            const float q = std::max(std::abs(px - width / 2) - (width / 2 - radius), 0.f);
            const float cap = radius - std::sqrt(std::max(0.f, radius * radius - q * q));
            area += std::max(0.f, std::min(y + .5f, height - cap) - std::max(y - .5f, cap));
        }
        return area / 256;
    }
    void Verify(const Window& w, int width, int height, const std::vector<unsigned char>& pixels, bool yellow = false) {
        const auto px = [&](float x) { return w.contentX + static_cast<int>(std::round(x * w.scale)); };
        const auto py = [&](float y) { return w.contentY + static_cast<int>(std::round(y * w.scale)); };
        const auto at = [&](int x, int y, int channel) { return pixels[(static_cast<std::size_t>(height - 1 - y) * width + x) * 4 + channel]; };
        const int left = px(10), top = py(10), right = px(90), bottom = py(70);
        int maximum = 0;
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            const float a = .5f * Area(x + .5f - left, y + .5f - top, float(right - left), float(bottom - top), 16 * w.scale);
            const int colored = y < py(20) ? 1 : x < px(50) ? 0 : 2;
            for (int channel = 0; channel < 3; ++channel) {
                const int expected = static_cast<int>(std::round(255 * (channel == colored || (yellow && colored == 0 && channel == 1) ? 1 : 1 - a)));
                const int error = std::abs(expected - at(x, y, channel));
                if (error > maximum && error > 3) std::printf("Mismatch (%d,%d) channel %d actual %d expected %d coverage %.3f\n", x, y, channel, at(x, y, channel), expected, a);
                maximum = std::max(maximum, error);
            }
        }
        std::printf("Layer DPI %.3f: reference max error=%d/255\n", w.scale, maximum); Check(maximum <= 2);
        Check(at(px(105), py(15), 0) == 0);
        Check(std::abs(int(at(px(130), py(110), 0)) - 191) <= 1 && at(px(130), py(110), 2) == 255);
        Check(at(px(201), py(21), 1) == 0 && at(px(198), py(21), 1) == 255);
        // Translation to a layer is integral physical pixels: identical text phases.
        const int shift = py(120) - py(90);
        if (std::abs(30 * w.scale - std::round(30 * w.scale)) < .001f)
            for (int y = py(90); y < py(110); ++y) for (int x = px(10); x < px(80); ++x)
                Check(std::abs(int(at(x, y, 0)) - int(at(x, y + shift, 0))) <= 1);
    }
    class Readback final : public Beast::Detail::OpenGLContext {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_; PFNGLREADPIXELSPROC read_;
        std::function<void(int, int, const std::vector<unsigned char>&)> verify_;
        inline static Readback* active_ = nullptr;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
            auto& self = *active_; std::vector<unsigned char> pixels(static_cast<std::size_t>(x1) * y1 * 4);
            self.read_(0, 0, x1, y1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); self.verify_(x1, y1, pixels);
            self.blit_(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        Readback(const Window& w, std::function<void(int, int, const std::vector<unsigned char>&)> verify) : verify_(std::move(verify)) {
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
        RHIBackend backend_; Canvas canvas_; RHICommandList commands_; std::unique_ptr<RHI> renderer_;
        Window viewport_; int attempts_ = 0;
        RHICommandList retained_;
        RHILayerStats previous_;
        bool yellow_ = false, prepared_ = false;
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {}
        WindowDefinition Configure() const override { WindowDefinition d; d.title = "Beast GPU layers"; d.width = 960; d.height = 640; d.gpuRendering = true; return d; }
        void CreateRenderer() {
            if (backend_ == RHIBackend::OpenGL) renderer_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<Readback>(GetWindow(),
                [this](int width, int height, const auto& pixels) { Verify(viewport_, width, height, pixels, yellow_); }), {backend_, true, true, true});
            else renderer_ = RHI::Create(GetWindow(), {backend_, true, true, true});
        }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceDestroyed) renderer_.reset();
            if (e.type == EventType::SurfaceCreated) CreateRenderer();
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 300); if (!renderer_) return;
            const int phase = frames % 6;
            if (!prepared_) {
                if (phase == 0) { viewport_ = GetWindow(); viewport_.scale = Scales[frames / 6];
                    viewport_.contentX = 9; viewport_.contentY = 11; viewport_.contentWidth -= 9; viewport_.contentHeight -= 11;
                    yellow_ = false; Record(canvas_, commands_, viewport_); retained_ = commands_; }
                if (phase == 2) { yellow_ = true; Record(canvas_, commands_, viewport_, true); }
                if (phase == 3) { yellow_ = false; commands_ = retained_; } // Live old snapshot must remain red.
                if (phase == 4) Record(canvas_, commands_, viewport_); // Recycle the expired yellow targets, clear/repaint red.
                if (phase == 5) { retained_.Reset(); renderer_.reset(); CreateRenderer(); }
                prepared_ = true;
            }
            if (!renderer_->Submit(viewport_, commands_)) return;
            const auto frameStats = renderer_->GetFrameStats(); const auto stats = frameStats.layers;
            Check(frameStats.submitCpuMs >= 0 && frameStats.submittedFrames > 0);
            if (frameStats.gpuMs) Check(std::isfinite(*frameStats.gpuMs) && *frameStats.gpuMs >= 0 && frameStats.gpuFrame < frameStats.submittedFrames);
            if (phase == 1 || phase == 3) Check(stats.renders == previous_.renders);
            if (phase == 4) Check(stats.allocations == previous_.allocations && stats.reuses > previous_.reuses);
            Check(stats.pooledBytes <= 32ull * 1024 * 1024 && stats.pooledTargets <= 16);
            previous_ = stats; prepared_ = false;
            if (++frames == static_cast<int>(Scales.size()) * 6) RequestExit();
        }
    };
}
int main() {
    try {
        CpuTests();
        for (auto backend : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(backend); Check(Run(test) == 0 && test.frames == 54); }
        std::puts("Layers: 9 DPI, overlap/group alpha, nested clips, crop, transforms, native text, replay and RHI recreation passed. Vulkan runtime/validation passed.");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
