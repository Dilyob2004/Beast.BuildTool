#pragma once
#include <cstdint>
#include <optional>

namespace Beast
{
    struct RHILayerStats
    {
        // Cumulative counters, scoped to one RHI/context generation.
        std::uint64_t allocations = 0, reuses = 0, renders = 0, evictions = 0;
        std::uint64_t pooledBytes = 0, pooledTargets = 0;
    };
    struct RHIFrameStats
    {
        std::uint64_t submittedFrames = 0, gpuFrame = 0;
        // Most recent Submit CPU wall time, including waits/presentation, not GPU time.
        double submitCpuMs = 0;
        // Delayed timestamp result for gpuFrame. No blocking readback; null means
        // unavailable/disabled/disjoint, never a measured zero.
        std::optional<double> gpuMs;
        RHILayerStats layers;
    };
}
