#pragma once
#include "RHI/RHIDiagnostics.h"
#include <algorithm>
#include <vector>
#include <utility>

namespace Beast::Detail
{
    // Native resources are moved explicitly; T has no destructor that frees GPU
    // handles. Owner must Clear while its device is alive, or Abandon on loss.
    // Put/Take only after the prior frame fence; live snapshots never enter here.
    template<class T> class LayerTargetPool
    {
        std::vector<T> idle_; // Oldest recycled entry first; exact RGBA8 size match.
        std::uint64_t bytes_ = 0;
    public:
        static constexpr std::size_t MaxTargets = 16;
        static constexpr std::uint64_t MaxBytes = 64ull * 1024 * 1024;
        bool Take(std::uint32_t width, std::uint32_t height, T& result)
        {
            auto it = std::find_if(idle_.begin(), idle_.end(), [&](const T& t) { return t.width == width && t.height == height; });
            if (it == idle_.end()) return false;
            bytes_ -= it->bytes; result = std::move(*it); idle_.erase(it); return true;
        }
        template<class Destroy> void Put(T value, std::uint64_t budget, RHILayerStats& stats, Destroy destroy)
        {
            budget = std::min(budget, MaxBytes);
            if (!budget || value.bytes > budget) { destroy(value); ++stats.evictions; return; }
            while (!idle_.empty() && (idle_.size() >= MaxTargets || bytes_ + value.bytes > budget)) {
                bytes_ -= idle_.front().bytes; destroy(idle_.front()); idle_.erase(idle_.begin()); ++stats.evictions;
            }
            const auto bytes = value.bytes;
            try { idle_.push_back(std::move(value)); bytes_ += bytes; }
            catch (...) { destroy(value); throw; }
        }
        template<class Destroy> void Clear(Destroy destroy) noexcept { for (auto& t : idle_) destroy(t); Abandon(); }
        void Abandon() noexcept { idle_.clear(); bytes_ = 0; }
        void ReadStats(RHILayerStats& stats) const noexcept { stats.pooledBytes = bytes_; stats.pooledTargets = idle_.size(); }
    };
}
