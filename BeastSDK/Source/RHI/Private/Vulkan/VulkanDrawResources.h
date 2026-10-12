#pragma once
#include "RHI/Private/Vulkan/VulkanPlatform.h"
#include "RHI/RHICommandList.h"
#include <unordered_map>
#include "RHI/Private/LayerTargetPool.h"

namespace Beast::Detail
{
    // One frame in flight, synchronized by VulkanRHI. Resource deletion, vertex
    // writes and Prepare must occur only after its submission fence completes.
    class VulkanDrawResources
    {
        vk::PhysicalDevice physical_;
        vk::Device device_;
        vk::Queue queue_;
        std::uint32_t family_;
        const vk::detail::DispatchLoaderDynamic& d_;
        vk::DescriptorSetLayout setLayout_;
        vk::PipelineLayout layout_;
        vk::CommandPool uploadPool_;
        vk::RenderPass layerPass_;
        struct Buffer { vk::Buffer buffer; vk::DeviceMemory memory; vk::DeviceSize capacity = 0; } vertices_;
        struct Texture
        {
            std::weak_ptr<const RHITexture> owner;
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
            vk::DescriptorPool pool;
            vk::DescriptorSet set;
            vk::Framebuffer framebuffer;
            bool rendered = true;
            bool recyclable = false; // Nearest RGBA8 bitmap, never a render target.
            std::uint32_t width = 0, height = 0;
            std::uint64_t bytes = 0;
        };
        struct Pipeline { std::weak_ptr<const RHIPipeline> owner; vk::Pipeline pipeline; };
        std::unordered_map<const RHITexture*, Texture> textures_;
        std::unordered_map<const RHIPipeline*, Pipeline> pipelines_;
        std::unordered_map<const RHIPipeline*, Pipeline> layerPipelines_;
        struct Layer { RHITextureHandle texture; std::uint32_t firstVertex; };
        std::vector<Layer> pendingLayers_;
        struct UploadJob { const RHITexture* texture; Buffer staging; vk::CommandBuffer command; };
        std::vector<UploadJob> uploads_;
        vk::DeviceSize uploadBytes_ = 0;
        static constexpr vk::DeviceSize UploadBudget = 8ull * 1024 * 1024;
        static constexpr std::size_t UploadJobLimit = 128; // Also bound driver/staging allocation overhead for tiny images.
        LayerTargetPool<Texture> layerPool_;
        LayerTargetPool<Texture> bitmapPool_;
        RHILayerStats bitmapStats_;
        std::uint64_t poolBudget_ = 32ull * 1024 * 1024;
        RHILayerStats stats_;
        std::uint32_t MemoryType(std::uint32_t mask, vk::MemoryPropertyFlags flags) const;
        Buffer CreateBuffer(vk::DeviceSize bytes, vk::BufferUsageFlags usage);
        void Destroy(Buffer& buffer) noexcept;
        void Destroy(Texture& texture) noexcept;
        void Initialize();
        void Upload(const RHITextureHandle& texture);
        void FlushUploads();
        void ClearUploads(bool discardTextures) noexcept; // No submitted work may still be executing.
        void CreatePipeline(const RHIPipelineHandle& pipeline, vk::RenderPass pass, bool layer = false);
    public:
        VulkanDrawResources(vk::PhysicalDevice physical, vk::Device device, vk::Queue queue,
            std::uint32_t family, const vk::detail::DispatchLoaderDynamic& dispatch)
            : physical_(physical), device_(device), queue_(queue), family_(family), d_(dispatch) {}
        ~VulkanDrawResources() { Release(); }
        void Prepare(const RHICommandList& commands, vk::RenderPass pass);
        void Draw(vk::CommandBuffer buffer, const RHIDrawTriangles& draw, std::uint32_t firstVertex, vk::Extent2D extent, bool layer = false);
        void DrawLayers(vk::CommandBuffer buffer);
        void CommitLayers();
        void SetPoolBudget(std::uint64_t bytes) noexcept { poolBudget_ = bytes; }
        RHILayerStats GetStats() const noexcept { auto result = stats_; layerPool_.ReadStats(result); return result; }
        void InvalidatePipelines() noexcept; // Before destroying/changing the compatible render pass.
        void Release() noexcept; // Caller waits for device idle first.
    };
}
