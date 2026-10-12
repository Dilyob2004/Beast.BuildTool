#pragma once
#include "UI/Geometry.h"
#include <array>
#include <bit>
#include <functional>
#include <string>
#include <string_view>

namespace Beast::UI::Detail
{
    // Metrics of the immutable embedded fonts depend on exact size/face/text,
    // not position, clipping or scroll phase. Four ways bound lookup/eviction
    // work even for streams of unique strings. Long text takes the uncached path.
    class TextMetricsCache
    {
        struct Entry {
            std::string text;
            Size value;
            std::uint32_t size = 0;
            FontFace face = FontFace::Regular;
            std::uint64_t used = 0;
        };
        std::array<std::array<Entry, 4>, 128> sets_;
        std::uint64_t serial_ = 0;
    public:
        struct Stats { std::uint64_t hits = 0, misses = 0, bypasses = 0; };
    private:
        Stats stats_;
    public:
        Stats GetStats() const { return stats_; }
        template<class Measure> Size Resolve(const std::string& text, float size, FontFace face, Measure measure)
        {
            if (text.size() > 512) { ++stats_.bypasses; return measure(); }
            const auto bits = std::bit_cast<std::uint32_t>(size);
            const auto hash = std::hash<std::string_view>{}(text) ^ (std::size_t(bits) * 0x9e3779b1u) ^ std::size_t(face);
            auto& set = sets_[hash % sets_.size()];
            Entry* oldest = &set.front();
            for (auto& entry : set) {
                if (entry.used && entry.size == bits && entry.face == face && entry.text == text) {
                    ++stats_.hits; entry.used = ++serial_; return entry.value;
                }
                if (entry.used < oldest->used) oldest = &entry;
            }
            ++stats_.misses;
            const auto value = measure(); // A failed calculation never poisons an entry.
            oldest->text = text; oldest->value = value; oldest->size = bits;
            oldest->face = face; oldest->used = ++serial_;
            return value;
        }
    };
}
