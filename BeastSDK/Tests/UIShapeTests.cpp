#include "Beast.h"
#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <source_location>
#include <limits>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Beast;
using namespace Beast::UI;
namespace {
    constexpr std::array Scales{.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.625f, 3.f, 4.f};
    void Check(bool value, std::source_location line = std::source_location::current())
    { if (!value) throw std::runtime_error("UI shape assertion at line " + std::to_string(line.line())); }
    Window View(float scale = 1) {
        Window w; w.width = w.contentWidth = 1000; w.height = w.contentHeight = 740;
        w.scale = scale; return w;
    }
    void Pointer(Context& ui, const Window& w, EventType type, Point p, int id = 1) {
        Event e{type}; e.x = w.contentX + p.x * w.scale; e.y = w.contentY + p.y * w.scale; e.pointerId = id;
        ui.HandleEvent(e, w);
    }
    std::vector<RHIDrawConstants> Ink(const RHICommandList& list) {
        std::vector<RHIDrawConstants> result;
        for (const auto& command : list.GetCommands())
            if (const auto* draw = std::get_if<RHIDrawTriangles>(&command); draw && draw->constants[23] > 0) result.push_back(draw->constants);
        return result;
    }
    void InputTests() {
        for (float scale : Scales) {
            auto w = View(scale); Context ui; int clicks = 0;
            ContainerStyle style; style.width = 120.f; style.height = 90.f; style.cornerRadius = 19;
            style.background = RHIColor{.9f, .96f, .95f, 1}; style.borderWidth = 1; style.borderColor = {.5f, .8f, .7f, 1};
            auto card = std::make_shared<Pressable>(std::make_shared<Container>(nullptr, style), [&] { ++clicks; });
            ui.SetRoot(std::make_shared<Center>(card)); ui.BuildFrame(w, 0);
            const auto r = card->GetBounds(); const Point origin{r.x + 17, r.y + 23};
            Pointer(ui, w, EventType::TouchDown, origin);
            auto first = Ink(ui.BuildFrame(w, .04)); Check(first.size() == 1);
            Check(std::abs(first[0][2] - 19 * scale) < .01f);
            // The origin is the press point, not the center of the card (allow grid phase).
            Check(std::abs(first[0][20] - 17 * scale) <= .51f && std::abs(first[0][21] - 23 * scale) <= .51f);
            const float radius = first[0][22];
            Check(Ink(ui.BuildFrame(w, .05))[0][22] > radius);
            Pointer(ui, w, EventType::TouchUp, origin); Check(clicks == 1 && !card->IsPressed());
            Check(!Ink(ui.BuildFrame(w, .016)).empty()); // release does not erase the splash
            for (int i = 0; i < 8; ++i) ui.BuildFrame(w, .05);
            Check(Ink(ui.BuildFrame(w, 0)).empty());
            // A quick tap entirely between frames must still be visible.
            Pointer(ui, w, EventType::TouchDown, origin); Pointer(ui, w, EventType::TouchUp, origin);
            Check(Ink(ui.BuildFrame(w, .016)).size() == 1 && clicks == 2);
            ui.HandleEvent(Event{EventType::FocusLost}, w); Check(Ink(ui.BuildFrame(w, 0)).empty());
            Pointer(ui, w, EventType::TouchDown, origin); ui.BuildFrame(w, .04);
            Pointer(ui, w, EventType::TouchCancel, origin); Check(Ink(ui.BuildFrame(w, .1)).empty() && clicks == 2);
            Pointer(ui, w, EventType::TouchDown, origin); card->SetEnabled(false); card->SetEnabled(true);
            Pointer(ui, w, EventType::TouchUp, origin); Check(Ink(ui.BuildFrame(w, .016)).empty() && clicks == 2);
            // Long holds stop dirtying the frame once expansion is complete.
            Pointer(ui, w, EventType::TouchDown, origin);
            for (int i = 0; i < 6; ++i) ui.BuildFrame(w, .1);
            Check(!card->Tick(.016f));
            ui.HandleEvent(Event{EventType::SurfaceDestroyed}, w); Check(Ink(ui.BuildFrame(w, 0)).empty());
            for (int i = 0; i < 7; ++i) {
                Pointer(ui, w, EventType::TouchDown, origin); Pointer(ui, w, EventType::TouchUp, origin);
            }
            Check(Ink(ui.BuildFrame(w, .016)).size() == 4 && clicks == 9);
            ui.SetRoot(nullptr);
            ui.SetRoot(std::make_shared<Center>(card)); Check(Ink(ui.BuildFrame(w, .016)).empty());
            ui.SetRoot(nullptr);
            // Scrolling cancels both click and ink even when the child remains visible.
            style.height = 600.f; auto column = std::make_shared<Column>(std::vector<WidgetPtr>{card, std::make_shared<Container>(nullptr, style)});
            auto scroll = std::make_shared<ScrollView>(column);
            ContainerStyle view; view.height = 200.f; view.width = 160.f;
            ui.SetRoot(std::make_shared<Center>(std::make_shared<Container>(scroll, view))); ui.BuildFrame(w, 0);
            const auto s = card->GetBounds(); const Point p{s.x + 30, s.y + 30};
            Pointer(ui, w, EventType::TouchDown, p); ui.BuildFrame(w, .04);
            Pointer(ui, w, EventType::TouchMove, {p.x, p.y - 15});
            Pointer(ui, w, EventType::TouchUp, {p.x, p.y - 15});
            Check(!card->IsPressed() && clicks == 9 && Ink(ui.BuildFrame(w, .1)).empty());
        }
        // Keyboard activation starts at the center and key repeat doesn't add splashes.
        auto w = View(); Context ui; int clicks = 0; auto b = std::make_shared<Button>("Ripple", [&] { ++clicks; });
        ui.SetRoot(std::make_shared<Center>(b)); ui.BuildFrame(w, 0);
        Event key{EventType::KeyDown}; key.key = Key::Tab; ui.HandleEvent(key, w);
        key.key = Key::Space; ui.HandleEvent(key, w); key.repeat = true; ui.HandleEvent(key, w);
        auto ink = Ink(ui.BuildFrame(w, .04)); Check(ink.size() == 1);
        Check(std::abs(ink[0][20] - ink[0][0] / 2) <= .51f && std::abs(ink[0][21] - ink[0][1] / 2) <= .51f);
        key.type = EventType::KeyUp; ui.HandleEvent(key, w); Check(clicks == 1);
        std::puts("Ripple: pointer origin, keyboard, expansion/fade, fast taps, hold idle, disable, surface loss and scroll cancellation passed at 9 DPI scales.");
    }
    void Record(Canvas& canvas, RHICommandList& commands, const Window& w) {
        Theme theme; theme.background = {1, 1, 1, 1}; canvas.Begin(commands, w, theme);
        canvas.GradientRect({8.25f, 8.75f, 70.5f, 46.5f}, 16, {.9f, .98f, .96f, 1}, {.3f, .7f, .6f, 1}, {0, 0}, {1, 1}, 1, {.2f, .5f, .4f, 1});
        canvas.RoundedRect({86.25f, 8.75f, 50.5f, 46.5f}, 19, {0, 0, 0, 0}, .6f, {.1f, .6f, .4f, .6f});
        canvas.RoundedRect({143.25f, 8.75f, 40.5f, 40.5f}, 100, {.1f, .5f, .3f, .45f}, 2.25f, {.2f, .4f, .7f, .7f});
        canvas.RoundedRect({190.25f, 8.75f, 25.5f, 20.5f}, 0, {0, 0, 0, 0}, 30, {.8f, .3f, .2f, .6f}); // collapsed inner contour
        const Rect card{8.25f, 66.25f, 70.5f, 46.5f};
        canvas.RoundedRect(card, 16, {.9f, .97f, .95f, 1}, 1, {.5f, .8f, .7f, 1});
        canvas.InkRipple(card, 16, {20.25f, 74.25f}, 30, {.0f, .35f, .25f, .22f});
        // Same-sized layout ancestors used to cut the glow at the card bottom.
        ContainerStyle style; style.height = 40.5f; style.background = RHIColor{.1f, .5f, .3f, 1}; style.cornerRadius = 14;
        style.shadowColor = {.1f, .5f, .3f, .6f}; style.shadowBlur = 10; style.shadowOffset = {0, 4};
        const auto decoration = [&] {
            return std::make_shared<Padding>(0.0f, std::make_shared<Container>(std::make_shared<Container>(nullptr, style)));
        };
        auto shadow = decoration(); shadow->Measure(Constraints::Tight({66.5f, 40.5f}));
        shadow->Layout({98.25f, 66.25f, 66.5f, 40.5f}); shadow->Paint(canvas);
        // In contrast, a real scrolling viewport must keep effects inside it.
        auto scroll = std::make_shared<ScrollView>(decoration());
        scroll->Measure(Constraints::Tight({45.5f, 40.5f})); scroll->Layout({183.25f, 66.25f, 45.5f, 40.5f}); scroll->Paint(canvas);
        // Adjacent fractional clips/geometry: every pixel belongs to exactly one cell.
        for (int row = 0; row < 2; ++row) for (int col = 0; col < 8; ++col) {
            Rect cell{8.25f + col * 10.25f, 126.25f + row * 10.25f, 10.25f, 10.25f};
            canvas.PushClip(cell); canvas.RoundedRect(cell, 0, {0, 1, 0, 1}); canvas.PopClip();
        }
        canvas.End();
    }
    // Independent area reference: integrate 512 vertical slices of the actual
    // rounded shape, not the shader's eight samples/adaptive integration axis.
    double Distance(double x, double y, double width, double height, double radius) {
        const double qx = std::abs(x - width / 2) - (width / 2 - radius), qy = std::abs(y - height / 2) - (height / 2 - radius);
        return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - radius;
    }
    double Coverage(double d) { return std::clamp(.5 - d, 0.0, 1.0); }
    double Area(double px, double py, double width, double height, double radius) {
        if (std::min(width, height) <= 0) return 0;
        if (radius <= 0) return std::max(0.0, std::min(px + .5, width) - std::max(px - .5, 0.0)) *
            std::max(0.0, std::min(py + .5, height) - std::max(py - .5, 0.0));
        const double d = Distance(px, py, width, height, radius);
        if (d <= -std::sqrt(.5)) return 1;
        if (d >= std::sqrt(.5)) return 0;
        const double left = std::max(0.0, px - .5), right = std::min(width, px + .5);
        if (right <= left) return 0;
        const double step = (right - left) / 512; double area = 0;
        for (int i = 0; i < 512; ++i) {
            const double x = left + (i + .5) * step, dx = std::max({radius - x, x - (width - radius), 0.0});
            const double cap = radius - std::sqrt(std::max(0.0, radius * radius - dx * dx));
            area += std::max(0.0, std::min(py + .5, height - cap) - std::max(py - .5, cap));
        }
        return area * step;
    }
    void Verify(const RHICommandList& commands, const Window& w, int width, int height, const std::vector<unsigned char>& pixels) {
        std::vector<std::array<double, 4>> expected(static_cast<std::size_t>(width) * height, {1, 1, 1, 1});
        double oldCoverageError = 0;
        for (const auto& command : commands.GetCommands()) {
            const auto& draw = std::get<RHIDrawTriangles>(command); const auto& c = draw.constants; const auto clip = *draw.scissor;
            const double left = std::round((double(draw.vertices[0].x) + 1) * w.width / 2 - draw.vertices[0].u);
            const double top = std::round((double(draw.vertices[0].y) + 1) * w.height / 2 - draw.vertices[0].v);
            const int fringe = static_cast<int>(std::ceil(4 * c[29])) + 1;
            for (int y = std::max(clip.y, static_cast<int>(top) - fringe); y < std::min(clip.y + clip.height, static_cast<int>(top + c[1]) + fringe); ++y)
                for (int x = std::max(clip.x, static_cast<int>(left) - fringe); x < std::min(clip.x + clip.width, static_cast<int>(left + c[0]) + fringe); ++x) {
                    const double px = x + .5 - left, py = y + .5 - top;
                    const double outer = Area(px, py, c[0], c[1], c[2]); double inner = outer;
                    if (c[29] == 0) oldCoverageError = std::max(oldCoverageError, std::abs(Coverage(Distance(px, py, c[0], c[1], c[2])) - outer));
                    if (c[3] > 0) inner = std::min(outer, Area(px - c[3], py - c[3], c[0] - 2 * c[3], c[1] - 2 * c[3], std::max(0.f, c[2] - c[3])));
                    const double dx = c[14] - c[12], dy = c[15] - c[13];
                    const double t = std::clamp(((px / c[0] - c[12]) * dx + (py / c[1] - c[13]) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
                    std::array<double, 4> source{};
                    for (int i = 0; i < 4; ++i) source[i] = (c[4 + i] + (c[8 + i] - c[4 + i]) * t) * inner + c[16 + i] * (outer - inner);
                    if (c[23] > 0) {
                        const double coverage = std::min(outer, Coverage(std::hypot(px - c[20], py - c[21]) - c[22]));
                        for (int i = 0; i < 4; ++i) source[i] = c[24 + i] * coverage + source[i] * (1 - c[27] * coverage);
                    }
                    if (c[29] > 0) {
                        const double coverage = .5 * std::erfc(Distance(px, py, c[0], c[1], c[2]) / (c[29] * std::sqrt(2.0)));
                        for (int i = 0; i < 4; ++i) source[i] = c[4 + i] * coverage;
                    }
                    auto& target = expected[static_cast<std::size_t>(y) * width + x];
                    for (int i = 0; i < 4; ++i) target[i] = source[i] + target[i] * (1 - source[3]);
                }
        }
        int maxError = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) for (int channel = 0; channel < 4; ++channel) {
            const auto reference = expected[static_cast<std::size_t>(y) * width + x][channel];
            const int actual = pixels[(static_cast<std::size_t>(height - 1 - y) * width + x) * 4 + channel];
            maxError = std::max(maxError, std::abs(actual - static_cast<int>(std::round(reference * 255))));
        }
        std::printf("Analytic GPU/reference DPI %.3f max error %d/255\n", w.scale, maxError); Check(maxError <= 2);
        std::printf("Previous distance-ramp coverage deviates from area reference by up to %.2f/255\n", oldCoverageError * 255);
        const auto red = [&](double x, double y) {
            const int col = static_cast<int>(std::round(x * w.scale)), row = static_cast<int>(std::round(y * w.scale));
            return pixels[(static_cast<std::size_t>(height - 1 - row) * width + col) * 4];
        };
        // Visible below layout bounds; smoothly fades, but never escapes a ScrollView.
        const int nearShadow = red(130, 109), farShadow = red(130, 122);
        Check(nearShadow < 230 && farShadow > 250 && farShadow > nearShadow);
        Check(red(200, 86) < 100 && red(200, 109) == 255);
        for (int y = static_cast<int>(std::round(126.25 * w.scale)); y < static_cast<int>(std::round(146.75 * w.scale)); ++y)
            for (int x = static_cast<int>(std::round(8.25 * w.scale)); x < static_cast<int>(std::round(90.25 * w.scale)); ++x) {
                const auto i = (static_cast<std::size_t>(height - 1 - y) * width + x) * 4;
                Check(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0);
            }
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
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {}
        WindowDefinition Configure() const override { WindowDefinition d; d.title = "Beast analytic curves / border / ripple tests"; d.width = 1000; d.height = 740; d.gpuRendering = true; return d; }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceDestroyed) renderer_.reset();
            if (e.type == EventType::SurfaceCreated) {
                if (backend_ == RHIBackend::OpenGL) renderer_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<Readback>(GetWindow(),
                    [this](int width, int height, const auto& pixels) { Verify(commands_, viewport_, width, height, pixels); }));
                else renderer_ = RHI::Create(GetWindow(), {backend_, true, true});
            }
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 300); if (!renderer_) return;
            viewport_ = GetWindow(); viewport_.scale = Scales[frames]; Record(canvas_, commands_, viewport_);
            if (renderer_->Submit(viewport_, commands_) && ++frames == static_cast<int>(Scales.size())) RequestExit();
        }
    };
}
int main() {
    try {
        InputTests();
        for (auto backend : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(backend); Check(Run(test) == 0 && test.frames == 9); }
        std::puts("Shapes: 9 scales, independent area reference, gap-free grid, borders, ripple, shadow overflow/falloff and viewport clipping; Vulkan runtime/validation passed.");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
