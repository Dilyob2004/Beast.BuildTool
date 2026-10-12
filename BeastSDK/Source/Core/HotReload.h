#pragma once
#include "UI/Application.h"

namespace Beast::HotReload
{
    // This is a same-toolchain C++ development ABI, not a cross-compiler plugin ABI.
    // The loader checks the full SDK/toolchain/build fingerprint before Create.
    struct Module
    {
        unsigned version, size;
        const char* fingerprint;
        UI::Scene* (*create)();
        UI::AppOptions (*options)();
    };
    int RunHost(const char* directory, const char* initialLibrary, const char* fingerprint, void* context = nullptr);
}

#if defined(BEAST_HOT_MODULE)
#if defined(_WIN32)
#define BEAST_HOT_EXPORT __declspec(dllexport)
#else
#define BEAST_HOT_EXPORT __attribute__((visibility("default")))
#endif
#define BEAST_HOT_UI_APPLICATION(SceneType, ...) \
    extern "C" BEAST_HOT_EXPORT const Beast::HotReload::Module* BeastHotModule() { \
        static const Beast::HotReload::Module module{1, sizeof(Beast::HotReload::Module), BEAST_HOT_ABI, \
            +[]() -> Beast::UI::Scene* { return new SceneType(); }, \
            +[]() -> Beast::UI::AppOptions { Beast::UI::AppOptions options __VA_OPT__(= __VA_ARGS__); return options; }}; \
        return &module; }
#endif
