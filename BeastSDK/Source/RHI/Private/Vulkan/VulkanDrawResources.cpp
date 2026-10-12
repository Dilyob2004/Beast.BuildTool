#include "RHI/Private/Vulkan/VulkanDrawResources.h"
#include "Core/Trace.h"
#include "RHI/Private/RHIInternal.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <functional>
#include <unordered_set>

namespace Beast::Detail
{
    std::uint32_t VulkanDrawResources::MemoryType(std::uint32_t mask, vk::MemoryPropertyFlags flags) const
    {
        const auto memory = physical_.getMemoryProperties(d_);
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((mask & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("Vulkan GPU has no compatible resource memory type.");
    }
    VulkanDrawResources::Buffer VulkanDrawResources::CreateBuffer(vk::DeviceSize bytes, vk::BufferUsageFlags usage)
    {
        Buffer result;
        try
        {
            vk::BufferCreateInfo info{}; info.size = bytes; info.usage = usage; info.sharingMode = vk::SharingMode::eExclusive;
            result.buffer = device_.createBuffer(info, nullptr, d_);
            const auto requirements = device_.getBufferMemoryRequirements(result.buffer, d_);
            vk::MemoryAllocateInfo allocate(requirements.size, MemoryType(requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
            result.memory = device_.allocateMemory(allocate, nullptr, d_);
            device_.bindBufferMemory(result.buffer, result.memory, 0, d_); result.capacity = bytes;
            return result;
        }
        catch (...) { Destroy(result); throw; }
    }
    void VulkanDrawResources::Destroy(Buffer& value) noexcept
    {
        if (value.buffer) device_.destroyBuffer(value.buffer, nullptr, d_);
        if (value.memory) device_.freeMemory(value.memory, nullptr, d_);
        value = {};
    }
    void VulkanDrawResources::Destroy(Texture& value) noexcept
    {
        if (value.framebuffer) device_.destroyFramebuffer(value.framebuffer, nullptr, d_);
        if (value.pool) device_.destroyDescriptorPool(value.pool, nullptr, d_);
        if (value.sampler) device_.destroySampler(value.sampler, nullptr, d_);
        if (value.view) device_.destroyImageView(value.view, nullptr, d_);
        if (value.image) device_.destroyImage(value.image, nullptr, d_);
        if (value.memory) device_.freeMemory(value.memory, nullptr, d_);
        value = {};
    }
    void VulkanDrawResources::Initialize()
    {
        if (layout_) return;
        try
        {
            const std::array bindings{
                vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment),
                vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment)};
            vk::DescriptorSetLayoutCreateInfo set{}; set.setBindings(bindings);
            setLayout_ = device_.createDescriptorSetLayout(set, nullptr, d_);
            vk::CommandPoolCreateInfo pool{}; pool.queueFamilyIndex = family_; pool.flags = vk::CommandPoolCreateFlagBits::eTransient;
            uploadPool_ = device_.createCommandPool(pool, nullptr, d_);
            vk::PipelineLayoutCreateInfo layout{}; layout.setSetLayouts(setLayout_);
            const vk::PushConstantRange constants(vk::ShaderStageFlagBits::eFragment, 0, sizeof(RHIDrawConstants));
            layout.setPushConstantRanges(constants);
            layout_ = device_.createPipelineLayout(layout, nullptr, d_);
            vk::AttachmentDescription attachment{};
            attachment.format = vk::Format::eR8G8B8A8Unorm; attachment.samples = vk::SampleCountFlagBits::e1;
            attachment.loadOp = vk::AttachmentLoadOp::eClear; attachment.storeOp = vk::AttachmentStoreOp::eStore;
            attachment.initialLayout = vk::ImageLayout::eUndefined; attachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            const vk::AttachmentReference color(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::SubpassDescription subpass{}; subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics; subpass.setColorAttachments(color);
            const std::array dependencies{
                vk::SubpassDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eColorAttachmentWrite),
                vk::SubpassDependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead)};
            vk::RenderPassCreateInfo pass{}; pass.setAttachments(attachment); pass.setSubpasses(subpass); pass.setDependencies(dependencies);
            layerPass_ = device_.createRenderPass(pass, nullptr, d_);
        }
        catch (...) { Release(); throw; }
    }
    void VulkanDrawResources::Upload(const RHITextureHandle& handle)
    {
        if (textures_.contains(handle.get())) return;
        TraceScope trace("Beast.VK.TextureCreate");
        const auto& desc = handle->GetDesc();
        // Atlas pages are immutable snapshots too: several can be created in a
        // scrolling frame. Bound staging memory, but do not wait once per page.
        if (!handle->GetContent() && !uploads_.empty() &&
            desc.pixels.size() > UploadBudget - uploadBytes_) FlushUploads();
        const auto properties = physical_.getProperties(d_);
        if (desc.width > properties.limits.maxImageDimension2D || desc.height > properties.limits.maxImageDimension2D)
            throw std::runtime_error("Texture exceeds Vulkan maxImageDimension2D.");
        auto required = vk::FormatFeatureFlagBits::eSampledImage;
        vk::FormatFeatureFlags features = required;
        if (handle->GetContent()) features |= vk::FormatFeatureFlagBits::eColorAttachment;
        if (desc.filter == RHIFilter::Linear) features |= vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((physical_.getFormatProperties(vk::Format::eR8G8B8A8Unorm, d_).optimalTilingFeatures & features) != features)
            throw std::runtime_error("Vulkan GPU does not support the requested RGBA8 texture sampling.");
        Texture native;
        if (handle->GetContent() && layerPool_.Take(desc.width, desc.height, native)) {
            native.owner = handle; native.rendered = false; ++stats_.reuses;
            try { textures_.emplace(handle.get(), native); }
            catch (...) { Destroy(native); throw; }
            return;
        }
        native.owner = handle; native.width = desc.width; native.height = desc.height;
        const bool recycled = !handle->GetContent() && desc.filter == RHIFilter::Nearest &&
            bitmapPool_.Take(desc.width, desc.height, native);
        native.owner = handle;
        native.recyclable = !handle->GetContent() && desc.filter == RHIFilter::Nearest;
        if (recycled) ++bitmapStats_.reuses;
        Buffer staging;
        vk::CommandBuffer upload;
        bool cached = false;
        try
        {
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            if (!recycled)
            {
            vk::ImageCreateInfo image{};
            image.imageType = vk::ImageType::e2D; image.format = vk::Format::eR8G8B8A8Unorm;
            image.extent = vk::Extent3D(desc.width, desc.height, 1); image.mipLevels = 1; image.arrayLayers = 1;
            image.samples = vk::SampleCountFlagBits::e1; image.tiling = vk::ImageTiling::eOptimal;
            image.usage = vk::ImageUsageFlagBits::eSampled | (handle->GetContent() ? vk::ImageUsageFlagBits::eColorAttachment : vk::ImageUsageFlagBits::eTransferDst);
            image.sharingMode = vk::SharingMode::eExclusive; image.initialLayout = vk::ImageLayout::eUndefined;
            native.image = device_.createImage(image, nullptr, d_);
            const auto requirements = device_.getImageMemoryRequirements(native.image, d_);
            native.bytes = requirements.size;
            vk::MemoryAllocateInfo allocate(requirements.size, MemoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
            native.memory = device_.allocateMemory(allocate, nullptr, d_);
            device_.bindImageMemory(native.image, native.memory, 0, d_);
            vk::ImageViewCreateInfo view{}; view.image = native.image; view.viewType = vk::ImageViewType::e2D;
            view.format = image.format; view.subresourceRange = range;
            native.view = device_.createImageView(view, nullptr, d_);
            vk::SamplerCreateInfo sampler{};
            sampler.magFilter = sampler.minFilter = desc.filter == RHIFilter::Linear ? vk::Filter::eLinear : vk::Filter::eNearest;
            sampler.mipmapMode = vk::SamplerMipmapMode::eNearest;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            sampler.maxLod = 0;
            native.sampler = device_.createSampler(sampler, nullptr, d_);
            const std::array sizes{vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 1), vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1)};
            vk::DescriptorPoolCreateInfo pool{}; pool.maxSets = 1; pool.setPoolSizes(sizes);
            native.pool = device_.createDescriptorPool(pool, nullptr, d_);
            vk::DescriptorSetAllocateInfo sets{}; sets.descriptorPool = native.pool; sets.setSetLayouts(setLayout_);
            native.set = device_.allocateDescriptorSets(sets, d_).front();
            const std::array infos{
                vk::DescriptorImageInfo(nullptr, native.view, vk::ImageLayout::eShaderReadOnlyOptimal),
                vk::DescriptorImageInfo(native.sampler, nullptr, vk::ImageLayout::eUndefined)};
            std::array<vk::WriteDescriptorSet, 2> writes{};
            for (std::uint32_t i = 0; i < 2; ++i)
            {
                writes[i].dstSet = native.set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
                writes[i].descriptorType = sizes[i].type; writes[i].pImageInfo = &infos[i];
            }
            device_.updateDescriptorSets(writes, {}, d_);
            if (handle->GetContent())
            {
                vk::FramebufferCreateInfo framebuffer{}; framebuffer.renderPass = layerPass_; framebuffer.setAttachments(native.view);
                framebuffer.width = desc.width; framebuffer.height = desc.height; framebuffer.layers = 1;
                native.framebuffer = device_.createFramebuffer(framebuffer, nullptr, d_); native.rendered = false;
                textures_.emplace(handle.get(), native); ++stats_.allocations; return;
            }
            ++bitmapStats_.allocations;
            }
            staging = CreateBuffer(desc.pixels.size(), vk::BufferUsageFlagBits::eTransferSrc);
            void* mapped = device_.mapMemory(staging.memory, 0, staging.capacity, {}, d_);
            std::memcpy(mapped, desc.pixels.data(), desc.pixels.size()); device_.unmapMemory(staging.memory, d_);
            vk::CommandBufferAllocateInfo command{}; command.commandPool = uploadPool_;
            command.level = vk::CommandBufferLevel::ePrimary; command.commandBufferCount = 1;
            upload = device_.allocateCommandBuffers(command, d_).front();
            vk::CommandBufferBeginInfo begin{}; begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            upload.begin(begin, d_);
            vk::ImageMemoryBarrier barrier{}; barrier.image = native.image; barrier.subresourceRange = range;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = vk::ImageLayout::eUndefined; barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            upload.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier, d_);
            vk::BufferImageCopy copy{}; copy.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(desc.width, desc.height, 1);
            upload.copyBufferToImage(staging.buffer, native.image, vk::ImageLayout::eTransferDstOptimal, copy, d_);
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal; barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite; barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            upload.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier, d_);
            upload.end(d_);
            textures_.emplace(handle.get(), native);
            cached = true;
            uploads_.push_back({handle.get(), staging, upload});
        }
        catch (...)
        {
            if (cached) textures_.erase(handle.get());
            if (upload) device_.freeCommandBuffers(uploadPool_, upload, d_);
            Destroy(staging); Destroy(native); throw;
        }
        // Ownership now belongs to uploads_ and textures_. Large individual
        // assets use their own batch rather than growing an unbounded queue.
        uploadBytes_ += staging.capacity;
        if (uploadBytes_ >= UploadBudget || uploads_.size() >= UploadJobLimit) FlushUploads();
    }
    void VulkanDrawResources::ClearUploads(bool discardTextures) noexcept
    {
        for (auto& job : uploads_)
        {
            device_.freeCommandBuffers(uploadPool_, job.command, d_);
            Destroy(job.staging);
            if (discardTextures)
                if (auto it = textures_.find(job.texture); it != textures_.end()) {
                    Destroy(it->second); textures_.erase(it);
                }
        }
        uploads_.clear(); uploadBytes_ = 0;
    }
    void VulkanDrawResources::FlushUploads()
    {
        if (uploads_.empty()) return;
        TraceScope trace("Beast.VK.UploadBatch");
        TraceCounter("Beast.VK.UploadTextures", static_cast<std::int64_t>(uploads_.size()));
        TraceCounter("Beast.VK.UploadBytes", static_cast<std::int64_t>(uploadBytes_));
        bool pending = false;
        try
        {
            std::vector<vk::CommandBuffer> buffers;
            buffers.reserve(uploads_.size());
            for (const auto& job : uploads_) buffers.push_back(job.command);
            vk::SubmitInfo submit{}; submit.setCommandBuffers(buffers);
            queue_.submit(submit, nullptr, d_); pending = true;
            {
                TraceScope waitTrace("Beast.VK.UploadWait");
                queue_.waitIdle(d_); pending = false;
            }
        }
        catch (...)
        {
            if (pending) { try { device_.waitIdle(d_); } catch (...) {} }
            // A failed Prepare must not leave cache entries with undefined contents.
            ClearUploads(true); throw;
        }
        ClearUploads(false);
    }
    void VulkanDrawResources::CreatePipeline(const RHIPipelineHandle& handle, vk::RenderPass pass, bool layer)
    {
        auto& cache = layer ? layerPipelines_ : pipelines_;
        if (cache.contains(handle.get())) return;
        vk::ShaderModule vertex, fragment; vk::Pipeline pipeline;
        const auto& desc = handle->GetDesc();
        try
        {
            vk::ShaderModuleCreateInfo module{}; module.setCode(desc.vertexShader->GetDesc().spirv);
            vertex = device_.createShaderModule(module, nullptr, d_);
            module.setCode(desc.fragmentShader->GetDesc().spirv); fragment = device_.createShaderModule(module, nullptr, d_);
            const std::array stages{
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertex, desc.vertexShader->GetDesc().entryPoint.c_str()),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragment, desc.fragmentShader->GetDesc().entryPoint.c_str())};
            static_assert(sizeof(RHIVertex) == 32 && offsetof(RHIVertex, color) == 16);
            const vk::VertexInputBindingDescription binding(0, sizeof(RHIVertex), vk::VertexInputRate::eVertex);
            const std::array attributes{vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, 0),
                vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(RHIVertex, u)),
                vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(RHIVertex, color))};
            vk::PipelineVertexInputStateCreateInfo input{}; input.setVertexBindingDescriptions(binding); input.setVertexAttributeDescriptions(attributes);
            vk::PipelineInputAssemblyStateCreateInfo assembly{}; assembly.topology = vk::PrimitiveTopology::eTriangleList;
            vk::PipelineViewportStateCreateInfo viewport{}; viewport.viewportCount = viewport.scissorCount = 1;
            vk::PipelineRasterizationStateCreateInfo raster{}; raster.polygonMode = vk::PolygonMode::eFill;
            raster.cullMode = vk::CullModeFlagBits::eNone; raster.frontFace = vk::FrontFace::eCounterClockwise; raster.lineWidth = 1;
            vk::PipelineMultisampleStateCreateInfo samples{}; samples.rasterizationSamples = vk::SampleCountFlagBits::e1;
            vk::PipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            attachment.blendEnable = desc.blend != RHIBlend::Opaque;
            attachment.srcColorBlendFactor = desc.blend == RHIBlend::StraightAlpha ? vk::BlendFactor::eSrcAlpha : vk::BlendFactor::eOne;
            attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha; attachment.colorBlendOp = vk::BlendOp::eAdd;
            attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne; attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            attachment.alphaBlendOp = vk::BlendOp::eAdd;
            vk::PipelineColorBlendStateCreateInfo blend{}; blend.setAttachments(attachment);
            const std::array dynamics{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
            vk::PipelineDynamicStateCreateInfo dynamic{}; dynamic.setDynamicStates(dynamics);
            vk::GraphicsPipelineCreateInfo info{}; info.setStages(stages); info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly; info.pViewportState = &viewport; info.pRasterizationState = &raster;
            info.pMultisampleState = &samples; info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
            info.layout = layout_; info.renderPass = pass; info.subpass = 0;
            pipeline = device_.createGraphicsPipeline(nullptr, info, nullptr, d_).value;
            cache.emplace(handle.get(), Pipeline{handle, pipeline});
        }
        catch (...)
        {
            if (pipeline) device_.destroyPipeline(pipeline, nullptr, d_);
            if (fragment) device_.destroyShaderModule(fragment, nullptr, d_);
            if (vertex) device_.destroyShaderModule(vertex, nullptr, d_);
            throw;
        }
        device_.destroyShaderModule(vertex, nullptr, d_); device_.destroyShaderModule(fragment, nullptr, d_);
    }
    void VulkanDrawResources::Prepare(const RHICommandList& commands, vk::RenderPass pass)
    {
        TraceScope trace("Beast.VK.Prepare");
        pendingLayers_.clear();
        for (auto it = layerPipelines_.begin(); it != layerPipelines_.end();)
            if (it->second.owner.expired()) { device_.destroyPipeline(it->second.pipeline, nullptr, d_); it = layerPipelines_.erase(it); } else ++it;
        for (auto it = pipelines_.begin(); it != pipelines_.end();)
            if (it->second.owner.expired()) { device_.destroyPipeline(it->second.pipeline, nullptr, d_); it = pipelines_.erase(it); } else ++it;
        for (auto it = textures_.begin(); it != textures_.end();)
            if (it->second.owner.expired()) {
                auto native = std::move(it->second); it = textures_.erase(it); native.owner.reset();
                if (native.framebuffer) layerPool_.Put(std::move(native), poolBudget_, stats_, [this](auto& t) { Destroy(t); });
                else if (native.recyclable) bitmapPool_.Put(std::move(native), 8ull * 1024 * 1024, bitmapStats_, [this](auto& t) { Destroy(t); });
                else Destroy(native);
            } else ++it;
        Initialize();
        std::vector<const RHIDrawTriangles*> draws;
        std::size_t count = 0;
        const auto append = [&](const RHICommandList& list) {
            for (const auto& command : list.GetCommands())
                if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                    if (draw->vertices.size() > 10000000 - count) throw std::runtime_error("Maximum ten million vertices per frame.");
                    count += draw->vertices.size(); draws.push_back(draw);
                }
        };
        append(commands); // The root keeps its firstVertex = 0 ABI.
        std::unordered_set<const RHITexture*> visited;
        std::uint64_t layerBytes = 0;
        std::function<void(const RHICommandList&, bool)> visit = [&](const RHICommandList& list, bool layer) {
            for (const auto& command : list.GetCommands())
                if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                    CreatePipeline(draw->pipeline, layer ? layerPass_ : pass, layer);
                    const auto& texture = draw->texture;
                    if (!visited.insert(texture.get()).second) continue;
                    if (texture->GetContent()) {
                        layerBytes += std::uint64_t(texture->GetDesc().width) * texture->GetDesc().height * 4;
                        if (layerBytes > 256ull * 1024 * 1024) throw std::runtime_error("Frame layer budget exceeds 256 MiB.");
                    }
                    Upload(texture);
                    if (texture->GetContent() && !textures_.at(texture.get()).rendered) {
                        visit(*texture->GetContent(), true);
                        pendingLayers_.push_back({texture, static_cast<std::uint32_t>(count)});
                        append(*texture->GetContent());
                    }
                }
        };
        try { visit(commands, false); FlushUploads(); }
        catch (...) { ClearUploads(true); pendingLayers_.clear(); throw; }
        TraceCounter("Beast.VK.BitmapAllocations", static_cast<std::int64_t>(bitmapStats_.allocations));
        TraceCounter("Beast.VK.BitmapReuses", static_cast<std::int64_t>(bitmapStats_.reuses));
        if (!count) return;
        const auto bytes = count * sizeof(RHIVertex);
        if (vertices_.capacity < bytes)
        {
            auto next = CreateBuffer(bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            Destroy(vertices_); vertices_ = next;
        }
        auto* mapped = static_cast<std::byte*>(device_.mapMemory(vertices_.memory, 0, bytes, {}, d_));
        for (const auto* draw : draws)
            {
                const auto size = draw->vertices.size() * sizeof(RHIVertex);
                std::memcpy(mapped, draw->vertices.data(), size); mapped += size;
            }
        device_.unmapMemory(vertices_.memory, d_);
    }
    void VulkanDrawResources::Draw(vk::CommandBuffer buffer, const RHIDrawTriangles& draw, std::uint32_t firstVertex, vk::Extent2D extent, bool layer)
    {
        const int width = static_cast<int>(extent.width), height = static_cast<int>(extent.height);
        const auto rect = Clip(draw.scissor.value_or(RHIRect{0, 0, width, height}), width, height);
        if (!rect.width || !rect.height) return;
        buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, (layer ? layerPipelines_ : pipelines_).at(draw.pipeline.get()).pipeline, d_);
        buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout_, 0, textures_.at(draw.texture.get()).set, {}, d_);
        buffer.pushConstants(layout_, vk::ShaderStageFlagBits::eFragment, 0, sizeof(RHIDrawConstants), draw.constants.data(), d_);
        const vk::DeviceSize offset = 0;
        buffer.bindVertexBuffers(0, vertices_.buffer, offset, d_);
        buffer.setViewport(0, vk::Viewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1), d_);
        buffer.setScissor(0, vk::Rect2D(vk::Offset2D(rect.x, rect.y), vk::Extent2D(rect.width, rect.height)), d_);
        buffer.draw(static_cast<std::uint32_t>(draw.vertices.size()), 1, firstVertex, 0, d_);
    }
    void VulkanDrawResources::DrawLayers(vk::CommandBuffer buffer)
    {
        for (const auto& layer : pendingLayers_)
        {
            const auto& desc = layer.texture->GetDesc(); const auto& commands = *layer.texture->GetContent();
            const vk::Extent2D extent(desc.width, desc.height);
            const auto c = commands.GetClearColor();
            const vk::ClearValue clear(vk::ClearColorValue(std::array{c.r, c.g, c.b, c.a}));
            vk::RenderPassBeginInfo begin{}; begin.renderPass = layerPass_; begin.framebuffer = textures_.at(layer.texture.get()).framebuffer;
            begin.renderArea = vk::Rect2D({0, 0}, extent); begin.setClearValues(clear);
            buffer.beginRenderPass(begin, vk::SubpassContents::eInline, d_);
            auto first = layer.firstVertex;
            for (const auto& command : commands.GetCommands())
                if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                    Draw(buffer, *draw, first, extent, true); first += static_cast<std::uint32_t>(draw->vertices.size());
                } else {
                    const auto& value = std::get<RHIClearRect>(command);
                    const auto rect = Clip(value.rect, static_cast<int>(desc.width), static_cast<int>(desc.height));
                    if (!rect.width || !rect.height) continue;
                    vk::ClearAttachment attachment{}; attachment.aspectMask = vk::ImageAspectFlagBits::eColor;
                    attachment.clearValue = vk::ClearValue(vk::ClearColorValue(std::array{value.color.r, value.color.g, value.color.b, value.color.a}));
                    const vk::ClearRect region(vk::Rect2D({rect.x, rect.y}, {static_cast<std::uint32_t>(rect.width), static_cast<std::uint32_t>(rect.height)}), 0, 1);
                    buffer.clearAttachments(attachment, region, d_);
                }
            buffer.endRenderPass(d_);
        }
    }
    void VulkanDrawResources::CommitLayers()
    {
        stats_.renders += pendingLayers_.size();
        for (const auto& layer : pendingLayers_) textures_.at(layer.texture.get()).rendered = true;
        pendingLayers_.clear();
    }
    void VulkanDrawResources::InvalidatePipelines() noexcept
    {
        for (const auto& item : layerPipelines_) device_.destroyPipeline(item.second.pipeline, nullptr, d_);
        layerPipelines_.clear();
        for (const auto& item : pipelines_) device_.destroyPipeline(item.second.pipeline, nullptr, d_);
        pipelines_.clear();
    }
    void VulkanDrawResources::Release() noexcept
    {
        ClearUploads(true);
        pendingLayers_.clear();
        InvalidatePipelines();
        for (auto& item : textures_) Destroy(item.second);
        layerPool_.Clear([this](auto& t) { Destroy(t); }); stats_ = {};
        bitmapPool_.Clear([this](auto& t) { Destroy(t); }); bitmapStats_ = {};
        textures_.clear(); Destroy(vertices_);
        if (layerPass_) device_.destroyRenderPass(layerPass_, nullptr, d_);
        layerPass_ = nullptr;
        if (layout_) device_.destroyPipelineLayout(layout_, nullptr, d_);
        if (setLayout_) device_.destroyDescriptorSetLayout(setLayout_, nullptr, d_);
        if (uploadPool_) device_.destroyCommandPool(uploadPool_, nullptr, d_);
        layout_ = nullptr; setLayout_ = nullptr; uploadPool_ = nullptr;
    }
}
