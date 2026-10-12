#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Beast::UI
{
    enum class FontFace { Regular, Bold, Icons };
    struct Size { float width = 0, height = 0; };
    struct Point { float x = 0, y = 0; };
    struct Rect
    {
        float x = 0, y = 0, width = 0, height = 0;
        bool Contains(Point p) const noexcept { return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height; }
    };
    struct Constraints
    {
        float minWidth = 0, maxWidth = std::numeric_limits<float>::infinity();
        float minHeight = 0, maxHeight = std::numeric_limits<float>::infinity();
        void Validate() const
        {
            if (!std::isfinite(minWidth) || !std::isfinite(minHeight) || minWidth < 0 || minHeight < 0 ||
                std::isnan(maxWidth) || std::isnan(maxHeight) || maxWidth < minWidth || maxHeight < minHeight)
                throw std::invalid_argument("Invalid UI constraints.");
        }
        Size Constrain(Size size) const
        {
            Validate();
            if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 0 || size.height < 0)
                throw std::invalid_argument("Invalid UI size.");
            return {std::clamp(size.width, minWidth, maxWidth), std::clamp(size.height, minHeight, maxHeight)};
        }
        static Constraints Tight(Size size) { return {size.width, size.width, size.height, size.height}; }
        static Constraints Loose(Size size) { return {0, size.width, 0, size.height}; }
    };
    inline Rect Unite(Rect a, Rect b) noexcept
    {
        const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
        return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
    }
    inline Rect Intersect(Rect a, Rect b) noexcept
    {
        const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
        return {x, y, std::max(0.0f, std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0.0f, std::min(a.y + a.height, b.y + b.height) - y)};
    }
}
