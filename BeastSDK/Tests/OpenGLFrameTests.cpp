#include "RHI/Private/OpenGL/OpenGLFrameResources.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include "RHI/Private/RHIInternal.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace Beast;
using namespace Beast::Detail;
namespace
{
    void Check(bool value) { if (!value) throw std::runtime_error("OpenGL frame test failed"); }
    template<class F> void Reject(F&& operation)
    {
        bool rejected = false;
        try { operation(); } catch (const std::exception&) { rejected = true; }
        Check(rejected);
    }
    struct Driver
    {
        std::set<GLuint> framebuffers, colors;
        std::set<GLsync> fences;
        GLuint next = 1, read = 0, draw = 0, color = 0;
        int width = 0, height = 0, generated = 0, deleted = 0, waits = 0, loads = 0;
        int invalidDeletes = 0, blits = 0, flushes = 0;
        bool scissor = false, failAllocation = false, failStorage = false, failFence = false;
        GLenum status = GL_FRAMEBUFFER_COMPLETE, waitResult = GL_ALREADY_SIGNALED, error = GL_NO_ERROR;
        std::vector<std::string> events;
        void LoseContext()
        { framebuffers.clear(); colors.clear(); fences.clear(); next = 1; read = draw = color = 0; }
    };
    Driver* driver;
    namespace FakeGL
    {
        void APIENTRY glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
        void APIENTRY glClear(GLbitfield) { Check(driver->draw != 0); }
        void APIENTRY glEnable(GLenum value) { if (value == GL_SCISSOR_TEST) driver->scissor = true; }
        void APIENTRY glDisable(GLenum value) { if (value == GL_SCISSOR_TEST) driver->scissor = false; }
        void APIENTRY glScissor(GLint, GLint, GLsizei, GLsizei) {}
        void APIENTRY glViewport(GLint, GLint, GLsizei, GLsizei) {}
        GLenum APIENTRY glGetError() { const auto result = driver->error; driver->error = GL_NO_ERROR; return result; }
        const GLubyte* APIENTRY glGetString(GLenum) { return reinterpret_cast<const GLubyte*>("Fake GL for frame tests"); }
        const GLubyte* APIENTRY glGetStringi(GLenum, GLuint) { return nullptr; }
        void APIENTRY glGetIntegerv(GLenum name, GLint* value)
        {
            if (name == GL_MAX_RENDERBUFFER_SIZE) *value = 4096;
            else if (name == GL_MAJOR_VERSION) *value = 4;
            else if (name == GL_MINOR_VERSION) *value = 6;
#if defined(_WIN32)
            else if (name == GL_CONTEXT_PROFILE_MASK) *value = GL_CONTEXT_CORE_PROFILE_BIT;
#endif
            else Check(false);
        }
        void APIENTRY glReadBuffer(GLenum value) { Check(value == GL_COLOR_ATTACHMENT0 && driver->read != 0); }
        void APIENTRY glFlush() { ++driver->flushes; driver->events.push_back("flush"); }
        void APIENTRY glGenFramebuffers(GLsizei count, GLuint* names)
        { Check(count == 1); *names = driver->next++; driver->framebuffers.insert(*names); ++driver->generated; }
        void APIENTRY glDeleteFramebuffers(GLsizei count, const GLuint* names)
        {
            Check(count == 1);
            if (driver->framebuffers.erase(*names) != 1) ++driver->invalidDeletes;
            ++driver->deleted;
            if (driver->read == *names) driver->read = 0;
            if (driver->draw == *names) driver->draw = 0;
        }
        void APIENTRY glBindFramebuffer(GLenum target, GLuint name)
        {
            Check(name == 0 || driver->framebuffers.count(name) == 1);
            if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) driver->read = name;
            if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) driver->draw = name;
        }
        GLenum APIENTRY glCheckFramebufferStatus(GLenum) { return driver->status; }
        void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum type, GLuint name)
        {
            Check(target == GL_FRAMEBUFFER && attachment == GL_COLOR_ATTACHMENT0 && type == GL_RENDERBUFFER);
            Check(driver->draw != 0 && driver->colors.count(name) == 1);
        }
        void APIENTRY glBlitFramebuffer(GLint x0, GLint y0, GLint x1, GLint y1,
            GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
        {
            Check(driver->read != 0 && driver->draw == 0 && !driver->scissor);
            Check(x0 == 0 && y0 == 0 && dx0 == 0 && dy0 == 0);
            Check(x1 == driver->width && y1 == driver->height && dx1 == x1 && dy1 == y1);
            Check(mask == GL_COLOR_BUFFER_BIT && filter == GL_NEAREST);
            ++driver->blits; driver->events.push_back("blit");
        }
        void APIENTRY glDrawBuffers(GLsizei count, const GLenum* values)
        { Check(count == 1 && *values == static_cast<GLenum>(driver->draw ? GL_COLOR_ATTACHMENT0 : GL_BACK)); }
        void APIENTRY glGenRenderbuffers(GLsizei count, GLuint* names)
        {
            Check(count == 1);
            if (driver->failAllocation) { *names = 0; return; }
            *names = driver->next++; driver->colors.insert(*names);
        }
        void APIENTRY glDeleteRenderbuffers(GLsizei count, const GLuint* names)
        { Check(count == 1); if (driver->colors.erase(*names) != 1) ++driver->invalidDeletes; }
        void APIENTRY glBindRenderbuffer(GLenum target, GLuint name)
        { Check(target == GL_RENDERBUFFER); driver->color = name; }
        void APIENTRY glRenderbufferStorage(GLenum target, GLenum format, GLsizei width, GLsizei height)
        {
            Check(target == GL_RENDERBUFFER && format == GL_RGBA8 && driver->color != 0);
            if (driver->failStorage) { driver->error = GL_OUT_OF_MEMORY; return; }
            driver->width = width; driver->height = height;
        }
        GLsync APIENTRY glFenceSync(GLenum condition, GLbitfield flags)
        {
            Check(condition == GL_SYNC_GPU_COMMANDS_COMPLETE && flags == 0 && driver->fences.empty());
            driver->events.push_back("fence");
            if (driver->failFence) return nullptr;
            const auto sync = reinterpret_cast<GLsync>(static_cast<std::uintptr_t>(driver->next++));
            driver->fences.insert(sync); return sync;
        }
        GLenum APIENTRY glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
        {
            Check(driver->fences.count(sync) == 1 && flags == 0 && timeout == 1000000);
            ++driver->waits; return driver->waitResult;
        }
        void APIENTRY glDeleteSync(GLsync sync)
        { if (driver->fences.erase(sync) != 1) ++driver->invalidDeletes; }
    }
    OpenGLFunctions Functions()
    {
        OpenGLFunctions result;
#define BEAST_FAKE_ENTRY(Type, Name) result.Name = &FakeGL::Name;
        BEAST_GL_ENTRYPOINTS_ALL(BEAST_FAKE_ENTRY)
#undef BEAST_FAKE_ENTRY
        return result;
    }
    void CheckEmpty(const Driver& state)
    { Check(state.framebuffers.empty() && state.colors.empty() && state.fences.empty() && state.invalidDeletes == 0); }
    void TestFrames()
    {
        Driver state; driver = &state; const auto gl = Functions(); OpenGLFrameResources frame;
        Check(!frame.BeginFrame(gl, 0, 100) && state.generated == 0);
        Reject([&] { frame.EndFrame(gl); });
        Check(frame.BeginFrame(gl, 640, 480)); const auto first = state.draw;
        Check(first != 0 && state.framebuffers.size() == 1 && state.colors.size() == 1);
        Reject([&] { frame.BeginFrame(gl, 640, 480); });
        state.scissor = true; frame.EndFrame(gl);
        Check(state.read == 0 && state.draw == 0 && state.fences.size() == 1);
        Check(state.events == std::vector<std::string>({"blit", "fence", "flush"}));
        state.waitResult = GL_TIMEOUT_EXPIRED;
        Check(!frame.BeginFrame(gl, 800, 600));
        Check(state.generated == 1 && state.deleted == 0 && state.fences.size() == 1);
        Check(!frame.BeginFrame(gl, 0, 600) && state.waits == 1);
        state.waitResult = GL_CONDITION_SATISFIED;
        Check(frame.BeginFrame(gl, 800, 600)); Check(state.draw != first && state.deleted == 1 && state.fences.empty());
        const auto resized = state.draw; frame.EndFrame(gl);
        state.waitResult = GL_ALREADY_SIGNALED;
        Check(frame.BeginFrame(gl, 800, 600) && state.draw == resized && state.generated == 2);
        frame.EndFrame(gl); frame.Release(gl); frame.Release(gl); CheckEmpty(state);
    }
    void TestFailures()
    {
        Driver state; driver = &state; const auto gl = Functions(); OpenGLFrameResources frame;
        Reject([&] { frame.BeginFrame(gl, 4097, 16); }); CheckEmpty(state);
        state.failAllocation = true;
        Reject([&] { frame.BeginFrame(gl, 16, 16); }); CheckEmpty(state); state.failAllocation = false;
        state.failStorage = true;
        Reject([&] { frame.BeginFrame(gl, 16, 16); }); CheckEmpty(state); state.failStorage = false;
        Check(frame.BeginFrame(gl, 16, 16)); const auto original = state.draw; frame.EndFrame(gl);
        state.status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        Reject([&] { frame.BeginFrame(gl, 32, 32); });
        Check(state.framebuffers.count(original) == 1 && state.framebuffers.size() == 1 && state.colors.size() == 1);
        state.status = GL_FRAMEBUFFER_COMPLETE;
        // Restore the fake driver's tracked storage dimensions to the retained target.
        state.width = state.height = 16;
        Check(frame.BeginFrame(gl, 16, 16) && state.draw == original);
        state.failFence = true; Reject([&] { frame.EndFrame(gl); });
        Check(state.fences.empty()); state.failFence = false;
        Check(frame.BeginFrame(gl, 16, 16)); frame.EndFrame(gl);
        state.waitResult = GL_WAIT_FAILED;
        Reject([&] { frame.BeginFrame(gl, 16, 16); }); Check(state.fences.size() == 1);
        frame.Release(gl); CheckEmpty(state);
    }
    class FakeContext final : public OpenGLContext
    {
    public:
        std::uint64_t generation = 1;
        bool recreate = false, failRecovery = false;
        std::string missing;
        ~FakeContext() override { driver->LoseContext(); }
        void* Load(const char* name) override
        {
            ++driver->loads;
            if (missing == name) return nullptr;
#define BEAST_RESOLVE_ENTRY(Type, Name) if (std::strcmp(name, #Name) == 0) return reinterpret_cast<void*>(&FakeGL::Name);
            BEAST_GL_ENTRYPOINTS_ALL(BEAST_RESOLVE_ENTRY)
#undef BEAST_RESOLVE_ENTRY
            throw std::runtime_error("Unexpected GL entry");
        }
        void MakeCurrent() override {}
        void GetSize(int& width, int& height) override { width = 640; height = 480; }
        std::uint64_t GetGeneration() const noexcept override { return generation; }
        bool Present() override
        {
            if (!recreate) return true;
            ++generation; driver->LoseContext(); recreate = false;
            if (failRecovery) throw std::runtime_error("Injected EGL recovery failure");
            return false;
        }
    };
    void TestContextLoss(bool failRecovery, bool failReload)
    {
        Driver state; driver = &state;
        Window window{}; window.nativeHandle = reinterpret_cast<void*>(1); window.width = 640; window.height = 480;
        auto context = std::make_unique<FakeContext>(); auto* control = context.get();
        auto rhi = CreateOpenGLRHI(window, std::move(context));
        RHICommandList commands; commands.BeginRenderPass({0, 0, 0, 1});
        commands.ClearRect({10, 20, 30, 40}, {1, 0, 0, 1}); commands.EndRenderPass();
        Check(rhi->Submit(window, commands)); const int firstLoads = state.loads;
        control->recreate = true; control->failRecovery = failRecovery;
        if (failReload) control->missing = "glFenceSync";
        if (failRecovery || failReload) Reject([&] { rhi->Submit(window, commands); });
        else
        {
            Check(!rhi->Submit(window, commands));
            Check(state.loads == firstLoads * 2); CheckEmpty(state);
            Check(rhi->Submit(window, commands));
            Check(state.framebuffers.size() == 1 && state.colors.size() == 1 && state.fences.size() == 1);
        }
        rhi.reset(); CheckEmpty(state);
    }
}
int main()
{
    try
    {
        TestFrames(); TestFailures();
        TestContextLoss(false, false); TestContextLoss(true, false); TestContextLoss(false, true);
        std::puts("OpenGL framebuffer, fence and context-loss tests passed."); return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
