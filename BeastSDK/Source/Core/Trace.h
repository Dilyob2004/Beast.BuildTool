#pragma once
#include <cstdint>
#if defined(__ANDROID__)
#include <android/trace.h>
#endif

namespace Beast::Detail
{
    // Android system tracing only; no log output or file I/O. Windows is a no-op.
    // Names are static labels, never application text. Scopes stay on one thread.
    inline bool TraceEnabled() noexcept
    {
#if defined(__ANDROID__)
        return ATrace_isEnabled();
#else
        return false;
#endif
    }
    inline void TraceCounter(const char* name, std::int64_t value) noexcept
    {
#if defined(__ANDROID__)
        if (TraceEnabled()) ATrace_setCounter(name, value); // Android 10 / API 29.
#else
        (void)name; (void)value;
#endif
    }
    class TraceScope final
    {
        bool active_;
    public:
        explicit TraceScope(const char* name) noexcept : active_(TraceEnabled())
        {
#if defined(__ANDROID__)
            if (active_) ATrace_beginSection(name);
#else
            (void)name;
#endif
        }
        TraceScope(const TraceScope&) = delete;
        TraceScope& operator=(const TraceScope&) = delete;
        ~TraceScope() { End(); }
        void End() noexcept
        {
#if defined(__ANDROID__)
            if (active_) ATrace_endSection();
#endif
            active_ = false;
        }
    };
}
