#pragma once
#include <algorithm>
#include <cstdint>

namespace Beast::Detail
{
    // Choreographer's monotonic display timestamp, not event-loop wall time.
    class FrameClock
    {
        std::int64_t previous_ = 0;
    public:
        void Reset() { previous_ = 0; }
        double Step(std::int64_t timestamp)
        {
            if (timestamp <= 0 || timestamp <= previous_) return 0;
            const double delta = previous_ ? double(timestamp - previous_) * 1e-9 : 0;
            previous_ = timestamp;
            return std::min(delta, .1);
        }
    };
}
