#include "RHI/Private/OpenGL/OpenGLTimer.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
using namespace Beast;
using namespace Beast::Detail;
namespace {
    void Check(bool v) { if (!v) throw std::runtime_error("GL timer assertion failed."); }
    struct Driver { GLint bits = 64; bool ready = false, fail = false; int generated = 0, deleted = 0, counters = 0, results = 0;
        GLuint64 begin = 1000000, end = 3000000; } driver;
    void APIENTRY Gen(GLsizei n, GLuint* ids) { Check(n == 2); ++driver.generated; ids[0] = driver.fail ? 0 : 1; ids[1] = driver.fail ? 0 : 2; }
    void APIENTRY Delete(GLsizei n, const GLuint* ids) { Check(n == 2); if (ids[0]) ++driver.deleted; if (ids[1]) ++driver.deleted; }
    void APIENTRY Query(GLenum target, GLenum field, GLint* bits) { Check(target == GL_TIMESTAMP && field == GL_QUERY_COUNTER_BITS); *bits = driver.bits; }
    void APIENTRY Counter(GLuint id, GLenum target) { Check((id == 1 || id == 2) && target == GL_TIMESTAMP); ++driver.counters; }
    void APIENTRY Available(GLuint id, GLenum field, GLuint* ready) { Check(id == 2 && field == GL_QUERY_RESULT_AVAILABLE); *ready = driver.ready ? 1 : 0; }
    void APIENTRY Result(GLuint id, GLenum field, GLuint64* value) {
        Check(driver.ready && field == GL_QUERY_RESULT); ++driver.results; *value = id == 1 ? driver.begin : driver.end;
    }
    class Context final : public OpenGLContext {
    public:
        const char* missing = ""; int loads = 0;
        void* Load(const char* name) override {
            ++loads; if (!std::strcmp(name, missing)) return nullptr;
#define ENTRY(gl, fn) if (!std::strcmp(name, gl)) return reinterpret_cast<void*>(fn)
            ENTRY("glGenQueries", Gen); ENTRY("glDeleteQueries", Delete); ENTRY("glGetQueryiv", Query);
            ENTRY("glQueryCounter", Counter); ENTRY("glGetQueryObjectuiv", Available); ENTRY("glGetQueryObjectui64v", Result);
#undef ENTRY
            throw std::runtime_error("Unexpected GL timer load.");
        }
        void* LoadBase(const char* name) override { return Load(name); }
        void MakeCurrent() override {}
        bool Present() override { return true; }
        void GetSize(int& w, int& h) override { w = h = 1; }
    };
}
int main() {
    try {
        OpenGLFunctions gl; // No required GL calls on the desktop optional-timer path.
        for (const char* name : {"glGenQueries", "glDeleteQueries", "glGetQueryiv", "glQueryCounter", "glGetQueryObjectuiv", "glGetQueryObjectui64v"}) {
            driver = {}; Context context; context.missing = name; OpenGLTimer timer; RHIFrameStats stats;
            timer.Initialize(context, gl); timer.Initialize(context, gl); Check(context.loads == 6);
            timer.Begin(1); timer.End(); timer.Resolve(gl, stats); Check(!stats.gpuMs && driver.counters == 0); timer.Release();
        }
        for (int bits : {0, 65}) { driver = {}; driver.bits = bits; Context context; OpenGLTimer timer;
            timer.Initialize(context, gl); timer.Begin(1); timer.End(); Check(driver.generated == 0); timer.Release(); }
        driver = {}; driver.fail = true; Context failed; OpenGLTimer allocation;
        allocation.Initialize(failed, gl); allocation.Begin(1); allocation.End(); Check(driver.counters == 0); allocation.Release();
        driver = {}; Context context; OpenGLTimer timer; RHIFrameStats stats;
        timer.Initialize(context, gl); timer.Begin(7); timer.End(); timer.Resolve(gl, stats);
        Check(!stats.gpuMs && driver.results == 0); timer.Begin(8); timer.End(); Check(driver.counters == 2); // Pending slots are not overwritten.
        driver.ready = true; timer.Resolve(gl, stats); Check(stats.gpuFrame == 7 && stats.gpuMs == 2 && driver.results == 2);
        timer.Resolve(gl, stats); Check(driver.results == 2); timer.Begin(8); timer.End(); timer.Resolve(gl, stats); Check(stats.gpuFrame == 8);
        timer.Release(); Check(driver.deleted == 2);
        driver = {}; driver.bits = 32; driver.begin = 0xfffffff0ull; driver.end = 0x20; driver.ready = true;
        timer.Initialize(context, gl); timer.Begin(9); timer.End(); timer.Resolve(gl, stats); Check(std::abs(*stats.gpuMs - .000048) < 1e-12);
        timer.Abandon(); Check(driver.deleted == 0); // No old-name deletion after context loss.
        timer.Initialize(context, gl); timer.Begin(1); timer.End(); timer.Release(); Check(driver.deleted == 2);
        std::puts("GL timer: optional/missing entry points, unsupported counters, allocation failure, nonblocking availability, no slot overwrite, wrap, release/loss passed.");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
