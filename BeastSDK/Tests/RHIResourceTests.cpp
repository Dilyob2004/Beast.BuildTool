#include "Launch/ResourceScene.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/Private/RHIInternal.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <source_location>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>

using namespace Beast;
using namespace Beast::Detail;
namespace
{
    void Check(bool okay, std::source_location line = std::source_location::current())
    { if (!okay) throw std::runtime_error("Resource assertion failed at line " + std::to_string(line.line())); }
    template<class F> void Reject(F&& action, std::source_location line = std::source_location::current())
    {
        bool rejected = false;
        try { action(); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected, line);
    }
    void CpuTests()
    {
        auto vs = BeastShaders::UIVertex(), fs = BeastShaders::UIFragment();
        auto pipeline = RHI::CreatePipeline({vs, fs});
        auto texture = RHI::CreateTexture({1, 1, {255, 255, 255, 255}});
        Reject([] { RHI::CreateTexture({0, 1, {}}); });
        Reject([] { RHI::CreateTexture({4097, 1, {}}); });
        Reject([] { RHI::CreateTexture({1, 1, {0, 0, 0}}); });
        Reject([] { RHI::CreateTexture({1, 1, {0, 0, 0, 0}, static_cast<RHIFilter>(99)}); });
        Reject([&] { RHI::CreatePipeline({fs, vs}); });
        Reject([&] { RHI::CreatePipeline({vs, nullptr}); });
        Reject([&] { RHI::CreatePipeline({vs, fs, static_cast<RHIBlend>(99)}); });
        auto shader = vs->GetDesc(); shader.spirv[0] = 0;
        Reject([&] { RHI::CreateShader(shader); });
        shader = vs->GetDesc(); shader.spirv.pop_back();
        Reject([&] { RHI::CreateShader(shader); });
        shader = vs->GetDesc(); shader.entryPoint = "Missing";
        Reject([&] { RHI::CreateShader(shader); });
        shader = fs->GetDesc(); shader.stage = RHIShaderStage::Vertex;
        Reject([&] { RHI::CreateShader(shader); });
        shader = vs->GetDesc(); shader.glslES300.clear();
        Reject([&] { RHI::CreateShader(shader); });
        auto vertices = ResourceScene::Quad(-1, -1, 1, 1);
        RHICommandList commands;
        Reject([&] { commands.DrawTriangles(pipeline, texture, vertices); });
        commands.BeginRenderPass({});
        Reject([&] { commands.DrawTriangles(nullptr, texture, vertices); });
        Reject([&] { commands.DrawTriangles(pipeline, nullptr, vertices); });
        Reject([&] { commands.DrawTriangles(pipeline, texture, {}); });
        Reject([&] { commands.DrawTriangles(pipeline, texture, std::span(vertices).first(4)); });
        Reject([&] { commands.DrawTriangles(pipeline, texture, vertices, RHIRect{0, 0, -1, 2}); });
        vertices[0].x = std::numeric_limits<float>::quiet_NaN();
        Reject([&] { commands.DrawTriangles(pipeline, texture, vertices); });
        vertices[0].x = -1;
        RHIDrawConstants constants{}; constants[31] = std::numeric_limits<float>::infinity();
        Reject([&] { commands.DrawTriangles(pipeline, texture, vertices, {}, constants); });
        constants[31] = -3.25f;
        commands.DrawTriangles(pipeline, texture, vertices, {}, constants);
        constants[31] = 99; // Commands own the data, not this caller's array.
        commands.ClearRect({0, 0, 3, 3}, {});
        commands.DrawTriangles(pipeline, texture, vertices, RHIRect{-100, -100, 1, 1});
        commands.EndRenderPass();
        vertices[0].x = 42;
        Check(std::get<RHIDrawTriangles>(commands.GetCommands()[0]).vertices[0].x == -1);
        Check(std::get<RHIDrawTriangles>(commands.GetCommands()[0]).constants[31] == -3.25f);
        Check(std::holds_alternative<RHIClearRect>(commands.GetCommands()[1]));
        std::weak_ptr<const RHIPipeline> lifetime = pipeline; pipeline.reset();
        Check(!lifetime.expired()); commands.Reset(); Check(lifetime.expired());
        Check(commands.GetCommands().empty());
        std::puts("CPU resource validation, ordered commands, vertex copies and handle lifetimes passed.");
    }

