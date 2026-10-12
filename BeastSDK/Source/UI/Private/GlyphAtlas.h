#pragma once
#include "UI/UI.h"
#include "UI/Private/Font.h"
#include "RHI/Private/TextureBuilder.h"
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <chrono>

namespace Beast::UI::Detail
{
    class GlyphAtlas
    {
    public:
        struct Quad { RHITextureHandle texture; Rect pixels, uv; };
    private:
        struct Key
        {
            std::uint32_t size;
            int glyph;
            FontId face;
            std::uint16_t x, y;
            bool operator==(const Key&) const = default;
        };
        struct Hash { std::size_t operator()(const Key&) const noexcept; };
        struct Page
        {
            std::unique_ptr<Beast::Detail::TextureBuilder> builder;
            int width, height, x = 1, y = 1, row = 0;
            std::uint64_t used = 0;
            Page(int w, int h) : builder(std::make_unique<Beast::Detail::TextureBuilder>(static_cast<unsigned>(w), static_cast<unsigned>(h))), width(w), height(h) {}
            std::uint64_t Bytes() const { return std::uint64_t(width) * height * 4; }
        };
        struct Entry { Page* page; int x, y; };
        struct Placed { Page* page; Rect pixels, uv; };
        struct CachedRun { GlyphRun run; std::uint64_t used, bytes; std::vector<Placed> placed; bool resolved = false; };
        std::vector<std::unique_ptr<Page>> pages_;
        std::unordered_map<Key, Entry, Hash> glyphs_;
        std::unordered_map<std::string, CachedRun> runs_;
        TextCacheStats stats_;
        std::uint64_t budget_ = 16ull * 1024 * 1024, serial_ = 0, newBytes_ = 0;
        struct WarmRun { std::string text; float size; Point phase; FontFace face; Axis axis; unsigned next = 0; };
        std::deque<WarmRun> warmRuns_;
        std::unordered_set<std::string> warmKeys_;
        void TrimPages();
        void TrimRuns(std::uint64_t incoming);
        CachedRun& Run(const std::string& text, float size, Point phase, FontFace face);
        struct WarmBudget { std::chrono::steady_clock::time_point deadline; bool deferred = false; };
        bool ResolveImpl(const std::string& text, float size, Point phase, FontFace face, bool translucent, std::vector<Quad>& output, WarmBudget* warm);
    public:
        void BeginFrame();
        void EndFrame();
        void SetBudget(std::uint64_t bytes);
        TextCacheStats GetStats() const { return stats_; }
        void QueueWarm(const std::string& text, float size, Point phase, FontFace face, Axis axis);
        bool WarmStep(std::vector<RHITextureHandle>& changedPages);
        void ResetWarmup() { warmRuns_.clear(); warmKeys_.clear(); }
        // False requests the exact legacy line path (oversized/overlapping
        // translucent text or exhausted frame budget). No partial draw is emitted.
        bool Resolve(const std::string& text, float size, Point phase, FontFace face, bool translucent, std::vector<Quad>& output);
    };
}
