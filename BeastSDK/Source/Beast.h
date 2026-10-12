#pragma once
#include "Platform/Platform.h"
#include "Core/Application.h"
#include "Core/Memory/Memory.h"
#include "RHI/RHI.h"
#include "UI/UI.h"
#include "UI/Application.h"
#include "UI/Build.h"

#if defined(__ANDROID__)
#include <android/native_activity.h>
struct android_app;
extern "C" void BeastNativeActivityOnCreate(ANativeActivity*, void*, size_t);
#define BEAST_APPLICATION(ApplicationType) \
    extern "C" void android_main(android_app* context) { \
        ApplicationType application; Beast::Run(application, context); } \
    extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate( \
        ANativeActivity* activity, void* state, size_t size) { \
        BeastNativeActivityOnCreate(activity, state, size); }
#else
#define BEAST_APPLICATION(ApplicationType) \
    int main() { ApplicationType application; return Beast::Run(application); }
#endif

// Exactly once per executable, at namespace scope. The variadic argument is
// an AppOptions aggregate, including commas in designated initializers.
#if defined(BEAST_HOT_MODULE)
#include "Core/HotReload.h"
#define BEAST_UI_APPLICATION(SceneType, ...) BEAST_HOT_UI_APPLICATION(SceneType, __VA_ARGS__)
#else
#define BEAST_UI_APPLICATION(SceneType, ...) \
    class BeastGeneratedUiApplication final : public Beast::UI::SceneApplication<SceneType> { \
        using Base = Beast::UI::SceneApplication<SceneType>; \
    public: BeastGeneratedUiApplication() : Base(__VA_ARGS__) {} }; \
    BEAST_APPLICATION(BeastGeneratedUiApplication)
#endif
