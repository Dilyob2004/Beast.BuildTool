#pragma once
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_DEFAULT_DISPATCHER
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.hpp>
#include "Core/Window.h"

namespace Beast::Detail
{
    // Platform-specific loading; all Vulkan resource APIs below use vk:: types.
    void* OpenVulkanLibrary();
    void CloseVulkanLibrary(void* library) noexcept;
    PFN_vkGetInstanceProcAddr LoadVulkanEntry(void* library);
    const char* VulkanSurfaceExtension();
    vk::SurfaceKHR CreateVulkanSurface(vk::Instance, const Window&, const vk::detail::DispatchLoaderDynamic&);
}
