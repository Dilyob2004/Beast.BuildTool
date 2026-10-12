#pragma once
#include <algorithm>
#include <cmath>

namespace Beast::UI::Detail
{
    // Sample content positions (after rubber resistance), not raw finger speed.
    // Bounded prediction fills a late input sample; it never drives a released drag.
    class ScrollSampler
    {
        float before_ = 0, latest_ = 0;
        double beforeTime_ = 0, latestTime_ = 0;
    public:
        void Reset() { beforeTime_ = latestTime_ = 0; }
        bool HasValue() const { return latestTime_ > 0; }
        float Latest() const { return latest_; }
        void Push(float offset, double time)
        {
            if (!std::isfinite(offset) || !std::isfinite(time) || time <= 0) { Reset(); return; }
            if (time <= latestTime_ || time - latestTime_ > .1) beforeTime_ = 0;
            else { before_ = latest_; beforeTime_ = latestTime_; }
            latest_ = offset; latestTime_ = time;
        }
        float Sample(double frameTime) const
        {
            const double span = latestTime_ - beforeTime_;
            if (!std::isfinite(frameTime) || !beforeTime_ || span < .001 || span > .1) return latest_;
            // A small input delay allows interpolation when a sample is already
            // newer than the display timestamp. Prediction is at most 8 ms / 8 dp.
            const double target = std::clamp(frameTime - .008, beforeTime_, latestTime_ + std::min(.008, span * .5));
            const double velocity = (double(latest_) - before_) / span;
            const double delta = velocity * (target - latestTime_);
            return static_cast<float>(double(latest_) + (target > latestTime_ ? std::clamp(delta, -8.0, 8.0) : delta));
        }
    };
}
