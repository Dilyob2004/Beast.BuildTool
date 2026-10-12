#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/OpenGL/OpenGLFunctions.h"
#include "RHI/Private/OpenGL/OpenGLFrameResources.h"
#include "RHI/Private/OpenGL/OpenGLDrawResources.h"
#include "RHI/Private/OpenGL/OpenGLTimer.h"
#include "Core/Application.h"
#include <string>

namespace Beast::Detail
{
    class OpenGLRHI final : public RHI
    {
        std::unique_ptr<OpenGLContext> context_;
        void* window_;
        OpenGLFunctions gl_;
        OpenGLFrameResources frame_;
        OpenGLDrawResources drawing_;
        OpenGLTimer timer_;
        RHIOptions options_;
        RHIFrameStats stats_;
        std::uint64_t generation_ = 0;
        void Color(RHIColor color) { gl_.glClearColor(color.r, color.g, color.b, color.a); }
        void RefreshContext()
        {
            if (generation_ == context_->GetGeneration()) return;
            frame_.Abandon();
            drawing_.Abandon();
            timer_.Abandon(); stats_ = {};
            gl_.Load(*context_);
            generation_ = context_->GetGeneration();
        }
    public:
        OpenGLRHI(const Window& window, std::unique_ptr<OpenGLContext> context, RHIOptions options)
            : context_(std::move(context)), window_(window.nativeHandle), options_(options)
        {
            if (!context_) throw std::invalid_argument("An OpenGL context is required.");
            RefreshContext();
            drawing_.SetPoolBudget(options.layerPoolBytes);
#if defined(_WIN32)
            // Verify the driver's actual context, not just the requested WGL attributes.
            GLint major = 0, minor = 0, profile = 0;
            gl_.glGetIntegerv(GL_MAJOR_VERSION, &major);
            gl_.glGetIntegerv(GL_MINOR_VERSION, &minor);
            gl_.glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
            if (major < 4 || (major == 4 && minor < 6) || !(profile & GL_CONTEXT_CORE_PROFILE_BIT))
                throw std::runtime_error("Beast requires OpenGL 4.6 core profile; driver returned " +
                    std::to_string(major) + "." + std::to_string(minor) + " (profile mask " +
                    std::to_string(profile) + ").");
#endif
            const auto* version = gl_.glGetString(GL_VERSION);
            Log(version ? reinterpret_cast<const char*>(version) : "OpenGL context ready");
        }
        ~OpenGLRHI() override
        {
            if (generation_ != context_->GetGeneration()) { frame_.Abandon(); drawing_.Abandon(); timer_.Abandon(); return; }
            try { context_->MakeCurrent(); timer_.Release(); drawing_.Release(); frame_.Release(gl_); }
            catch (...) { frame_.Abandon(); drawing_.Abandon(); timer_.Abandon(); } // Context destruction releases remaining objects.
        }
        RHIBackend GetBackend() const noexcept override { return RHIBackend::OpenGL; }
        RHIFrameStats GetFrameStats() const noexcept override { auto result = stats_; result.layers = drawing_.GetStats(); return result; }
        bool Submit(const Window& window, const RHICommandList& commands) override
        {
            SubmitCpuTimer cpuTimer(stats_, options_.profiling);
            CheckSubmission(window, window_, commands);
            if (window.width <= 0 || window.height <= 0) return false;
            context_->MakeCurrent();
            RefreshContext();
            int width = 0, height = 0; context_->GetSize(width, height);
            if (width <= 0 || height <= 0) return false;
            if (!frame_.BeginFrame(gl_, width, height)) return false;
            drawing_.Collect();
            if (options_.profiling) { timer_.Initialize(*context_, gl_); timer_.Resolve(gl_, stats_); timer_.Begin(stats_.submittedFrames + 1); }
            gl_.glViewport(0, 0, width, height);
            gl_.glDisable(GL_SCISSOR_TEST);
            Color(commands.GetClearColor()); gl_.glClear(GL_COLOR_BUFFER_BIT);
            gl_.glEnable(GL_SCISSOR_TEST);
            for (const auto& command : commands.GetCommands())
            {
                if (const auto* clear = std::get_if<RHIClearRect>(&command))
                {
                    const auto rect = Clip(clear->rect, width, height);
                    if (!rect.width || !rect.height) continue;
                    gl_.glScissor(rect.x, height - rect.y - rect.height, rect.width, rect.height);
                    Color(clear->color); gl_.glClear(GL_COLOR_BUFFER_BIT);
                }
                else
                {
                    const auto& draw = std::get<RHIDrawTriangles>(command);
                    const auto rect = Clip(draw.scissor.value_or(RHIRect{0, 0, width, height}), width, height);
                    if (!rect.width || !rect.height) continue;
                    drawing_.Initialize(*context_);
                    gl_.glScissor(rect.x, height - rect.y - rect.height, rect.width, rect.height);
                    drawing_.Draw(draw, gl_);
                }
            }
            gl_.glDisable(GL_SCISSOR_TEST);
            if (options_.profiling) timer_.End();
            frame_.EndFrame(gl_);
            ++stats_.submittedFrames;
            const bool presented = context_->Present();
            RefreshContext(); // EGL may have recreated its context; never reuse old GL names.
            return presented;
        }
    };
    std::unique_ptr<RHI> CreateOpenGLRHI(const Window& window, RHIOptions options)
    { return CreateOpenGLRHI(window, CreateOpenGLContext(window, options), options); }
    std::unique_ptr<RHI> CreateOpenGLRHI(const Window& window, std::unique_ptr<OpenGLContext> context, RHIOptions options)
    { return std::make_unique<OpenGLRHI>(window, std::move(context), options); }
}
