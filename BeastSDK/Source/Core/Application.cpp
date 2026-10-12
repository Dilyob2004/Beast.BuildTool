#include "Core/Application.h"
#include <cstdio>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace Beast
{
    void Application::Dispatch(const Event& event)
    {
        input_.Apply(event);
        if (event.type == EventType::FocusGained) window_.focused = true;
        if (event.type == EventType::FocusLost) window_.focused = false;
        OnEvent(event);
        if (event.type == EventType::KeyDown && event.key == Key::Back && !event.repeat && !OnBack()) RequestExit();
        if (event.type == EventType::CloseRequested) RequestExit();
    }

    void Log(const char* message)
    {
        if (!message) return;
#if defined(__ANDROID__)
        __android_log_write(ANDROID_LOG_INFO, "Beast", message);
#else
        std::fprintf(stdout, "[Beast] %s\n", message);
        std::fflush(stdout);
#endif
    }
}
