#include "Beast.h"
#include "Core/HotReload.h"
#if defined(__ANDROID__)
#include <android_native_app_glue.h>
extern "C" void android_main(android_app* context)
{
    const std::string directory = std::string(context->activity->internalDataPath) + "/beast-hot";
    Beast::HotReload::RunHost(directory.c_str(), "libBeastModule_initial.so", BEAST_HOT_ABI, context);
}
extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity* activity, void* state, size_t size)
{ BeastNativeActivityOnCreate(activity, state, size); }
#else
#include <filesystem>
#include <windows.h>
int main()
{
    SetConsoleCtrlHandler(+[](DWORD signal) -> BOOL { return signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT; }, TRUE);
    wchar_t buffer[32768];
    const auto length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (!length || length == 32768) return 1;
    const auto directory = std::filesystem::path(buffer).parent_path();
    return Beast::HotReload::RunHost(directory.string().c_str(), (directory / "BeastModule_initial.dll").string().c_str(), BEAST_HOT_ABI);
}
#endif
