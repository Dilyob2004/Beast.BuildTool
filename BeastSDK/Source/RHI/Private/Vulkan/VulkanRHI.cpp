#include "RHI/Private/Vulkan/VulkanPlatform.h"
#include "RHI/Private/RHIInternal.h"
#include "RHI/Private/Vulkan/VulkanDrawResources.h"
#include "Core/Trace.h"
#include "Core/Application.h"
#include <array>
#include <cstring>
#include <limits>
#include <cmath>

namespace Beast::Detail
{
    class VulkanRHI final : public RHI
    {
        void* library_ = nullptr;
        void* window_ = nullptr;
        RHIOptions options_;
        vk::detail::DispatchLoaderDynamic dispatch_;
        vk::Instance instance_;
        vk::DebugUtilsMessengerEXT debug_;
        vk::SurfaceKHR surface_;
        vk::PhysicalDevice physical_;
        vk::Device device_;
        vk::Queue queue_;
        std::uint32_t queueFamily_ = 0;
        vk::SwapchainKHR swapchain_;
        vk::Extent2D extent_;
        vk::RenderPass pass_;
        std::vector<vk::Image> images_;
        std::vector<vk::ImageView> views_;
        std::vector<vk::Framebuffer> framebuffers_;
        std::vector<vk::Semaphore> rendered_; // Per acquired image, not per frame.
        vk::Semaphore acquired_;
        vk::Fence submitted_;
        vk::CommandPool pool_;
        vk::CommandBuffer buffer_;
        std::unique_ptr<VulkanDrawResources> drawing_;
        RHIFrameStats stats_;
        vk::QueryPool timerPool_;
        std::uint32_t timerBits_ = 0;
        double timestampPeriod_ = 0;
        bool timerPending_ = false;
        std::uint64_t timerFrame_ = 0;
        std::chrono::steady_clock::time_point timerStarted_;
        int requestedWidth_ = 0, requestedHeight_ = 0;
        bool dirty_ = true;

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT,
            vk::DebugUtilsMessageTypeFlagsEXT, const vk::DebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            Log(data && data->pMessage ? data->pMessage : "Vulkan validation message");
            return VK_FALSE;
        }
        void CreateInstance()
        {
            library_ = OpenVulkanLibrary();
            if (!library_) throw std::runtime_error("Vulkan loader is unavailable. Install a Vulkan-capable GPU driver.");
            const auto entry = LoadVulkanEntry(library_);
            if (!entry) throw std::runtime_error("Missing vkGetInstanceProcAddr.");
            dispatch_.init(entry);
            const auto available = vk::enumerateInstanceExtensionProperties(nullptr, dispatch_);
            const auto hasExtension = [&](const char* name)
            { return std::any_of(available.begin(), available.end(), [&](const auto& value) { return std::strcmp(value.extensionName, name) == 0; }); };
            std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VulkanSurfaceExtension()};
            for (const char* extension : extensions)
                if (!hasExtension(extension)) throw std::runtime_error(std::string("Missing Vulkan extension: ") + extension);
            std::vector<const char*> layers;
            if (options_.validation)
            {
                const auto availableLayers = vk::enumerateInstanceLayerProperties(dispatch_);
                if (std::any_of(availableLayers.begin(), availableLayers.end(), [](const auto& layer)
                    { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; }))
                    layers.push_back("VK_LAYER_KHRONOS_validation");
                else Log("Vulkan validation requested, but VK_LAYER_KHRONOS_validation is not installed.");
            }
            const bool useDebug = !layers.empty() && hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            if (useDebug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            vk::ApplicationInfo application{};
            application.pApplicationName = "Beast"; application.pEngineName = "Beast";
            application.apiVersion = VK_API_VERSION_1_0;
            vk::InstanceCreateInfo info{};
            info.pApplicationInfo = &application; info.setPEnabledExtensionNames(extensions); info.setPEnabledLayerNames(layers);
            instance_ = vk::createInstance(info, nullptr, dispatch_);
            dispatch_.init(instance_);
            if (useDebug)
            {
                vk::DebugUtilsMessengerCreateInfoEXT debugInfo{};
                debugInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
                debugInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
                debugInfo.pfnUserCallback = DebugMessage;
                debug_ = instance_.createDebugUtilsMessengerEXT(debugInfo, nullptr, dispatch_);
                Log("Vulkan validation enabled");
            }
        }
        void CreateDevice()
        {
            for (const auto& candidate : instance_.enumeratePhysicalDevices(dispatch_))
            {
                const auto extensions = candidate.enumerateDeviceExtensionProperties(nullptr, dispatch_);
                if (!std::any_of(extensions.begin(), extensions.end(), [](const auto& value)
                    { return std::strcmp(value.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
                const auto families = candidate.getQueueFamilyProperties(dispatch_);
                for (std::uint32_t index = 0; index < families.size(); ++index)
                    if ((families[index].queueFlags & vk::QueueFlagBits::eGraphics) &&
                        candidate.getSurfaceSupportKHR(index, surface_, dispatch_))
                    { physical_ = candidate; queueFamily_ = index; break; }
                if (physical_) break;
            }
            if (!physical_) throw std::runtime_error("No Vulkan GPU with a graphics/present queue and swapchain support.");
            const float priority = 1;
            vk::DeviceQueueCreateInfo queueInfo{};
            queueInfo.queueFamilyIndex = queueFamily_; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
            const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            vk::DeviceCreateInfo info{};
            info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queueInfo;
            info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension;
            device_ = physical_.createDevice(info, nullptr, dispatch_);
            dispatch_.init(device_);
            queue_ = device_.getQueue(queueFamily_, 0, dispatch_);
            drawing_ = std::make_unique<VulkanDrawResources>(physical_, device_, queue_, queueFamily_, dispatch_);
            drawing_->SetPoolBudget(options_.layerPoolBytes);
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer; poolInfo.queueFamilyIndex = queueFamily_;
            pool_ = device_.createCommandPool(poolInfo, nullptr, dispatch_);
            vk::CommandBufferAllocateInfo allocate{};
            allocate.commandPool = pool_; allocate.level = vk::CommandBufferLevel::ePrimary; allocate.commandBufferCount = 1;
            buffer_ = device_.allocateCommandBuffers(allocate, dispatch_).front();
            acquired_ = device_.createSemaphore({}, nullptr, dispatch_);
            vk::FenceCreateInfo fenceInfo{}; fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
            submitted_ = device_.createFence(fenceInfo, nullptr, dispatch_);
            const auto properties = physical_.getProperties(dispatch_);
            if (options_.profiling) {
                timerBits_ = physical_.getQueueFamilyProperties(dispatch_)[queueFamily_].timestampValidBits;
                timestampPeriod_ = properties.limits.timestampPeriod;
                if (timerBits_) { vk::QueryPoolCreateInfo timer{}; timer.queryType = vk::QueryType::eTimestamp; timer.queryCount = 2;
                    timerPool_ = device_.createQueryPool(timer, nullptr, dispatch_); }
            }
            Log((std::string("Vulkan GPU: ") + properties.deviceName.data()).c_str());
        }
        void ResolveTimer()
        {
            if (!timerPending_) return;
            std::array<std::uint64_t, 2> stamps{};
            const auto result = device_.getQueryPoolResults(timerPool_, 0, 2, sizeof(stamps), stamps.data(), sizeof(std::uint64_t), vk::QueryResultFlagBits::e64, dispatch_);
            if (result != vk::Result::eSuccess) return;
            timerPending_ = false;
            const auto ageMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - timerStarted_).count();
            if (ageMs >= std::ldexp(1.0, static_cast<int>(timerBits_)) * timestampPeriod_ / 1000000) {
                stats_.gpuMs.reset(); stats_.gpuFrame = 0; return;
            }
            const auto mask = timerBits_ == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << timerBits_) - 1;
            stats_.gpuMs = double((stamps[1] - stamps[0]) & mask) * timestampPeriod_ / 1000000;
            stats_.gpuFrame = timerFrame_;
        }
        void DestroySwapchain() noexcept
        {
            if (drawing_) drawing_->InvalidatePipelines();
            for (auto framebuffer : framebuffers_) device_.destroyFramebuffer(framebuffer, nullptr, dispatch_);
            framebuffers_.clear();
            for (auto view : views_) device_.destroyImageView(view, nullptr, dispatch_);
            views_.clear();
            for (auto semaphore : rendered_) device_.destroySemaphore(semaphore, nullptr, dispatch_);
            rendered_.clear(); images_.clear();
            if (pass_) device_.destroyRenderPass(pass_, nullptr, dispatch_);
            pass_ = nullptr;
            if (swapchain_) device_.destroySwapchainKHR(swapchain_, nullptr, dispatch_);
            swapchain_ = nullptr;
        }
        bool CreateSwapchain(const Window& window)
        {
            device_.waitIdle(dispatch_);
            DestroySwapchain();
            const auto capabilities = physical_.getSurfaceCapabilitiesKHR(surface_, dispatch_);
            if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment))
                throw std::runtime_error("Vulkan surface does not support color attachments.");
            extent_ = capabilities.currentExtent;
            if (extent_.width == std::numeric_limits<std::uint32_t>::max())
            {
                extent_.width = std::clamp(static_cast<std::uint32_t>(window.width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent_.height = std::clamp(static_cast<std::uint32_t>(window.height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            }
            if (!extent_.width || !extent_.height) return false;
            const auto formats = physical_.getSurfaceFormatsKHR(surface_, dispatch_);
            if (formats.empty()) throw std::runtime_error("Vulkan surface has no supported formats.");
            auto format = formats.front();
            if (formats.size() == 1 && format.format == vk::Format::eUndefined) format.format = vk::Format::eB8G8R8A8Unorm;
            for (const auto& candidate : formats)
                if ((candidate.format == vk::Format::eB8G8R8A8Unorm || candidate.format == vk::Format::eR8G8B8A8Unorm) &&
                    candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) { format = candidate; break; }
            auto mode = vk::PresentModeKHR::eFifo;
            if (!options_.vsync)
            {
                const auto modes = physical_.getSurfacePresentModesKHR(surface_, dispatch_);
                if (std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) != modes.end()) mode = vk::PresentModeKHR::eMailbox;
                else if (std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eImmediate) != modes.end()) mode = vk::PresentModeKHR::eImmediate;
            }
            auto composite = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            for (auto value : {vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied})
                if (capabilities.supportedCompositeAlpha & value) { composite = value; break; }
            std::uint32_t count = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount) count = std::min(count, capabilities.maxImageCount);
            vk::SwapchainCreateInfoKHR info{};
            info.surface = surface_; info.minImageCount = count; info.imageFormat = format.format;
            info.imageColorSpace = format.colorSpace; info.imageExtent = extent_; info.imageArrayLayers = 1;
            info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment; info.imageSharingMode = vk::SharingMode::eExclusive;
            info.preTransform = capabilities.currentTransform; info.compositeAlpha = composite;
            info.presentMode = mode; info.clipped = true;
            swapchain_ = device_.createSwapchainKHR(info, nullptr, dispatch_);
            images_ = device_.getSwapchainImagesKHR(swapchain_, dispatch_);
            vk::AttachmentDescription attachment{};
            attachment.format = format.format; attachment.samples = vk::SampleCountFlagBits::e1;
            attachment.loadOp = vk::AttachmentLoadOp::eClear; attachment.storeOp = vk::AttachmentStoreOp::eStore;
            attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare; attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachment.initialLayout = vk::ImageLayout::eUndefined; attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
            vk::AttachmentReference reference(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
            dependency.srcStageMask = dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            vk::RenderPassCreateInfo passInfo{};
            passInfo.attachmentCount = 1; passInfo.pAttachments = &attachment;
            passInfo.subpassCount = 1; passInfo.pSubpasses = &subpass; passInfo.dependencyCount = 1; passInfo.pDependencies = &dependency;
            pass_ = device_.createRenderPass(passInfo, nullptr, dispatch_);
            for (auto image : images_)
            {
                vk::ImageViewCreateInfo viewInfo{};
                viewInfo.image = image; viewInfo.viewType = vk::ImageViewType::e2D; viewInfo.format = format.format;
                viewInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                views_.push_back(device_.createImageView(viewInfo, nullptr, dispatch_));
                vk::FramebufferCreateInfo framebufferInfo{};
                framebufferInfo.renderPass = pass_; framebufferInfo.attachmentCount = 1; framebufferInfo.pAttachments = &views_.back();
                framebufferInfo.width = extent_.width; framebufferInfo.height = extent_.height; framebufferInfo.layers = 1;
                framebuffers_.push_back(device_.createFramebuffer(framebufferInfo, nullptr, dispatch_));
                rendered_.push_back(device_.createSemaphore({}, nullptr, dispatch_));
            }
            requestedWidth_ = window.width; requestedHeight_ = window.height; dirty_ = false;
            return true;
        }
        void Cleanup() noexcept
        {
            if (device_)
            {
                try { device_.waitIdle(dispatch_); } catch (...) {}
                DestroySwapchain();
                drawing_.reset();
                if (timerPool_) device_.destroyQueryPool(timerPool_, nullptr, dispatch_);
                if (submitted_) device_.destroyFence(submitted_, nullptr, dispatch_);
                if (acquired_) device_.destroySemaphore(acquired_, nullptr, dispatch_);
                if (pool_) device_.destroyCommandPool(pool_, nullptr, dispatch_);
                device_.destroy(nullptr, dispatch_); device_ = nullptr;
            }
            if (instance_)
            {
                if (surface_) instance_.destroySurfaceKHR(surface_, nullptr, dispatch_);
                if (debug_) instance_.destroyDebugUtilsMessengerEXT(debug_, nullptr, dispatch_);
                instance_.destroy(nullptr, dispatch_); instance_ = nullptr;
            }
            CloseVulkanLibrary(library_); library_ = nullptr;
        }
    public:
        VulkanRHI(const Window& window, RHIOptions options) : window_(window.nativeHandle), options_(options)
        {
            try { CreateInstance(); surface_ = CreateVulkanSurface(instance_, window, dispatch_); CreateDevice(); }
            catch (...) { Cleanup(); throw; }
        }
        ~VulkanRHI() override { Cleanup(); }
        RHIBackend GetBackend() const noexcept override { return RHIBackend::Vulkan; }
        RHIFrameStats GetFrameStats() const noexcept override { auto result = stats_; if (drawing_) result.layers = drawing_->GetStats(); return result; }
        bool Submit(const Window& window, const RHICommandList& commands) override
        {
            TraceScope trace("Beast.VK.Submit");
            SubmitCpuTimer cpuTimer(stats_, options_.profiling);
            CheckSubmission(window, window_, commands);
            if (window.width <= 0 || window.height <= 0) return false;
            try
            {
                if ((dirty_ || requestedWidth_ != window.width || requestedHeight_ != window.height) && !CreateSwapchain(window)) return false;
                {
                    TraceScope waitTrace("Beast.VK.FrameFence");
                    if (device_.waitForFences(submitted_, true, 100000000, dispatch_) == vk::Result::eTimeout) return false;
                }
                ResolveTimer();
                drawing_->Prepare(commands, pass_);
                const auto acquire = [&] {
                    TraceScope acquireTrace("Beast.VK.Acquire");
                    return device_.acquireNextImageKHR(swapchain_, 100000000, acquired_, nullptr, dispatch_);
                }();
                if (acquire.result == vk::Result::eTimeout || acquire.result == vk::Result::eNotReady) return false;
                const auto index = acquire.value;
                dirty_ = acquire.result == vk::Result::eSuboptimalKHR;
                TraceScope recordTrace("Beast.VK.Record");
                buffer_.reset({}, dispatch_);
                vk::CommandBufferBeginInfo begin{}; begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
                buffer_.begin(begin, dispatch_);
                const bool sample = timerPool_ && !timerPending_;
                if (sample) { buffer_.resetQueryPool(timerPool_, 0, 2, dispatch_);
                    buffer_.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, timerPool_, 0, dispatch_); }
                drawing_->DrawLayers(buffer_);
                const auto color = commands.GetClearColor();
                vk::ClearValue clear(vk::ClearColorValue(std::array<float, 4>{color.r, color.g, color.b, color.a}));
                vk::RenderPassBeginInfo passInfo{};
                passInfo.renderPass = pass_; passInfo.framebuffer = framebuffers_[index];
                passInfo.renderArea = vk::Rect2D(vk::Offset2D(0, 0), extent_); passInfo.clearValueCount = 1; passInfo.pClearValues = &clear;
                buffer_.beginRenderPass(passInfo, vk::SubpassContents::eInline, dispatch_);
                std::uint32_t firstVertex = 0;
                for (const auto& operation : commands.GetCommands())
                {
                    if (const auto* draw = std::get_if<RHIDrawTriangles>(&operation))
                    {
                        drawing_->Draw(buffer_, *draw, firstVertex, extent_);
                        firstVertex += static_cast<std::uint32_t>(draw->vertices.size());
                        continue;
                    }
                    const auto& command = std::get<RHIClearRect>(operation);
                    const auto rect = Clip(command.rect, static_cast<int>(extent_.width), static_cast<int>(extent_.height));
                    if (!rect.width || !rect.height) continue;
                    const auto c = command.color;
                    vk::ClearAttachment attachment{};
                    attachment.aspectMask = vk::ImageAspectFlagBits::eColor; attachment.colorAttachment = 0;
                    attachment.clearValue = vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{c.r, c.g, c.b, c.a}));
                    vk::ClearRect region{};
                    region.rect = vk::Rect2D(vk::Offset2D(rect.x, rect.y), vk::Extent2D(rect.width, rect.height));
                    region.baseArrayLayer = 0; region.layerCount = 1;
                    buffer_.clearAttachments(attachment, region, dispatch_);
                }
                buffer_.endRenderPass(dispatch_);
                if (sample) buffer_.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timerPool_, 1, dispatch_);
                buffer_.end(dispatch_);
                recordTrace.End();
                const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                vk::SubmitInfo submit{};
                submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired_; submit.pWaitDstStageMask = &waitStage;
                submit.commandBufferCount = 1; submit.pCommandBuffers = &buffer_;
                submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &rendered_[index];
                device_.resetFences(submitted_, dispatch_);
                if (sample) timerStarted_ = std::chrono::steady_clock::now();
                {
                    TraceScope queueTrace("Beast.VK.QueueSubmit");
                    queue_.submit(submit, submitted_, dispatch_);
                }
                ++stats_.submittedFrames;
                if (sample) { timerPending_ = true; timerFrame_ = stats_.submittedFrames; }
                drawing_->CommitLayers();
                vk::PresentInfoKHR present{};
                present.waitSemaphoreCount = 1; present.pWaitSemaphores = &rendered_[index];
                present.swapchainCount = 1; present.pSwapchains = &swapchain_; present.pImageIndices = &index;
                {
                    TraceScope presentTrace("Beast.VK.Present");
                    if (queue_.presentKHR(present, dispatch_) == vk::Result::eSuboptimalKHR) dirty_ = true;
                }
                return true;
            }
            catch (const vk::OutOfDateKHRError&) { dirty_ = true; return false; }
            catch (const vk::SurfaceLostKHRError&)
            {
                device_.waitIdle(dispatch_); DestroySwapchain();
                instance_.destroySurfaceKHR(surface_, nullptr, dispatch_); surface_ = nullptr;
                surface_ = CreateVulkanSurface(instance_, window, dispatch_);
                dirty_ = true; return false;
            }
        }
    };
    std::unique_ptr<RHI> CreateVulkanRHI(const Window& window, RHIOptions options)
    { return std::make_unique<VulkanRHI>(window, options); }
}
