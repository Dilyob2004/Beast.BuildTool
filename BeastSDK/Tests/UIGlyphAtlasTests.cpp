#include "Beast.h"
#include "UI/Canvas.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include <cstdio>
#include <cstring>
#include <source_location>
#include <unordered_set>
using namespace Beast;
using namespace Beast::UI;
using namespace Beast::UI::Build;
namespace {
    constexpr std::array Scales{.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.625f, 3.f, 4.f};
    void Check(bool value, std::source_location at = std::source_location::current())
    { if (!value) throw std::runtime_error("Glyph atlas assertion at line " + std::to_string(at.line())); }
    template<class F> void Reject(F action) { bool rejected = false; try { action(); } catch (const std::logic_error&) { rejected = true; } Check(rejected); }
    Window View() { Window w; w.width = w.contentWidth = 800; w.height = w.contentHeight = 600; return w; }
    void ReferenceTest() {
        UI::Detail::GlyphAtlas atlas; std::vector<UI::Detail::GlyphAtlas::Quad> quads;
        int maximum = 0;
        for (float scale : Scales) for (auto face : {FontFace::Regular, FontFace::Bold, FontFace::Icons})
            for (const auto phase : {Point{}, Point{.375f, .734375f}, Point{.984375f, .015625f}})
                for (const std::string text : {"Чёткость Aa 0123", "AVATAR To WA", "a\xcc\x81 a\xcc\x88", "office ffi q\u0301", "ffi कि ♜ नमस्ते", "\xee\x88\x97", "\xff\xc0\x80"}) {
                    atlas.BeginFrame(); Check(atlas.Resolve(text, 14 * scale, phase, face, false, quads)); atlas.EndFrame();
                    const auto reference = UI::Detail::RasterizeText(text, 14 * scale, 4096, phase, 4096, face);
                    const auto& d = reference.texture->GetDesc(); std::vector<unsigned char> actual(std::size_t(d.width) * d.height);
                    for (const auto& q : quads) {
                        Check(q.texture->IsReady()); const auto& t = q.texture->GetDesc();
                        const int tx = static_cast<int>(std::round(q.uv.x * t.width)), ty = static_cast<int>(std::round(q.uv.y * t.height));
                        Check(std::abs(q.uv.width * t.width - q.pixels.width) < .001f && t.filter == RHIFilter::Nearest);
                        for (int row = 0; row < static_cast<int>(q.pixels.height); ++row) for (int col = 0; col < static_cast<int>(q.pixels.width); ++col) {
                            const int x = static_cast<int>(q.pixels.x) + col, y = static_cast<int>(q.pixels.y) + row;
                            Check(x >= 0 && y >= 0 && x < static_cast<int>(d.width) && y < static_cast<int>(d.height));
                            auto& dst = actual[std::size_t(y) * d.width + x];
                            const unsigned src = t.pixels[(std::size_t(ty + row) * t.width + tx + col) * 4];
                            dst = static_cast<unsigned char>(src + (dst * (255 - src) + 127) / 255);
                        }
                    }
                    int error = 0; for (std::size_t i = 0; i < actual.size(); ++i) error = std::max(error, std::abs(int(actual[i]) - d.pixels[i * 4]));
                    if (error > 2) std::printf("Atlas/reference error=%d scale=%.3f face=%d text=%s\n", error, scale, int(face), text.c_str());
                    Check(error <= 2); maximum = std::max(maximum, error);
                }
        std::printf("Glyph/reference coverage: 9 DPI, 3 faces, kerning, combining marks, invalid UTF-8 and phases; max error=%d/255.\n", maximum);
    }
    void LifetimeTests() {
        Canvas c; auto w = View(); RHICommandList commands;
        auto record = [&](const std::string& text) { c.Begin(commands, w, {}); c.Text({10, 10}, text, 16, {1, 1, 1, 1}); c.End(); };
        record("AAAA"); const auto before = c.GetTextCacheStats(); Check(before.glyphMisses > 0 && before.pages > 0);
        const auto retained = commands; const auto texture = std::get<RHIDrawTriangles>(retained.GetCommands().front()).texture;
        const auto pixels = texture->GetDesc().pixels;
        record("AAAA"); auto after = c.GetTextCacheStats(); Check(after.glyphMisses == before.glyphMisses && after.pagesCreated == before.pagesCreated);
        // Same initial glyph used in a new string without re-rasterizing it.
        record("ABCD"); Check(c.GetTextCacheStats().glyphHits > after.glyphHits);
        Check(texture->GetDesc().pixels == pixels);
        c.SetGlyphAtlasBudget(256 * 256 * 4);
        for (int i = 0; i < 8; ++i) { c.Begin(commands, w, {}); c.Text({10, 10}, "Atlas eviction", 40.f + i * 6, {1, 1, 1, 1}); c.End();
            Check(c.GetTextCacheStats().pageBytes <= 256 * 256 * 4 && c.GetTextCacheStats().pages <= 1); }
        Check(c.GetTextCacheStats().evictions > 0 && texture->GetDesc().pixels == pixels);
        c.SetGlyphAtlasBudget(0); record("Fallback"); Check(c.GetTextCacheStats().fallbackLines > 0 && c.GetTextCacheStats().pages == 0);
        c.SetGlyphAtlasBudget(16ull * 1024 * 1024);
        // HarfBuzz composes a+acute; q+acute keeps two overlapping glyphs.
        c.Begin(commands, w, {}); c.Text({10, 10}, "q\xcc\x81", 20, {1, 0, 0, .3f}); c.End(); Check(c.GetTextCacheStats().fallbackLines >= 2);
        c.Begin(commands, w, {}); c.Text({10, 10}, "Hello", 256, {1, 1, 1, 1}); c.End(); // Large native glyphs, no downscale.
        Reject([&] { c.SetGlyphAtlasBudget(65ull * 1024 * 1024); });
        // Never upload an unsealed page, including when hidden inside a layer.
        Beast::Detail::TextureBuilder staged(4, 4); auto handle = staged.Handle();
        const auto pipeline = std::get<RHIDrawTriangles>(retained.GetCommands().front()).pipeline;
        const std::array<RHIVertex, 3> triangle{{{-1,-1,0,0},{1,-1,1,0},{0,1,0,1}}};
        RHICommandList pending; pending.BeginRenderPass({0,0,0,0}); pending.DrawTriangles(pipeline, handle, triangle); pending.EndRenderPass();
        Reject([&] { Beast::Detail::CheckSubmission(w, nullptr, pending); });
        auto layer = RHI::CreateLayerTexture(4, 4, pending); RHICommandList outer;
        outer.BeginRenderPass({}); outer.DrawTriangles(pipeline, layer, triangle); outer.EndRenderPass();
        Reject([&] { Beast::Detail::CheckSubmission(w, nullptr, outer); });
        staged.WriteCoverage(0, 0, 1, 1, std::array<std::uint8_t, 1>{255}); staged.Seal();
        Beast::Detail::CheckSubmission(w, nullptr, outer);
        Reject([&] { staged.WriteCoverage(0, 0, 1, 1, std::array<std::uint8_t, 1>{0}); });
        std::puts("Atlas immutable pages, reuse, eviction, retained commands, fallback and staging submission validation passed.");
    }
    void WarmupTests() {
        for (auto axis : {Axis::Vertical, Axis::Horizontal}) {
            UI::Detail::GlyphAtlas atlas; std::vector<UI::Detail::GlyphAtlas::Quad> quads;
            const std::string text = "Warm Привет";
            atlas.BeginFrame(); Check(atlas.Resolve(text, 28, {}, FontFace::Regular, false, quads)); atlas.EndFrame();
            const auto retained = quads.front().texture; const auto original = retained->GetDesc().pixels;
            atlas.QueueWarm(text, 28, {}, FontFace::Regular, axis);
            std::vector<RHITextureHandle> uploads;
            for (int i = 0; i < 64; ++i) {
                atlas.WarmStep(uploads);
                for (const auto& page : uploads) Check(page->IsReady());
            }
            const auto warmed = atlas.GetStats();
            for (int i = 0; i < 8; ++i) {
                const Point phase = axis == Axis::Vertical ? Point{0, i / 8.f} : Point{i / 8.f, 0};
                atlas.BeginFrame(); Check(atlas.Resolve(text, 28, phase, FontFace::Regular, false, quads)); atlas.EndFrame();
            }
            Check(atlas.GetStats().glyphMisses == warmed.glyphMisses);
            Check(atlas.GetStats().pagesCreated == warmed.pagesCreated);
            Check(retained->GetDesc().pixels == original);
            Check(!atlas.WarmStep(uploads) && uploads.empty());
            atlas.QueueWarm("Cancelled", 30, {}, FontFace::Regular, axis);
            atlas.SetBudget(0); Check(!atlas.WarmStep(uploads));
        }
        std::puts("Idle warmup: both axes, eight exact coverage phases, sealed uploads, immutable replay, completion and cancellation passed.");
    }
    class Readback final : public Beast::Detail::OpenGLContext {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_; PFNGLREADPIXELSPROC read_;
        std::function<void(const std::vector<unsigned char>&)> verify_;
        inline static Readback* active_ = nullptr;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
            auto& self = *active_; std::vector<unsigned char> pixels(std::size_t(x1) * y1 * 4);
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
        RHIBackend backend_; Canvas atlas_, legacy_; std::unique_ptr<RHI> rhi_;
        RHICommandList commands_, old_, future_; Window viewport_;
        std::vector<unsigned char> reference_; int attempts_ = 0, maxError_ = 0; bool prepared_ = false;
        void CreateRenderer() {
            if (backend_ == RHIBackend::OpenGL) rhi_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<Readback>(GetWindow(),
                [this](const auto& pixels) {
                    const int phase = frames % 6;
                    if (phase == 0 || phase == 4) { reference_ = pixels; return; }
                    Check(pixels.size() == reference_.size()); int error = 0;
                    for (std::size_t i = 0; i < pixels.size(); ++i) if (i % 4 != 3) error = std::max(error, std::abs(int(pixels[i]) - reference_[i]));
                    if (error > 3) std::printf("Atlas GPU mismatch frame=%d scale=%.3f error=%d\n", frames, viewport_.scale, error);
                    Check(error <= 3); maxError_ = std::max(maxError_, error);
                }), {backend_, false, true});
            else rhi_ = RHI::Create(GetWindow(), {backend_, false, true});
        }
        void Record(Canvas& c, RHICommandList& list, bool changed) {
            Theme theme; theme.background = {.93f, .97f, .95f, 1}; c.Begin(list, viewport_, theme);
            c.Text({12.37f, 8.73f}, changed ? "Другая строка 98765" : "Чёткость AVATAR To 0123", 14, {.12f, .4f, .2f, 1});
            c.Text({10, 35}, "Beast Aa a\xcc\x81", 17, {.1f, .3f, .2f, .45f}, FontFace::Bold);
            c.Text({10, 66}, "\xee\x88\x97", 22, {0, .5f, .2f, 1}, FontFace::Icons);
            c.SaveLayer({8, 100, 150, 45}, .7f); c.ClipRRect({8, 100, 150, 45}, 8);
            c.RoundedRect({8, 100, 150, 45}, 0, {.8f, .9f, .8f, 1}); c.Text({9.3f, 103.7f}, "Layer 123 AV", 18, {.2f, .1f, .5f, 1}); c.Restore();
            c.Save(); c.Translate(.21f, .17f); c.Scale(1.25f); c.Text({10.23f, 128.67f}, "Scale текст", 15, {.2f, .3f, .4f, 1}); c.Restore();
            c.PushClip({2.3f, 180.1f, 110.2f, 22.2f}); c.Text({-2.37f, 179.73f}, "Clipped Чёткость", 14, {.2f, .3f, .1f, 1}); c.PopClip();
            c.End();
            for (const auto& command : list.GetCommands()) if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                if (draw->texture->GetContent() || draw->texture->GetDesc().width == 1) continue;
                const auto& t = draw->texture->GetDesc(); Check(t.filter == RHIFilter::Nearest && draw->texture->IsReady());
                for (std::size_t q = 0; q < draw->vertices.size(); q += 6) {
                    const auto a = draw->vertices[q], b = draw->vertices[q + 2];
                    Check(std::abs((b.x - a.x) * viewport_.width / 2 - (b.u - a.u) * t.width) < .002f);
                    Check(std::abs((b.y - a.y) * viewport_.height / 2 - (b.v - a.v) * t.height) < .002f);
                }
            }
        }
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) { legacy_.SetGlyphAtlasBudget(0); }
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 900; w.height = 850; w.title = "Beast glyph atlas tests"; w.gpuRendering = true; return w; }
        void OnEvent(const Event& e) override {
            if (e.type == EventType::SurfaceCreated) CreateRenderer();
            if (e.type == EventType::SurfaceDestroyed) rhi_.reset();
        }
        void OnUpdate(double) override {
            Check(++attempts_ < 500); if (!rhi_) return;
            const int phase = frames % 6;
            if (!prepared_) {
                viewport_ = GetWindow(); viewport_.scale = Scales[frames / 6];
                viewport_.contentX = 11; viewport_.contentY = 17; viewport_.contentWidth -= 22; viewport_.contentHeight -= 34;
                if (phase == 0) Record(legacy_, commands_, false);
                if (phase == 1) { Record(atlas_, commands_, false); old_ = commands_; }
                if (phase == 2) { Record(atlas_, future_, true); commands_ = old_; }
                if (phase == 3) { atlas_.SetGlyphAtlasBudget(0); atlas_.SetGlyphAtlasBudget(16ull * 1024 * 1024); rhi_.reset(); CreateRenderer(); commands_ = old_; }
                if (phase == 4) Record(legacy_, commands_, true);
                if (phase == 5) commands_ = future_;
                prepared_ = true;
            }
            if (!rhi_->Submit(viewport_, commands_)) return;
            prepared_ = false;
            if (++frames == 54) {
                std::printf("Atlas %s: 9 DPI, native texels, 3 faces, alpha, crop, transform, nested layers, COW/replay/eviction and RHI recreation passed; GL pixel max error=%d/255.\n",
                    backend_ == RHIBackend::OpenGL ? "OpenGL" : "Vulkan (no pixel readback)", maxError_); RequestExit();
            }
        }
    };
}
int main() {
    try { ReferenceTest(); LifetimeTests(); WarmupTests(); for (auto api : {RHIBackend::OpenGL, RHIBackend::Vulkan}) { GpuTest test(api); Check(Run(test) == 0 && test.frames == 54); } }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
