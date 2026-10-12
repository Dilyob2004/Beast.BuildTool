#pragma once
#include "Core/Window.h"
#include <algorithm>
#include <cstdint>

namespace Beast::Detail
{
    // Matches GameActivity's atomic left/top/right/bottom physical-pixel snapshot.
    inline void ApplySafeInsets(Window& window, std::uint64_t margins)
    {
        window.contentX = std::clamp(static_cast<int>(margins & 65535), 0, window.width);
        window.contentY = std::clamp(static_cast<int>((margins >> 16) & 65535), 0, window.height);
        window.contentWidth = std::max(0, window.width - window.contentX - static_cast<int>((margins >> 32) & 65535));
        window.contentHeight = std::max(0, window.height - window.contentY - static_cast<int>((margins >> 48) & 65535));
    }
}
