#include "UI/Canvas.h"
#include <array>

namespace Beast::UI
{
    Rect Canvas::Transform(Rect r) const
    { return {r.x * scale_ + translations_.back().x, r.y * scale_ + translations_.back().y, r.width * scale_, r.height * scale_}; }
    void Canvas::Save()
    {
        if (!commands_ || states_.size() >= 32) throw std::logic_error("Canvas state stack is unavailable or full.");
        states_.push_back({clips_, translations_, scale_, layers_.size(), cacheableTransform_, snapShapes_, scrollAxes_});
    }
    void Canvas::Restore()
    {
        if (!commands_ || states_.empty()) throw std::logic_error("Canvas Restore without Save.");
        if (recordingSubtree_ && states_.size() <= cacheStateDepth_) throw std::logic_error("Cached painter restored its caller's state.");
        while (layers_.size() > states_.back().layers) EndLayer();
        Flush(); auto state = std::move(states_.back()); states_.pop_back();
        clips_ = std::move(state.clips); translations_ = std::move(state.translations); scale_ = state.scale;
        cacheableTransform_ = state.cacheableTransform;
        snapShapes_ = state.snapShapes;
        scrollAxes_ = state.scrollAxes;
    }
    void Canvas::Translate(float x, float y)
    {
        if (!commands_ || !std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("Invalid Canvas translation.");
        const Point p{translations_.back().x + x * scale_, translations_.back().y + y * scale_};
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::invalid_argument("Canvas translation overflow.");
        // Arbitrary subpixel transforms can cross a shape's half-pixel snap
        // inside one text-cache phase. Draw directly rather than reuse stale AA.
        const double dx = double(x) * scale_ * window_.scale, dy = double(y) * scale_ * window_.scale;
        cacheableTransform_ &= dx == std::round(dx) && dy == std::round(dy);
        translations_.back() = p;
    }
    void Canvas::Scale(float factor)
    {
        if (!commands_ || !std::isfinite(factor) || factor <= 0 || scale_ * factor < .0625f || scale_ * factor > 16)
            throw std::invalid_argument("Canvas combined scale must be in [1/16,16].");
        scale_ *= factor;
    }
    void Canvas::SaveLayer(Rect bounds, float opacity)
    {
        Save();
        try { BeginLayer(bounds, opacity, 0, false); }
        catch (...) { states_.pop_back(); throw; }
    }
    void Canvas::ClipRRect(Rect bounds, float radius)
    {
        if (states_.empty()) throw std::logic_error("ClipRRect requires Save/Restore.");
        BeginLayer(bounds, 1, radius, true);
    }
    void Canvas::BeginLayer(Rect bounds, float opacity, float radius, bool rounded)
    {
        if (recordingSubtree_ && layers_.size() >= 8) throw CacheDepthLimit{};
        for (float v : {bounds.x, bounds.y, bounds.width, bounds.height, opacity, radius})
            if (!std::isfinite(v)) throw std::invalid_argument("Non-finite Canvas layer.");
        if (bounds.width < 0 || bounds.height < 0 || opacity < 0 || opacity > 1 || radius < 0 || layers_.size() >= 8)
            throw std::invalid_argument("Invalid Canvas layer size, opacity, radius or nesting.");
        const auto r = Transform(bounds);
        for (float v : {r.x, r.y, r.width, r.height, r.x + r.width, r.y + r.height})
            if (!std::isfinite(v)) throw std::invalid_argument("Canvas layer transform overflow.");
        const auto clip = Intersect(r, clips_.back().content);
        const auto px = [&](float p) { return std::round(double(window_.contentX) + physicalOrigin_.x + double(p) * window_.scale) - physicalOrigin_.x; };
        const auto py = [&](float p) { return std::round(double(window_.contentY) + physicalOrigin_.y + double(p) * window_.scale) - physicalOrigin_.y; };
        const int left = static_cast<int>(std::clamp(px(clip.x), 0.0, double(window_.width)));
        const int top = static_cast<int>(std::clamp(py(clip.y), 0.0, double(window_.height)));
        const int right = static_cast<int>(std::clamp(px(clip.x + clip.width), double(left), double(window_.width)));
        const int bottom = static_cast<int>(std::clamp(py(clip.y + clip.height), double(top), double(window_.height)));
        const RHIRect pixels{left, top, right - left, bottom - top};
        if (pixels.width > 4096 || pixels.height > 4096) throw std::invalid_argument("Canvas layer exceeds 4096 pixels.");
        const float mx = static_cast<float>(px(r.x)), my = static_cast<float>(py(r.y));
        const Rect mask{mx - left, my - top, static_cast<float>(px(r.x + r.width)) - mx,
            static_cast<float>(py(r.y + r.height)) - my};
        Layer layer{std::make_unique<RHICommandList>(), commands_, window_, physicalOrigin_, clips_, pixels, mask,
            std::min(radius * scale_ * window_.scale, std::min(mask.width, mask.height) / 2), opacity, rounded};
        layer.content->BeginRenderPass({0, 0, 0, 0});
        Flush(); layers_.push_back(std::move(layer));
        commands_ = layers_.back().content.get();
        window_.width = std::max(1, pixels.width); window_.height = std::max(1, pixels.height);
        window_.contentX -= left; window_.contentY -= top;
        physicalOrigin_.x += left; physicalOrigin_.y += top;
        PushClip(bounds);
    }
    void Canvas::EndLayer()
    {
        Flush(); commands_->EndRenderPass();
        auto layer = std::move(layers_.back()); layers_.pop_back();
        commands_ = layer.parent; window_ = layer.window; physicalOrigin_ = layer.origin; clips_ = std::move(layer.clips);
        if (!layer.pixels.width || !layer.pixels.height || layer.opacity == 0) return;
        auto texture = RHI::CreateLayerTexture(static_cast<std::uint32_t>(layer.pixels.width),
            static_cast<std::uint32_t>(layer.pixels.height), std::move(*layer.content));
        CompositeLayer(texture, layer.pixels, layer.mask, layer.radius, layer.opacity, layer.rounded);
    }
    void Canvas::CompositeLayer(const RHITextureHandle& texture, RHIRect pixels, Rect mask, float radius, float opacity, bool rounded, bool effects)
    {
        const bool previous = paintingShadow_; paintingShadow_ = effects;
        BeginBatch(texture); Flush();
        paintingShadow_ = previous;
        const float x = static_cast<float>(pixels.x), y = static_cast<float>(pixels.y);
        const float w = static_cast<float>(pixels.width), h = static_cast<float>(pixels.height);
        const RHIColor white{1, 1, 1, 1};
        const auto tl = PixelVertex({x, y}, {0, 0}, white), tr = PixelVertex({x + w, y}, {1, 0}, white);
        const auto br = PixelVertex({x + w, y + h}, {1, 1}, white), bl = PixelVertex({x, y + h}, {0, 1}, white);
        const std::array vertices{tl, tr, br, tl, br, bl};
        RHIDrawConstants constants{};
        constants[0] = mask.x; constants[1] = mask.y; constants[2] = mask.width; constants[3] = mask.height;
        constants[4] = w; constants[5] = h; constants[6] = radius; constants[7] = opacity;
        constants[8] = rounded ? 1.f : 0.f;
        commands_->DrawTriangles(layerPipeline_, texture, vertices, scissor_, constants);
    }
}
