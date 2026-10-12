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
namespace
{
    constexpr std::array Scales{.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.625f, 3.0f, 4.0f};
    constexpr Point Origin{13.37f, 7.73f};
    const std::string TextLine = "Чёткость Aa 0123";
    void Check(bool value, std::source_location line = std::source_location::current())
    { if (!value) throw std::runtime_error("UI raster assertion at line " + std::to_string(line.line())); }
    Point Physical(RHIVertex v, const Window& window)
    { return {(v.x + 1) * .5f * window.width, (v.y + 1) * .5f * window.height}; }
    RHIDrawTriangles Record(Canvas& canvas, RHICommandList& commands, const Window& window, float fontSize = 14, Point origin = Origin)
    {
        // Keep the legacy rasterizer's exact-pixel regression as an independent
        // baseline. UIGlyphAtlasTests covers the default atlas rendering path.
        canvas.SetGlyphAtlasBudget(0);
        Theme theme; theme.background = {0, 0, 0, 1};
        canvas.Begin(commands, window, theme); canvas.Text(origin, TextLine, fontSize, {1, 1, 1, 1}); canvas.End();
        Check(commands.GetCommands().size() == 1);
        return std::get<RHIDrawTriangles>(commands.GetCommands().front());
    }
    void CheckMapping(const RHIDrawTriangles& draw, const Window& window, float fontSize = 14, Point origin = Origin)
    {
        const auto tl = Physical(draw.vertices[0], window), br = Physical(draw.vertices[2], window);
        const auto& texture = draw.texture->GetDesc();
        // Proves native sampling, including fractional em sizes at 125/150%.
        Check(std::abs(br.x - tl.x - texture.width) < .001f);
        Check(std::abs(br.y - tl.y - texture.height) < .001f);
        Check(std::abs(tl.x - std::round(tl.x)) < .001f && std::abs(tl.y - std::round(tl.y)) < .001f);
        Check(texture.filter == RHIFilter::Nearest);
        const double px = std::round((window.contentX + double(origin.x) * window.scale) * 64) / 64;
        const double py = std::round((window.contentY + double(origin.y) * window.scale) * 64) / 64;
        Check(std::abs(tl.x - std::floor(px)) < .001 && std::abs(tl.y - std::floor(py)) < .001);
        const Point phase{static_cast<float>(px - std::floor(px)), static_cast<float>(py - std::floor(py))};
        // Compare with an independently requested *fractional* raster size.
        const auto reference = UI::Detail::RasterizeText(TextLine, fontSize * window.scale,
            static_cast<int>(texture.width), phase, static_cast<int>(texture.height));
        Check(reference.texture->GetDesc().pixels == texture.pixels);
        Check(std::any_of(texture.pixels.begin(), texture.pixels.end(), [](auto p) { return p > 0 && p < 255; }));
    }
    void CpuTests()
    {
        Canvas canvas; RHICommandList commands;
        for (auto size : {Size{800, 600}, Size{1920, 1080}, Size{2560, 1440}, Size{3840, 2160}})
            for (float scale : Scales)
            {
                Window window; window.width = static_cast<int>(size.width); window.height = static_cast<int>(size.height);
                window.contentX = 17; window.contentY = 23;
                window.contentWidth = window.width - 34; window.contentHeight = window.height - 46; window.scale = scale;
                auto first = Record(canvas, commands, window); CheckMapping(first, window);
                Check(Record(canvas, commands, window).texture == first.texture); // Cache hit at identical DPI/phase.
                CheckMapping(Record(canvas, commands, window, 14, {-2.37f, -1.73f}), window, 14, {-2.37f, -1.73f});
            }
        Window window; window.width = window.contentWidth = 1600; window.height = window.contentHeight = 1200;
        window.scale = 1.25f;
        auto first = Record(canvas, commands, window);
        window.scale = 1.2501f; auto fractional = Record(canvas, commands, window);
        Check(first.texture != fractional.texture); CheckMapping(fractional, window);
        auto moved = Record(canvas, commands, window, 14, {Origin.x + .2f, Origin.y});
        Check(moved.texture != fractional.texture);
        CheckMapping(moved, window, 14, {Origin.x + .2f, Origin.y});
        window.scale = 4;
        const auto large = Record(canvas, commands, window, 128); CheckMapping(large, window, 128);
        Check(large.texture->GetDesc().height > 256); // No old 256px bitmap upscaling.
        window.scale = .5f; // Sub-pixel em sizes retain their true coverage, no ceil to 1px.
        CheckMapping(Record(canvas, commands, window, 1), window, 1);
        std::puts("UI raster CPU tests passed: 800x600 through 4K, 75-400% DPI, negative/fractional origins, phase/size cache keys, >256px text.");
    }
    class ReadbackContext final : public Beast::Detail::OpenGLContext
    {
        std::unique_ptr<Beast::Detail::OpenGLContext> actual_;
        PFNGLBLITFRAMEBUFFERPROC blit_;
        PFNGLREADPIXELSPROC read_;
        std::function<void(int, int, const std::vector<unsigned char>&)> verify_;
        inline static ReadbackContext* active_ = nullptr;
        static void APIENTRY Blit(GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            auto& self = *active_;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(x1) * y1 * 4);
            self.read_(0, 0, x1, y1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            self.verify_(x1, y1, pixels);
            self.blit_(x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        ReadbackContext(const Window& window, std::function<void(int, int, const std::vector<unsigned char>&)> verify) : verify_(std::move(verify))
        {
            actual_ = Beast::Detail::CreateOpenGLContext(window, {});
            blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(actual_->Load("glBlitFramebuffer"));
            read_ = reinterpret_cast<PFNGLREADPIXELSPROC>(actual_->LoadBase("glReadPixels")); Check(blit_ && read_); active_ = this;
        }
        void* Load(const char* name) override { return std::strcmp(name, "glBlitFramebuffer") == 0 ? reinterpret_cast<void*>(Blit) : actual_->Load(name); }
        void* LoadBase(const char* name) override { return actual_->LoadBase(name); }
        void MakeCurrent() override { actual_->MakeCurrent(); }
        bool Present() override { return actual_->Present(); }
        void GetSize(int& w, int& h) override { actual_->GetSize(w, h); }
    };
    class GpuTest final : public Beast::Application
    {
        RHIBackend backend_;
        Canvas canvas_;
        RHICommandList commands_;
        RHIDrawTriangles draw_;
        Window viewport_;
        std::unique_ptr<RHI> renderer_;
        int attempts_ = 0;
        void Verify(int width, int height, const std::vector<unsigned char>& pixels)
        {
            const auto tl = Physical(draw_.vertices[0], viewport_);
            const int left = static_cast<int>(std::round(tl.x)), top = static_cast<int>(std::round(tl.y));
            const auto& texture = draw_.texture->GetDesc(); const auto clip = *draw_.scissor;
            std::size_t compared = 0;
            for (int row = 0; row < static_cast<int>(texture.height); ++row)
                for (int col = 0; col < static_cast<int>(texture.width); ++col)
                {
                    const int x = left + col, y = top + row;
                    if (x < 0 || y < 0 || x >= width || y >= height || x < clip.x || y < clip.y || x >= clip.x + clip.width || y >= clip.y + clip.height) continue;
                    const auto source = texture.pixels[(static_cast<std::size_t>(row) * texture.width + col) * 4];
                    const auto target = (static_cast<std::size_t>(height - 1 - y) * width + x) * 4;
                    for (int channel = 0; channel < 3; ++channel) Check(std::abs(int(pixels[target + channel]) - source) <= 1);
                    ++compared;
                }
            Check(compared > 100);
        }
    public:
        int frames = 0;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {}
        WindowDefinition Configure() const override { WindowDefinition w; w.width = 800; w.height = 600; w.gpuRendering = true; w.title = "Beast DPI raster tests"; return w; }
        void OnEvent(const Event& event) override
        {
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
            if (event.type == EventType::SurfaceCreated)
            {
                if (backend_ == RHIBackend::OpenGL) renderer_ = Beast::Detail::CreateOpenGLRHI(GetWindow(), std::make_unique<ReadbackContext>(GetWindow(),
                    [this](int w, int h, const auto& pixels) { Verify(w, h, pixels); }));
                else renderer_ = RHI::Create(GetWindow(), {backend_, true, true});
            }
        }
        void OnUpdate(double) override
        {
            Check(++attempts_ < 300); if (!renderer_) return;
            viewport_ = GetWindow(); viewport_.scale = Scales[frames];
            viewport_.contentX = 17; viewport_.contentY = 23;
            viewport_.contentWidth = viewport_.width - 34; viewport_.contentHeight = viewport_.height - 46;
            draw_ = Record(canvas_, commands_, viewport_); CheckMapping(draw_, viewport_);
            if (renderer_->Submit(viewport_, commands_) && ++frames == static_cast<int>(Scales.size())) RequestExit();
        }
    };
}
int main()
{
    try {
        CpuTests();
        for (auto backend : {RHIBackend::OpenGL, RHIBackend::Vulkan})
        { GpuTest test(backend); Check(Run(test) == 0 && test.frames == static_cast<int>(Scales.size())); }
        std::puts("UI DPI GPU tests passed: 9 scales per backend; OpenGL framebuffer equals original AA texels within 1/255 (no resampling); Vulkan runtime/validation passed.");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