    // Read the real offscreen color attachment immediately before the backend's
    // presentation blit. No production readback/test hook is needed.
    class PixelContext final : public OpenGLContext
    {
        std::unique_ptr<OpenGLContext> real_;
        Window window_;
        std::uint64_t generation_ = 1;
        PFNGLBLITFRAMEBUFFERPROC blit_ = nullptr;
        PFNGLREADPIXELSPROC read_ = nullptr;
        PFNGLGETERRORPROC error_ = nullptr;
        inline static PixelContext* active_ = nullptr;
        void Pixel(float x, float y, int width, int height, int r, int g, int b)
        {
            unsigned char value[4]{};
            read_(static_cast<int>(x * width), height - 1 - static_cast<int>(y * height), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value);
            if (std::abs(int(value[0]) - r) > 1 || std::abs(int(value[1]) - g) > 1 || std::abs(int(value[2]) - b) > 1 || value[3] != 255)
            {
                std::fprintf(stderr, "GPU pixel %.2f,%.2f got %u,%u,%u,%u expected %d,%d,%d,255\n", x, y,
                    unsigned(value[0]), unsigned(value[1]), unsigned(value[2]), unsigned(value[3]), r, g, b);
                Check(false);
            }
        }
        static void APIENTRY Blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            auto& self = *active_;
            self.Pixel(.05f, .05f, sx1, sy1, 26, 26, 26);
            self.Pixel(.15f, .15f, sx1, sy1, 255, 0, 0);
            self.Pixel(.85f, .15f, sx1, sy1, 0, 255, 0);
            self.Pixel(.15f, .85f, sx1, sy1, 0, 0, 255);
            self.Pixel(.85f, .85f, sx1, sy1, 255, 255, 0);
            self.Pixel(.25f, .25f, sx1, sy1, 255, 0, 255);
            self.Pixel(.35f, .46f, sx1, sy1, 255, 128, 128);
            self.Pixel(.65f, .46f, sx1, sy1, 128, 255, 128);
            self.Pixel(.35f, .54f, sx1, sy1, 128, 128, 255);
            self.Pixel(.35f, .68f, sx1, sy1, 64, 64, 255);
            self.Pixel(.75f, .76f, sx1, sy1, 0, 255, 255);
            Check(self.error_() == GL_NO_ERROR);
            ++self.frames;
            self.blit_(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
        }
    public:
        int frames = 0;
        explicit PixelContext(const Window& window) : window_(window)
        {
            real_ = CreateOpenGLContext(window, {});
            blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(real_->Load("glBlitFramebuffer"));
            read_ = reinterpret_cast<PFNGLREADPIXELSPROC>(real_->LoadBase("glReadPixels"));
            error_ = reinterpret_cast<PFNGLGETERRORPROC>(real_->LoadBase("glGetError"));
            Check(blit_ && read_ && error_); active_ = this;
        }
        void* Load(const char* name) override
        { return std::strcmp(name, "glBlitFramebuffer") == 0 ? reinterpret_cast<void*>(Blit) : real_->Load(name); }
        void* LoadBase(const char* name) override { return real_->LoadBase(name); }
        void MakeCurrent() override { real_->MakeCurrent(); }
        bool Present() override
        {
            const bool result = real_->Present();
            if (frames == 15)
            {
                // Real context replacement, same RHI and CPU handles. Emulate
                // EGL_CONTEXT_LOST generation changes while resources exist.
                real_.reset(); ++generation_;
                real_ = CreateOpenGLContext(window_, {});
                blit_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(real_->Load("glBlitFramebuffer"));
                read_ = reinterpret_cast<PFNGLREADPIXELSPROC>(real_->LoadBase("glReadPixels"));
                error_ = reinterpret_cast<PFNGLGETERRORPROC>(real_->LoadBase("glGetError"));
                Check(blit_ && read_ && error_);
            }
            return result;
        }
        std::uint64_t GetGeneration() const noexcept override { return generation_; }
        void GetSize(int& width, int& height) override { real_->GetSize(width, height); }
    };
    // Cold immutable texture bursts exercise batch boundaries, a texture larger
    // than the staging budget, nested layers, replay and device recreation.
    class UploadBatchTest final : public Application
    {
        std::unique_ptr<RHI> renderer_;
        RHICommandList commands_;
        int attempts_ = 0;
        void Record()
        {
            auto pipeline = RHI::CreatePipeline({BeastShaders::UIVertex(), BeastShaders::UIFragment()});
            RHICommandList child;
            child.BeginRenderPass({0, 0, 0, 1});
            const auto quad = ResourceScene::Quad(-1, -1, 1, 1);
            for (unsigned i = 0; i < 130; ++i) // Cross the tiny-image job limit too.
                child.DrawTriangles(pipeline, RHI::CreateTexture({1, 1, {255, 255, 255, 255}}), quad);
            // 4 + 4 MiB reaches the boundary; 16 MiB is a standalone upload.
            // The remaining pages must flush at the end of Prepare.
            for (const unsigned size : {1024u, 1024u, 2048u, 256u, 256u, 256u})
            {
                RHITextureDesc desc; desc.width = desc.height = size;
                desc.pixels.resize(std::size_t(size) * size * 4, 255);
                child.DrawTriangles(pipeline, RHI::CreateTexture(std::move(desc)), quad);
            }
            child.EndRenderPass();
            commands_.Reset(); commands_.BeginRenderPass({0, 0, 0, 1});
            commands_.DrawTriangles(pipeline, RHI::CreateLayerTexture(64, 64, std::move(child)), quad);
            commands_.EndRenderPass();
        }
    public:
        int frames = 0;
        WindowDefinition Configure() const override
        { WindowDefinition w; w.title = "Vulkan upload batch regression"; w.width = w.height = 128; w.gpuRendering = true; return w; }
        void OnStart() override { Record(); }
        void OnEvent(const Event& event) override
        {
            if (event.type == EventType::SurfaceCreated) renderer_ = RHI::Create(GetWindow(), {RHIBackend::Vulkan, true, true});
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        }
        void OnUpdate(double) override
        {
            Check(++attempts_ < 100);
            if (!renderer_->Submit(GetWindow(), commands_)) return;
            ++frames;
            if (frames == 2) Record(); // Retire the old textures after their submission fence.
            if (frames == 4) { renderer_.reset(); renderer_ = RHI::Create(GetWindow(), {RHIBackend::Vulkan, true, true}); }
            if (frames == 6) RequestExit();
        }
    };
    class GpuTest final : public Application
    {
        RHIBackend backend_;
        std::unique_ptr<RHI> renderer_;
        std::unique_ptr<ResourceScene> scene_;
        RHICommandList commands_;
        int attempts_ = 0, originalWidth_ = 0;
        void Create()
        {
            if (backend_ == RHIBackend::OpenGL) renderer_ = CreateOpenGLRHI(GetWindow(), std::make_unique<PixelContext>(GetWindow()));
            else renderer_ = RHI::Create(GetWindow(), {backend_, true, true});
        }
    public:
        int frames = 0;
        bool resized = false;
        explicit GpuTest(RHIBackend backend) : backend_(backend) {}
        WindowDefinition Configure() const override
        { WindowDefinition window; window.title = "Beast resource GPU tests"; window.width = 500; window.height = 400; window.gpuRendering = true; return window; }
        void OnStart() override { scene_ = std::make_unique<ResourceScene>(); }
        void OnEvent(const Event& event) override
        {
            if (event.type == EventType::SurfaceCreated) Create();
            if (event.type == EventType::SurfaceDestroyed) renderer_.reset();
        }
        void OnUpdate(double) override
        {
            Check(++attempts_ < 300);
            const auto& window = GetWindow();
            if (!frames) originalWidth_ = window.width;
            if (window.width != originalWidth_) resized = true;
            scene_->Record(commands_, window.width, window.height);
            if (!renderer_->Submit(window, commands_)) return;
            ++frames;
            if (frames == 10) Check(SetWindowPos(static_cast<HWND>(window.nativeHandle), nullptr, 0, 0, 700, 500, SWP_NOMOVE | SWP_NOZORDER));
            if (frames == 20) scene_ = std::make_unique<ResourceScene>(); // Retire GPU resources after their fence.
            if (frames == 30) { renderer_.reset(); Create(); } // Same CPU resources restored on a new device/context.
            if (frames == 40) RequestExit();
        }
    };
}
int main()
{
    try
    {
        CpuTests();
        GpuTest gl(RHIBackend::OpenGL); Check(Run(gl) == 0 && gl.frames == 40 && gl.resized);
        std::puts("OpenGL: 440 GPU pixel assertions, texture orientation, blending, scissor, resize, retirement, context loss and recreation passed.");
        GpuTest vk(RHIBackend::Vulkan); Check(Run(vk) == 0 && vk.frames == 40 && vk.resized);
        std::puts("Vulkan: 40 textured frames, resize, retirement and device recreation passed (inspect validation log).");
        UploadBatchTest uploads; Check(Run(uploads) == 0 && uploads.frames == 6);
        std::puts("Vulkan: bounded upload bursts, oversized texture, nested layer, replay, retirement and recreation passed.");
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
