#if defined(__ANDROID__)
#include "RHI/Private/Vulkan/VulkanPlatform.h"
#include <dlfcn.h>
namespace Beast::Detail
{
    void* OpenVulkanLibrary() { return dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL); }
    void CloseVulkanLibrary(void* library) noexcept { if (library) dlclose(library); }
    PFN_vkGetInstanceProcAddr LoadVulkanEntry(void* library)
    { return reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr")); }
    const char* VulkanSurfaceExtension() { return VK_KHR_ANDROID_SURFACE_EXTENSION_NAME; }
    vk::SurfaceKHR CreateVulkanSurface(vk::Instance instance, const Window& window, const vk::detail::DispatchLoaderDynamic& dispatcher)
    {
        vk::AndroidSurfaceCreateInfoKHR info{};
        info.window = static_cast<ANativeWindow*>(window.nativeHandle);
        return instance.createAndroidSurfaceKHR(info, nullptr, dispatcher);
    }
}
#endif
