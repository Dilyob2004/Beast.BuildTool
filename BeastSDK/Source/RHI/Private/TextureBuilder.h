#pragma once
#include "RHI/RHIResources.h"
#include <span>
#include <stdexcept>
#include <algorithm>

namespace Beast::Detail
{
    // Internal single-threaded staging resource. Recording can refer to Handle,
    // but submission is rejected until Seal. No writes are possible afterwards.
    // Used to assemble one immutable atlas page across a whole Canvas frame.
    class TextureBuilder
    {
        std::shared_ptr<RHITexture> texture_;
    public:
        TextureBuilder(std::uint32_t width, std::uint32_t height)
        {
            if (!width || !height || width > 4096 || height > 4096) throw std::invalid_argument("Invalid staging texture size.");
            RHITextureDesc desc{width, height, std::vector<std::uint8_t>(std::size_t(width) * height * 4), RHIFilter::Nearest};
            texture_.reset(new RHITexture(std::move(desc))); texture_->ready_ = false;
        }
        TextureBuilder(const TextureBuilder&) = delete;
        explicit TextureBuilder(const RHITextureHandle& source)
        {
            if (!source || !source->IsReady() || source->GetContent()) throw std::invalid_argument("Clone requires a sealed bitmap texture.");
            texture_.reset(new RHITexture(source->GetDesc())); texture_->ready_ = false;
        }
        TextureBuilder& operator=(const TextureBuilder&) = delete;
        RHITextureHandle Handle() const { return texture_; }
        bool IsSealed() const noexcept { return texture_->ready_; }
        void WriteCoverage(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> coverage)
        {
            auto& d = texture_->desc_;
            if (IsSealed()) throw std::logic_error("Cannot modify a sealed texture.");
            if (x > d.width || y > d.height || width > d.width - x || height > d.height - y || coverage.size() != std::size_t(width) * height)
                throw std::invalid_argument("Invalid staging texture region.");
            for (std::uint32_t row = 0; row < height; ++row) for (std::uint32_t col = 0; col < width; ++col)
            {
                auto* dst = &d.pixels[(std::size_t(y + row) * d.width + x + col) * 4];
                std::fill_n(dst, 4, coverage[std::size_t(row) * width + col]);
            }
        }
        void Seal() noexcept { texture_->ready_ = true; }
    };
}
