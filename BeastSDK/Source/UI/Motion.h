#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Beast::UI
{
    // Finite ease-out transitions: independent of refresh rate, interruptible
    // from the current value, with an exact endpoint (no perpetual repaint).
    class MotionValue
    {
        float value_ = 0, from_ = 0, target_ = 0, elapsed_ = 0, duration_ = 0;
    public:
        float Get() const noexcept { return value_; }
        void Set(float value)
        {
            if (!std::isfinite(value)) throw std::invalid_argument("Invalid motion value.");
            value_ = from_ = target_ = value; elapsed_ = duration_ = 0;
        }
        void AnimateTo(float target, float duration = .18f)
        {
            if (!std::isfinite(target) || !std::isfinite(duration) || duration < 0)
                throw std::invalid_argument("Invalid motion target/duration.");
            if (duration == 0) { Set(target); return; }
            if (target == target_) return;
            from_ = value_; target_ = target; elapsed_ = 0; duration_ = duration;
        }
        bool Tick(float seconds)
        {
            if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid motion delta.");
            if (value_ == target_) return false;
            const float before = value_;
            elapsed_ = std::min(duration_, elapsed_ + seconds);
            const float t = elapsed_ / duration_, remaining = 1 - t;
            value_ = elapsed_ == duration_ ? target_ : from_ + (target_ - from_) * (1 - remaining * remaining * remaining);
            return value_ != before;
        }
    };
}
