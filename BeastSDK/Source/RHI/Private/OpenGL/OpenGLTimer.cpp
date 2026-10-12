#include "RHI/Private/OpenGL/OpenGLTimer.h"
#include "RHI/Private/OpenGL/OpenGLContext.h"
#include <cstring>
#include <limits>
#include <cmath>

namespace Beast::Detail
{
    namespace { constexpr GLenum Timestamp = 0x8E28, CounterBits = 0x8864, Disjoint = 0x8FBB; }
    void OpenGLTimer::Initialize(OpenGLContext& context, const OpenGLFunctions& gl)
    {
        if (initialized_) return;
        initialized_ = true;
#if defined(__ANDROID__)
        GLint count = 0; gl.glGetIntegerv(GL_NUM_EXTENSIONS, &count); bool supported = false;
        for (GLint i = 0; i < count; ++i) {
            const auto* extension = gl.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (extension && std::strcmp(reinterpret_cast<const char*>(extension), "GL_EXT_disjoint_timer_query") == 0) supported = true;
        }
        if (!supported) return;
        const char* suffix = "EXT";
#else
        const char* suffix = "";
#endif
#define LOAD_TIMER(member, name) member = reinterpret_cast<decltype(member)>(context.Load((std::string(name) + suffix).c_str()))
        LOAD_TIMER(generate_, "glGenQueries"); LOAD_TIMER(destroy_, "glDeleteQueries"); LOAD_TIMER(query_, "glGetQueryiv");
        LOAD_TIMER(counter_, "glQueryCounter"); LOAD_TIMER(available_, "glGetQueryObjectuiv"); LOAD_TIMER(result_, "glGetQueryObjectui64v");
#undef LOAD_TIMER
        if (!generate_ || !destroy_ || !query_ || !counter_ || !available_ || !result_) return;
        GLint bits = 0; query_(Timestamp, CounterBits, &bits);
        if (bits <= 0 || bits > 64) return;
        bits_ = static_cast<unsigned>(bits); generate_(2, ids_);
        if (!ids_[0] || !ids_[1]) { Release(); initialized_ = true; }
#if defined(__ANDROID__)
        GLint ignored = 0; gl.glGetIntegerv(Disjoint, &ignored); // Start a fresh disjoint interval.
#else
        (void)gl;
#endif
    }
    void OpenGLTimer::Resolve(const OpenGLFunctions& gl, RHIFrameStats& stats)
    {
        if (!bits_) return;
#if defined(__ANDROID__)
        GLint disjoint = 0; gl.glGetIntegerv(Disjoint, &disjoint);
        if (disjoint) { invalid_ = pending_; stats.gpuMs.reset(); stats.gpuFrame = 0; }
#else
        (void)gl;
#endif
        if (!pending_) return;
        GLuint ready = 0; available_(ids_[1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return; // Never wait just to obtain a measurement.
        pending_ = false;
        const auto ageMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();
        // A long pause can span more than one hardware counter period: modulo
        // subtraction would then silently report a shorter, misleading interval.
        if (invalid_ || ageMs >= std::ldexp(1.0, static_cast<int>(bits_)) / 1000000) {
            invalid_ = false; stats.gpuMs.reset(); stats.gpuFrame = 0; return;
        }
        GLuint64 begin = 0, end = 0; result_(ids_[0], GL_QUERY_RESULT, &begin); result_(ids_[1], GL_QUERY_RESULT, &end);
        const auto mask = bits_ == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << bits_) - 1;
        stats.gpuMs = double((end - begin) & mask) / 1000000; stats.gpuFrame = frame_;
    }
    void OpenGLTimer::Begin(std::uint64_t frame)
    {
        if (!bits_ || pending_) return;
        frame_ = frame; started_ = std::chrono::steady_clock::now(); counter_(ids_[0], Timestamp); recording_ = true;
    }
    void OpenGLTimer::End()
    { if (recording_) { counter_(ids_[1], Timestamp); recording_ = false; pending_ = true; } }
    void OpenGLTimer::Release() noexcept
    { if (destroy_) destroy_(2, ids_); Abandon(); }
}
