#include "UI/Canvas.h"
#include <unordered_set>

namespace Beast::UI
{
    RepaintBoundary::RepaintBoundary(WidgetPtr child) { Adopt(std::move(child)); }
    Size RepaintBoundary::OnMeasure(const Constraints& c) const { return Children().front()->Measure(c); }
    void RepaintBoundary::OnLayout() { Children().front()->Layout(GetBounds()); }
    void RepaintBoundary::OnPaint(Canvas& canvas) const { canvas.PaintCached(*this); }
    Rect RepaintBoundary::GetPaintBounds() const
    {
        const auto r = GetBounds();
        if (boundsRevision_ != paintRevision_)
        {
            relativePaintBounds_ = Widget::GetPaintBounds();
            relativePaintBounds_.x -= r.x; relativePaintBounds_.y -= r.y;
            boundsRevision_ = paintRevision_;
        }
        auto result = relativePaintBounds_; result.x += r.x; result.y += r.y; return result;
    }
    void Canvas::SetRepaintCacheBudget(std::uint64_t bytes)
    {
        if (bytes > 64ull * 1024 * 1024) throw std::invalid_argument("Repaint cache budget exceeds 64 MiB.");
        repaintBudget_ = bytes; TrimRepaintCache();
    }
    void Canvas::TrimRepaintCache()
    {
        for (auto it = subtrees_.begin(); it != subtrees_.end();)
        {
            if (it->second.owner.expired()) { repaintStats_.cachedBytes -= it->second.bytes; it = subtrees_.erase(it); }
            else ++it;
        }
        while (repaintStats_.cachedBytes > repaintBudget_ || subtrees_.size() > 64)
        {
            auto oldest = subtrees_.end();
            for (auto it = subtrees_.begin(); it != subtrees_.end(); ++it)
                if (oldest == subtrees_.end() || it->second.used < oldest->second.used) oldest = it;
            if (oldest == subtrees_.end()) break;
            repaintStats_.cachedBytes -= oldest->second.bytes; subtrees_.erase(oldest); ++repaintStats_.evictions;
        }
        repaintStats_.entries = subtrees_.size();
    }
    namespace
    {
        // Charge reachable bitmap data, render targets and CPU display-list
        // storage. Shared resources are deliberately charged again across entries.
        std::uint64_t SnapshotBytes(const RHITextureHandle& root)
        {
            std::unordered_set<const RHITexture*> visited;
            const auto count = [&](const auto& self, const RHITexture* texture) -> std::uint64_t {
                if (!visited.insert(texture).second) return 0;
                const auto& desc = texture->GetDesc();
                std::uint64_t bytes = std::uint64_t(desc.width) * desc.height * 4;
                if (const auto* list = texture->GetContent()) for (const auto& command : list->GetCommands())
                {
                    bytes += sizeof(RHICommand);
                    if (const auto* draw = std::get_if<RHIDrawTriangles>(&command))
                        bytes += draw->vertices.size() * sizeof(RHIVertex) + self(self, draw->texture.get());
                }
                return bytes;
            };
            return count(count, root.get());
        }
    }
    void Canvas::PaintCached(const RepaintBoundary& widget)
    {
        const auto paint = [&] { widget.Widget::OnPaint(*this); };
        // Only the outer boundary records a raster snapshot: inner boundaries
        // must not pin an unbounded graph of other cache entries. At an existing
        // layer scope fall back too, preserving the RHI's eight-layer depth limit.
        if (!repaintBudget_ || recordingSubtree_ || !layers_.empty() || !cacheableTransform_)
        { ++repaintStats_.bypasses; paint(); return; }
        const auto r = Transform(widget.GetPaintBounds());
        for (float value : {r.x, r.y, r.width, r.height, r.x + r.width, r.y + r.height})
            if (!std::isfinite(value)) throw std::invalid_argument("Invalid widget paint bounds.");
        if (r.width < 0 || r.height < 0) throw std::invalid_argument("Negative widget paint bounds.");
        const auto effectClip = clips_.back().effects;
        if (r.x < effectClip.x || r.y < effectClip.y || r.x + r.width > effectClip.x + effectClip.width || r.y + r.height > effectClip.y + effectClip.height)
        {
            // A moving viewport edge would resize this target every frame.
            // Draw partial cards directly; retain their last full snapshot for
            // reuse when they become fully visible again. No allocation churn.
            ++repaintStats_.bypasses; paint(); return;
        }
        // The cache is a performance hint, NOT a new clip boundary. Include one
        // physical pixel of AA fringe and retain the distinct content/effect clips.
        const float fringe = 1 / window_.scale;
        const auto visible = Intersect({r.x - fringe, r.y - fringe, r.width + 2 * fringe, r.height + 2 * fringe}, clips_.back().effects);
        const auto px = [&](float p) { return std::round(window_.contentX + physicalOrigin_.x + double(p) * window_.scale) - physicalOrigin_.x; };
        const auto py = [&](float p) { return std::round(window_.contentY + physicalOrigin_.y + double(p) * window_.scale) - physicalOrigin_.y; };
        const int left = static_cast<int>(std::clamp(px(visible.x), 0.0, double(window_.width)));
        const int top = static_cast<int>(std::clamp(py(visible.y), 0.0, double(window_.height)));
        const int right = static_cast<int>(std::clamp(px(visible.x + visible.width), double(left), double(window_.width)));
        const int bottom = static_cast<int>(std::clamp(py(visible.y + visible.height), double(top), double(window_.height)));
        if (left == right || top == bottom) return;
        const RHIRect pixels{left, top, right - left, bottom - top};
        const auto targetBytes = std::uint64_t(pixels.width) * pixels.height * 4;
        if (pixels.width > 4096 || pixels.height > 4096 || targetBytes > std::min<std::uint64_t>(repaintBudget_, 8ull * 1024 * 1024))
        { ++repaintStats_.bypasses; paint(); return; }
        const auto bounds = widget.GetBounds(), anchor = Transform(bounds);
        const double ax = double(window_.contentX) + physicalOrigin_.x + double(anchor.x) * window_.scale;
        const double ay = double(window_.contentY) + physicalOrigin_.y + double(anchor.y) * window_.scale;
        // Same 1/64-pixel phase used by text rasterization. Integer-pixel scroll
        // translation reuses the texture at 1:1; changed phase/DPI re-rasterizes.
        // Normalize the phase carry before floor: tiny accumulated scroll error
        // around an integer must not alternate between phase 64 and phase 0.
        const double qx = std::round(ax * 64) / 64, qy = std::round(ay * 64) / 64;
        const double ix = std::floor(qx), iy = std::floor(qy);
        std::vector<double> key{bounds.width, bounds.height, scale_, window_.scale,
            std::round((qx - ix) * 64), std::round((qy - iy) * 64),
            left + physicalOrigin_.x - ix, top + physicalOrigin_.y - iy, double(pixels.width), double(pixels.height)};
        for (auto clip : {clips_.back().content, clips_.back().effects})
        {
            // Ignore clipped space outside this snapshot: fully visible cards
            // reuse while scrolling; a newly exposed edge is recorded again.
            const auto c = Intersect(visible, clip);
            key.insert(key.end(), {px(c.x) - left, py(c.y) - top, px(c.x + c.width) - left, py(c.y + c.height) - top});
        }
        for (auto color : {theme_.background, theme_.primary, theme_.onPrimary, theme_.onSurface})
            key.insert(key.end(), {color.r, color.g, color.b, color.a});
        const Rect mask{0, 0, float(pixels.width), float(pixels.height)};
        auto found = subtrees_.find(&widget);
        if (found != subtrees_.end() && !found->second.owner.expired() && found->second.revision == widget.paintRevision_ && found->second.key == key)
        {
            ++repaintStats_.hits; found->second.used = serial_;
            CompositeLayer(found->second.texture, pixels, mask, 0, 1, false, true); return;
        }
        if (found != subtrees_.end()) { repaintStats_.cachedBytes -= found->second.bytes; subtrees_.erase(found); repaintStats_.entries = subtrees_.size(); }
        ++repaintStats_.misses;
        // Keep a state scope for custom painters and restore it even on failure.
        const auto stateDepth = states_.size();
        if (stateDepth >= 31) { ++repaintStats_.bypasses; paint(); return; }
        Save();
        Layer layer{std::make_unique<RHICommandList>(), commands_, window_, physicalOrigin_, clips_, pixels, mask, 0, 1, false};
        layer.content->BeginRenderPass({0, 0, 0, 0}); Flush();
        layers_.push_back(std::move(layer)); commands_ = layers_.back().content.get();
        window_.width = pixels.width; window_.height = pixels.height;
        window_.contentX -= left; window_.contentY -= top;
        physicalOrigin_.x += left; physicalOrigin_.y += top;
        recordingSubtree_ = true; cacheStateDepth_ = states_.size();
        RHITextureHandle texture;
        const auto rollback = [&] {
            vertices_.clear(); texture_.reset();
            commands_ = layers_.front().parent; window_ = layers_.front().window; physicalOrigin_ = layers_.front().origin;
            layers_.clear(); while (states_.size() > stateDepth + 1) states_.pop_back(); recordingSubtree_ = false; Restore();
        };
        try
        {
            paint(); Flush();
            if (layers_.size() != 1 || states_.size() != stateDepth + 1) throw std::logic_error("Unbalanced cached widget painting.");
            commands_->EndRenderPass();
            texture = RHI::CreateLayerTexture(static_cast<std::uint32_t>(pixels.width), static_cast<std::uint32_t>(pixels.height), std::move(*layers_.back().content));
        }
        catch (const CacheDepthLimit&) { rollback(); ++repaintStats_.bypasses; paint(); return; }
        catch (...) { rollback(); throw; }
        commands_ = layers_.back().parent; window_ = layers_.back().window; physicalOrigin_ = layers_.back().origin;
        layers_.pop_back(); recordingSubtree_ = false; Restore();
        const auto bytes = SnapshotBytes(texture);
        if (bytes <= repaintBudget_)
        {
            subtrees_.emplace(&widget, CachedSubtree{widget.weak_from_this(), widget.paintRevision_, serial_, bytes, std::move(key), texture});
            repaintStats_.cachedBytes += bytes;
        }
        TrimRepaintCache();
        CompositeLayer(texture, pixels, mask, 0, 1, false, true);
    }
}
