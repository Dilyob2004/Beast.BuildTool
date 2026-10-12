#include "UI/Private/GlyphAtlas.h"
#include "Core/Trace.h"
#include <bit>
#include <chrono>

namespace Beast::UI::Detail
{
    std::size_t GlyphAtlas::Hash::operator()(const Key& k) const noexcept
    {
        std::size_t hash = k.size;
        for (auto value : {static_cast<unsigned>(k.glyph), static_cast<unsigned>(k.face), unsigned(k.x), unsigned(k.y)})
            hash ^= value + std::size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
        return hash;
    }
    void GlyphAtlas::SetBudget(std::uint64_t bytes)
    {
        if (bytes > 64ull * 1024 * 1024) throw std::invalid_argument("Glyph atlas budget exceeds 64 MiB.");
        EndFrame(); budget_ = bytes; TrimPages(); ResetWarmup();
    }
    void GlyphAtlas::BeginFrame()
    {
        // A previous interrupted Canvas paint may have left builders open.
        // Seal them before accepting another frame, never change old commands.
        EndFrame(); ++serial_; newBytes_ = 0;
    }
    void GlyphAtlas::EndFrame()
    {
        for (auto& page : pages_) if (!page->builder->IsSealed()) { page->builder->Seal(); stats_.sealedBytes += page->Bytes(); }
        TrimPages();
    }
    void GlyphAtlas::TrimPages()
    {
        // Prepared runs contain non-owning page pointers. Drop their mappings
        // before any page eviction; immutable RHI handles in old frames survive.
        if (stats_.pageBytes > budget_ || pages_.size() > 64)
        {
            // Don't keep speculative work alive under actual cache pressure.
            warmRuns_.clear();
            for (auto& item : runs_) { item.second.resolved = false; item.second.placed.clear(); }
        }
        while (stats_.pageBytes > budget_ || pages_.size() > 64)
        {
            const auto oldest = std::min_element(pages_.begin(), pages_.end(), [](const auto& a, const auto& b) { return a->used < b->used; });
            if (oldest == pages_.end()) break;
            const auto* page = oldest->get();
            for (auto it = glyphs_.begin(); it != glyphs_.end();) if (it->second.page == page) it = glyphs_.erase(it); else ++it;
            stats_.pageBytes -= page->Bytes(); pages_.erase(oldest); ++stats_.evictions;
        }
        stats_.pages = pages_.size(); stats_.glyphs = glyphs_.size();
    }
    void GlyphAtlas::TrimRuns(std::uint64_t incoming)
    {
        while (!runs_.empty() && (stats_.runBytes + incoming > 2ull * 1024 * 1024 || runs_.size() >= 512))
        {
            auto oldest = std::min_element(runs_.begin(), runs_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            stats_.runBytes -= oldest->second.bytes; runs_.erase(oldest);
        }
    }
    GlyphAtlas::CachedRun& GlyphAtlas::Run(const std::string& text, float size, Point phase, FontFace face)
    {
        const auto key = std::to_string(std::bit_cast<std::uint32_t>(size)) + "/" + std::to_string(static_cast<int>(face)) + "/" +
            std::to_string(std::bit_cast<std::uint32_t>(phase.x)) + "/" + std::to_string(std::bit_cast<std::uint32_t>(phase.y)) + "/" + text;
        if (const auto found = runs_.find(key); found != runs_.end()) { ++stats_.runHits; found->second.used = serial_; return found->second; }
        ++stats_.runMisses;
        auto run = LayoutGlyphs(text, size, phase, face);
        std::vector<Placed> placed; placed.reserve(run.glyphs.size());
        const std::uint64_t bytes = sizeof(CachedRun) + key.size() + run.glyphs.capacity() * sizeof(PositionedGlyph) + placed.capacity() * sizeof(Placed);
        TrimRuns(bytes);
        const auto inserted = runs_.emplace(key, CachedRun{std::move(run), serial_, bytes, std::move(placed)});
        stats_.runBytes += bytes; stats_.runs = runs_.size(); return inserted.first->second;
    }
    void GlyphAtlas::QueueWarm(const std::string& text, float size, Point phase, FontFace face, Axis axis)
    {
        // Small visible/nearby UI labels, bounded independently of atlas/run caches.
        if (!budget_ || text.size() > 128 || size > 96 || warmKeys_.size() >= 256 || warmRuns_.size() >= 128) return;
        const float fixed = axis == Axis::Vertical ? phase.x : phase.y;
        const auto key = std::to_string(std::bit_cast<std::uint32_t>(size)) + "/" + std::to_string(int(face)) + "/" +
            std::to_string(int(axis)) + "/" + std::to_string(std::bit_cast<std::uint32_t>(fixed)) + "/" + text;
        if (!warmKeys_.insert(key).second) return;
        warmRuns_.push_back({text, size, phase, face, axis});
    }
    bool GlyphAtlas::WarmStep(std::vector<RHITextureHandle>& changedPages)
    {
        changedPages.clear();
        if (warmRuns_.empty() || !budget_) return false;
        Beast::Detail::TraceScope trace("Beast.Text.Warmup");
        BeginFrame();
        if (warmRuns_.empty()) return false;
        WarmBudget warm{std::chrono::steady_clock::now() + std::chrono::microseconds(750)};
        std::vector<Quad> quads;
        do {
            auto& request = warmRuns_.front();
            auto phase = request.phase;
            (request.axis == Axis::Vertical ? phase.y : phase.x) = request.next / 8.f;
            // Same rasterizer and coverage as the visible path, never resampling.
            ResolveImpl(request.text, request.size, phase, request.face, false, quads, &warm);
            // Resume this phase next idle frame; already packed glyphs survive.
            if (warm.deferred) break;
            if (++request.next == 8) warmRuns_.pop_front();
        } while (!warmRuns_.empty() && newBytes_ < 512 * 1024 && std::chrono::steady_clock::now() < warm.deadline);
        for (const auto& page : pages_) if (!page->builder->IsSealed()) changedPages.push_back(page->builder->Handle());
        EndFrame();
        return !changedPages.empty();
    }
    bool GlyphAtlas::Resolve(const std::string& text, float size, Point phase, FontFace face, bool translucent, std::vector<Quad>& output)
    { return ResolveImpl(text, size, phase, face, translucent, output, nullptr); }
    bool GlyphAtlas::ResolveImpl(const std::string& text, float size, Point phase, FontFace face, bool translucent, std::vector<Quad>& output, WarmBudget* warm)
    {
        Beast::Detail::TraceScope trace("Beast.Text.Resolve");
        output.clear();
        const auto fallback = [&] { output.clear(); ++stats_.fallbackLines; return false; };
        const auto defer = [&] { output.clear(); warm->deferred = true; return false; };
        if (!budget_) return fallback();
        const auto stagingLimit = std::min<std::uint64_t>(budget_, warm ? 512 * 1024 : 16 * 1024 * 1024);
        auto& cached = Run(text, size, phase, face); const auto& run = cached.run;
        if (run.oversized || (translucent && run.overlaps)) return fallback();
        output.reserve(run.glyphs.size());
        const auto emit = [&] {
            for (const auto& p : cached.placed) {
                p.page->used = serial_; output.push_back({p.page->builder->Handle(), p.pixels, p.uv});
            }
        };
        if (cached.resolved) { stats_.glyphHits += run.glyphs.size(); emit(); return true; }
        cached.placed.clear();
        for (const auto& glyph : run.glyphs)
        {
            const Key key{std::bit_cast<std::uint32_t>(size), glyph.index, glyph.font, glyph.phaseX, glyph.phaseY};
            auto found = glyphs_.find(key);
            if (found == glyphs_.end())
            {
                // The deadline is cooperative: a single layout/rasterizer call
                // may exceed it, but the rest of the line waits for another frame.
                if (warm && std::chrono::steady_clock::now() >= warm->deadline) return defer();
                ++stats_.glyphMisses;
                Page* selected = nullptr; int x = 0, y = 0, row = 0;
                for (auto& page : pages_)
                {
                    if (page->builder->IsSealed() && newBytes_ + page->Bytes() > stagingLimit) continue;
                    x = page->x; y = page->y; row = page->row;
                    if (x + glyph.width + 1 > page->width) { x = 1; y += row + 1; row = 0; }
                    if (x + glyph.width + 1 <= page->width && y + glyph.height + 1 <= page->height) { selected = page.get(); break; }
                }
                if (!selected)
                {
                    const auto dimension = [](int needed) { int side = 256; while (side < needed + 2) side *= 2; return side; };
                    const int width = dimension(glyph.width), height = dimension(glyph.height);
                    const auto bytes = std::uint64_t(width) * height * 4;
                    // Bound new staging data in one frame as well as retained data.
                    if (bytes > stagingLimit) return fallback();
                    if (newBytes_ + bytes > stagingLimit) return warm ? defer() : fallback();
                    auto page = std::make_unique<Page>(width, height); selected = page.get(); pages_.push_back(std::move(page));
                    stats_.pageBytes += bytes; newBytes_ += bytes; ++stats_.pagesCreated;
                    x = y = 1; row = 0;
                }
                auto coverage = RasterizeGlyph(glyph, size);
                if (selected->builder->IsSealed())
                {
                    // Copy-on-write generation: old command lists/layer snapshots
                    // keep their immutable page. All future glyph lookups use
                    // the new generation, preserving UV positions and packing.
                    selected->builder = std::make_unique<Beast::Detail::TextureBuilder>(selected->builder->Handle());
                    newBytes_ += selected->Bytes(); ++stats_.pagesCreated;
                }
                selected->builder->WriteCoverage(static_cast<unsigned>(x), static_cast<unsigned>(y), static_cast<unsigned>(glyph.width), static_cast<unsigned>(glyph.height), coverage);
                selected->x = x + glyph.width + 1; selected->y = y; selected->row = std::max(row, glyph.height);
                found = glyphs_.emplace(key, Entry{selected, x, y}).first;
            }
            else ++stats_.glyphHits;
            auto& entry = found->second; entry.page->used = serial_;
            // Preserve the old line box, including clipped negative bearings.
            const int left = std::max(0, glyph.x), top = std::max(0, glyph.y);
            const int right = std::min(run.width, glyph.x + glyph.width), bottom = std::min(run.height, glyph.y + glyph.height);
            if (right <= left || bottom <= top) continue;
            const float u = float(entry.x + left - glyph.x) / entry.page->width, v = float(entry.y + top - glyph.y) / entry.page->height;
            cached.placed.push_back({entry.page, {float(left), float(top), float(right - left), float(bottom - top)},
                {u, v, float(right - left) / entry.page->width, float(bottom - top) / entry.page->height}});
        }
        // A later glyph in the same run may have cloned an earlier glyph's page.
        // Resolve handles after packing, so a line uses one generation per page.
        cached.resolved = true; emit();
        stats_.pages = pages_.size(); stats_.glyphs = glyphs_.size(); return true;
    }
}
