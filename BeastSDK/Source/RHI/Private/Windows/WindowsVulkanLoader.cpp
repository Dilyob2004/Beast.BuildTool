#if defined(_WIN32)
#include "RHI/Private/Vulkan/VulkanPlatform.h"
namespace Beast::Detail
{
    void* OpenVulkanLibrary() { return LoadLibraryW(L"vulkan-1.dll"); }
    void CloseVulkanLibrary(void* library) noexcept { if (library) FreeLibrary(static_cast<HMODULE>(library)); }
    PFN_vkGetInstanceProcAddr LoadVulkanEntry(void* library)
    { return reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr")); }
    const char* VulkanSurfaceExtension() { return VK_KHR_WIN32_SURFACE_EXTENSION_NAME; }
    vk::SurfaceKHR CreateVulkanSurface(vk::Instance instance, const Window& window, const vk::detail::DispatchLoaderDynamic& dispatcher)
    {
        vk::Win32SurfaceCreateInfoKHR info{};
        info.hinstance = GetModuleHandleW(nullptr); info.hwnd = static_cast<HWND>(window.nativeHandle);
        return instance.createWin32SurfaceKHR(info, nullptr, dispatcher);
    }
}
#endif
