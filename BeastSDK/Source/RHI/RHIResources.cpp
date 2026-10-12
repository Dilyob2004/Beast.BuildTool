#include "RHI/RHI.h"
#include <cstring>
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace Beast
{
    RHIShaderHandle RHI::CreateShader(RHIShaderDesc desc)
    {
        if (desc.name.empty() || desc.entryPoint.empty() || desc.glsl460.empty() || desc.glslES300.empty())
            throw std::invalid_argument("A shader requires its name, entry point and both GLSL targets.");
        if (desc.stage != RHIShaderStage::Vertex && desc.stage != RHIShaderStage::Fragment)
            throw std::invalid_argument("Unsupported shader stage.");
        if (desc.stage == RHIShaderStage::Fragment && desc.combinedSampler.empty())
            throw std::invalid_argument("The fragment shader requires one combined GLSL sampler.");
        const auto& words = desc.spirv;
        if (words.size() < 5 || words[0] != 0x07230203 || words[1] != 0x00010000 || !words[3] || words[4])
            throw std::invalid_argument("Expected a validated SPIR-V 1.0 shader package from Beast BuildTool.");
        bool entryFound = false, inFunction = false, functionFound = false;
        for (std::size_t offset = 5; offset < words.size();)
        {
            const std::size_t count = words[offset] >> 16;
            if (!count || count > words.size() - offset) throw std::invalid_argument("Truncated SPIR-V instruction.");
            const auto opcode = words[offset] & 0xffff;
            if (opcode == 54) // OpFunction
            {
                if (inFunction || count != 5) throw std::invalid_argument("Malformed SPIR-V function.");
                inFunction = functionFound = true;
            }
            if (opcode == 56) // OpFunctionEnd
            {
                if (!inFunction || count != 1) throw std::invalid_argument("Malformed SPIR-V function end.");
                inFunction = false;
            }
            if ((words[offset] & 0xffff) == 15 && count >= 4) // OpEntryPoint
            {
                const char* name = reinterpret_cast<const char*>(&words[offset + 3]);
                const std::size_t bytes = (count - 3) * sizeof(std::uint32_t);
                const auto* end = static_cast<const char*>(std::memchr(name, 0, bytes));
                if (!end) throw std::invalid_argument("Unterminated SPIR-V entry point.");
                const std::uint32_t model = desc.stage == RHIShaderStage::Vertex ? 0u : 4u;
                entryFound |= words[offset + 1] == model && std::string(name, end) == desc.entryPoint;
            }
            offset += count;
        }
        if (inFunction || !functionFound) throw std::invalid_argument("Truncated SPIR-V function body.");
        if (!entryFound) throw std::invalid_argument("Shader entry point/stage does not match SPIR-V.");
        return RHIShaderHandle(new RHIShader(std::move(desc)));
    }
    RHITextureHandle RHI::CreateTexture(RHITextureDesc desc)
    {
        const std::uint64_t bytes = std::uint64_t(desc.width) * desc.height * 4;
        // Vulkan/GLES baseline minimum; actual driver limits are checked on upload.
        if (!desc.width || !desc.height || desc.width > 4096 || desc.height > 4096 || bytes != desc.pixels.size())
            throw std::invalid_argument("RGBA8 textures require 1..4096 dimensions and exactly width*height*4 bytes.");
        if (desc.filter != RHIFilter::Nearest && desc.filter != RHIFilter::Linear)
            throw std::invalid_argument("Unsupported texture filter.");
        return RHITextureHandle(new RHITexture(std::move(desc)));
    }
    RHITextureHandle RHI::CreateLayerTexture(std::uint32_t width, std::uint32_t height, RHICommandList content)
    {
        if (!width || !height || width > 4096 || height > 4096 || !content.IsReady())
            throw std::invalid_argument("Layer requires a closed pass and dimensions 1..4096.");
        const auto clear = content.GetClearColor();
        if (clear.r > clear.a || clear.g > clear.a || clear.b > clear.a)
            throw std::invalid_argument("Layer clear color must be premultiplied.");
        unsigned depth = 1;
        std::uint64_t bytes = std::uint64_t(width) * height * 4;
        for (const auto& command : content.GetCommands())
            if (const auto* draw = std::get_if<RHIDrawTriangles>(&command)) {
                depth = std::max(depth, 1 + draw->texture->GetLayerDepth());
                bytes += draw->texture->GetLayerBytes();
                if (depth > 8 || bytes > 256ull * 1024 * 1024) throw std::invalid_argument("Layer tree exceeds 8 levels or 256 MiB.");
            } else {
                const auto c = std::get<RHIClearRect>(command).color;
                if (c.r > c.a || c.g > c.a || c.b > c.a) throw std::invalid_argument("Layer rectangle clear color must be premultiplied.");
            }
        return RHITextureHandle(new RHITexture({width, height, {}, RHIFilter::Nearest},
            std::make_shared<const RHICommandList>(std::move(content)), depth, bytes));
    }
    RHIPipelineHandle RHI::CreatePipeline(RHIPipelineDesc desc)
    {
        if (!desc.vertexShader || !desc.fragmentShader ||
            desc.vertexShader->GetDesc().stage != RHIShaderStage::Vertex ||
            desc.fragmentShader->GetDesc().stage != RHIShaderStage::Fragment)
            throw std::invalid_argument("A graphics pipeline requires vertex and fragment shaders of matching stages.");
        if (desc.blend != RHIBlend::Opaque && desc.blend != RHIBlend::StraightAlpha && desc.blend != RHIBlend::PremultipliedAlpha)
            throw std::invalid_argument("Unsupported blend mode.");
        return RHIPipelineHandle(new RHIPipeline(std::move(desc)));
    }
}
