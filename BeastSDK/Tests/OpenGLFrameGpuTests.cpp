#include "Beast.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFrameResources.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <source_location>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace Beast;
using namespace Beast::Detail;
namespace
{
    void Check(bool value, std::source_location location = std::source_location::current())
    { if (!value) throw std::runtime_error("OpenGL GPU framebuffer test failed at line " + std::to_string(location.line())); }
    class FrameTest final : public Application
    {
        std::unique_ptr<OpenGLContext> context_;
        OpenGLFunctions gl_;
        OpenGLFrameResources frame_;
        PFNGLREADPIXELSPROC readPixels_ = nullptr;
        PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC attachment_ = nullptr;
        PFNGLGETRENDERBUFFERPARAMETERIVPROC renderbuffer_ = nullptr;
        int firstWidth_ = 0, firstHeight_ = 0;
        int attempts_ = 0;
        bool windowReady_ = false;
        bool windowReadback_ = true;
        void Create()
        {
            RHIOptions options; options.vsync = false;
            context_ = CreateOpenGLContext(GetWindow(), options);
            gl_.Load(*context_);
            readPixels_ = reinterpret_cast<PFNGLREADPIXELSPROC>(context_->LoadBase("glReadPixels"));
            attachment_ = reinterpret_cast<PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC>(context_->Load("glGetFramebufferAttachmentParameteriv"));
            renderbuffer_ = reinterpret_cast<PFNGLGETRENDERBUFFERPARAMETERIVPROC>(context_->Load("glGetRenderbufferParameteriv"));
            Check(readPixels_ && attachment_ && renderbuffer_);
        }
        void Destroy()
        {
            if (!context_) return;
            context_->MakeCurrent(); frame_.Release(gl_); context_.reset();
        }
        void Pixel(int x, int y, int r, int g, int b)
        {
            unsigned char value[4]{};
            readPixels_(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value);
            GLint readTarget = 0; gl_.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readTarget);
            if (std::abs(int(value[0]) - r) > 1 || std::abs(int(value[1]) - g) > 1 || std::abs(int(value[2]) - b) > 1)
                std::fprintf(stderr, "Frame %d, read FBO %d, error %u, pixel (%d,%d): got %u,%u,%u; expected %d,%d,%d\n",
                    frames, readTarget, unsigned(gl_.glGetError()), x, y, unsigned(value[0]), unsigned(value[1]), unsigned(value[2]), r, g, b);
            Check(std::abs(int(value[0]) - r) <= 1 && std::abs(int(value[1]) - g) <= 1 && std::abs(int(value[2]) - b) <= 1);
        }
    public:
        int frames = 0;
        bool resized = false;
        WindowDefinition Configure() const override
        { WindowDefinition window; window.title = "Beast FBO pixel verification"; window.width = 400; window.height = 300; window.gpuRendering = true; return window; }
        void OnEvent(const Event& event) override
        {
            if (event.type == EventType::SurfaceCreated) Create();
            if (event.type == EventType::SurfaceDestroyed) Destroy();
        }
        void OnUpdate(double) override
        {
            Check(++attempts_ < 300);
            if (!windowReady_)
            {
                Check(context_->Present()); windowReady_ = true; return;
            }
            int width = 0, height = 0; context_->GetSize(width, height);
            if (frames == 0)
            {
                gl_.glBindFramebuffer(GL_FRAMEBUFFER, 0); gl_.glDisable(GL_SCISSOR_TEST);
                gl_.glClearColor(0, 1, 0, 1); gl_.glClear(GL_COLOR_BUFFER_BIT); gl_.glReadBuffer(GL_BACK);
                // Calibrate window readback independently of our FBO/blit path.
                // Occluded/remote driver surfaces can return undefined pixels even
                // after a direct default-framebuffer clear. Do not misreport that
                // as a verified window blit, or skip offscreen resource/pixel checks.
                unsigned char pixel[4]{};
                readPixels_(15, height - 25, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                Check(gl_.glGetError() == GL_NO_ERROR);
                windowReadback_ = pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0;
                if (!windowReadback_)
                    std::puts("SKIP window pixel assertions: direct default-framebuffer clear/readback is unavailable on this host. Offscreen FBO tests still run.");
            }
            if (!frame_.BeginFrame(gl_, width, height)) return;
            if (frames == 0) { firstWidth_ = width; firstHeight_ = height; }
            if (frames > 5 && (width != firstWidth_ || height != firstHeight_)) resized = true;
            GLint bound = 0, color = 0, storageWidth = 0, storageHeight = 0, format = 0;
            gl_.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound); Check(bound != 0);
            attachment_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &color); Check(color != 0);
            gl_.glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(color));
            renderbuffer_(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &storageWidth);
            renderbuffer_(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &storageHeight);
            renderbuffer_(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
            Check(storageWidth == width && storageHeight == height && format == GL_RGBA8);
            gl_.glBindRenderbuffer(GL_RENDERBUFFER, 0);
            gl_.glViewport(0, 0, width, height); gl_.glDisable(GL_SCISSOR_TEST);
            gl_.glClearColor(0.1f, 0.2f, 0.3f, 1); gl_.glClear(GL_COLOR_BUFFER_BIT);
            gl_.glEnable(GL_SCISSOR_TEST); gl_.glScissor(10, height - 60, 30, 40);
            gl_.glClearColor(1, 0, 0, 1); gl_.glClear(GL_COLOR_BUFFER_BIT);
            Pixel(15, height - 25, 255, 0, 0); Pixel(15, 25, 26, 51, 77);
            frame_.EndFrame(gl_); // Must disable scissor before the full-frame blit.
            gl_.glReadBuffer(GL_BACK);
            if (windowReadback_)
            {
                Pixel(15, height - 25, 255, 0, 0); Pixel(15, 25, 26, 51, 77);
                Pixel(width - 3, height - 3, 26, 51, 77);
            }
            Check(gl_.glGetError() == GL_NO_ERROR);
            Check(context_->Present());
            ++frames;
#if defined(_WIN32)
            if (frames == 5)
                SetWindowPos(static_cast<HWND>(GetWindow().nativeHandle), nullptr, 0, 0, 720, 520, SWP_NOMOVE | SWP_NOZORDER);
#endif
            if (frames == 10) { Destroy(); Create(); }
            if (frames == 15) RequestExit();
        }
    };
}
int main()
{
    FrameTest test;
    if (Run(test) != 0 || test.frames != 15 || !test.resized) return 1;
    std::puts("OpenGL GPU FBO format/size, offscreen pixels, blit calls, resize and recreation tests passed."); return 0;
}
