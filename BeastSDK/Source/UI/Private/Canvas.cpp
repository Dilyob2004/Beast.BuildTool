#include "UI/Canvas.h"
#include "UI/Private/UIShaders.generated.h"
#include <array>
#include <bit>

namespace Beast::UI
{
    namespace
    {
        RHIColor Premultiply(RHIColor c) { return {c.r * c.a, c.g * c.a, c.b * c.a, c.a}; }
        bool Equal(RHIRect a, RHIRect b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
        void ValidateColor(RHIColor color)
        {
            for (float value : {color.r, color.g, color.b, color.a})
                if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument("Invalid UI paint color.");
        }
        void ValidateRect(Rect rect)
        {
            for (float value : {rect.x, rect.y, rect.width, rect.height, rect.x + rect.width, rect.y + rect.height})
                if (!std::isfinite(value)) throw std::invalid_argument("Invalid UI paint rectangle.");
            if (rect.width < 0 || rect.height < 0) throw std::invalid_argument("Negative UI paint size.");
        }
    }
    Canvas::Canvas()
    {
        pipeline_ = RHI::CreatePipeline({Detail::Shaders::BuiltinUIVertex(), Detail::Shaders::BuiltinUIFragment(), RHIBlend::PremultipliedAlpha});
        shapePipeline_ = RHI::CreatePipeline({Detail::Shaders::BuiltinUIVertex(), Detail::Shaders::BuiltinUIShape(), RHIBlend::PremultipliedAlpha});
        layerPipeline_ = RHI::CreatePipeline({Detail::Shaders::BuiltinUIVertex(), Detail::Shaders::BuiltinUILayer(), RHIBlend::PremultipliedAlpha});
        white_ = RHI::CreateTexture({1, 1, {255, 255, 255, 255}});
    }
    void Canvas::Begin(RHICommandList& commands, const Window& window, Theme theme)
    {
        if (window.scale != window_.scale) glyphAtlas_.ResetWarmup();
        commands_ = &commands; window_ = window; theme_ = theme; ++serial_;
        glyphAtlas_.BeginFrame(); glyphQuads_.clear();
        recordingSubtree_ = false; TrimRepaintCache();
        vertices_.clear(); texture_.reset(); clips_.clear();
        translations_.assign(1, Point{});
        states_.clear(); layers_.clear(); scale_ = 1; physicalOrigin_ = {};
        cacheableTransform_ = true; snapShapes_ = true; scrollAxes_ = 0;
        nearbyWarmLabels_ = 0;
        const Rect viewport{0, 0, window.contentWidth / window.scale, window.contentHeight / window.scale};
        clips_.push_back({viewport, viewport, {}}); paintingShadow_ = false;
        commands.Reset(); commands.BeginRenderPass(theme.background);
    }
    void Canvas::End()
    {
        if (!commands_ || !states_.empty() || !layers_.empty()) throw std::logic_error("Unbalanced Canvas Save/Restore.");
        Flush(); glyphAtlas_.EndFrame(); glyphQuads_.clear();
        // Upload prewarmed pages through the ordinary validated RHI lifetime path.
        // Degenerate transparent triangles have no coverage on either backend.
        std::array<RHIVertex, 3> hidden{};
        for (auto& vertex : hidden) vertex.color = {0, 0, 0, 0};
        for (const auto& page : warmPages_) commands_->DrawTriangles(pipeline_, page, hidden, RHIRect{0, 0, 1, 1});
        warmPages_.clear(); commands_->EndRenderPass(); commands_ = nullptr;
        // Retain recently visible lines for reverse scrolling. Bound both
        // CPU textures and the corresponding weak-owner GPU resource cache.
        constexpr std::size_t budget = 16 * 1024 * 1024;
        while (textBytes_ > budget || text_.size() > 512)
        {
            auto oldest = text_.end();
            for (auto it = text_.begin(); it != text_.end(); ++it)
                if (oldest == text_.end() || it->second.used < oldest->second.used) oldest = it;
            if (oldest == text_.end()) break;
            const auto& bitmap = oldest->second.bitmap;
            textBytes_ -= static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height) * 4;
            text_.erase(oldest); // Recorded commands retain their own handles.
        }
    }
    void Canvas::PushClip(Rect rect)
    {
        if (!commands_) throw std::logic_error("UI Canvas is not recording.");
        ValidateRect(rect); rect = Transform(rect);
        clips_.push_back({Intersect(clips_.back().content, rect), Intersect(clips_.back().effects, rect), Intersect(clips_.back().warm, rect)});
    }
    void Canvas::PushLayoutClip(Rect rect)
    {
        if (!commands_) throw std::logic_error("UI Canvas is not recording.");
        ValidateRect(rect); rect = Transform(rect);
        // Layout bounds contain ordinary content, not a descendant's shadow.
        // Viewport/ScrollView/user PushClip remain real effect clip boundaries.
        clips_.push_back({Intersect(clips_.back().content, rect), clips_.back().effects, Intersect(clips_.back().warm, rect)});
    }
    void Canvas::PopClip() { if (clips_.size() > 1) clips_.pop_back(); }
    void Canvas::PushScrollOffset(Point offset, bool pixelAligned, Axis axis)
    {
        if (!commands_ || !std::isfinite(offset.x) || !std::isfinite(offset.y)) throw std::invalid_argument("Invalid scroll paint offset.");
        Save();
        if (!pixelAligned)
        {
            if (discoverWarmup_)
            {
                auto region = clips_.back().content;
                if (region.width > 0 && region.height > 0)
                {
                    // Half a viewport, at most 256 logical units, in either
                    // direction. Nested scrolls stay inside the parent's reserve.
                    const float margin = std::min(256.f * scale_, (axis == Axis::Vertical ? region.height : region.width) * .5f);
                    if (axis == Axis::Vertical) { region.y -= margin; region.height += 2 * margin; }
                    else { region.x -= margin; region.width += 2 * margin; }
                    clips_.back().warm = scrollAxes_ ? Intersect(region, clips_.back().warm) : region;
                }
            }
            scrollAxes_ |= axis == Axis::Vertical ? 1u : 2u;
            // Layout already contains the exact scroll displacement. Keep it.
            // A phase-changing card snapshot would be rebuilt every frame;
            // draw analytic shapes and native-resolution glyph coverage instead.
            snapShapes_ = false; cacheableTransform_ = false;
            return;
        }
        const auto previous = translations_.back();
        offset.x *= scale_; offset.y *= scale_;
        translations_.push_back({previous.x + offset.x - std::round(offset.x * window_.scale) / window_.scale,
            previous.y + offset.y - std::round(offset.y * window_.scale) / window_.scale});
    }
    void Canvas::PopScrollOffset() { Restore(); }
    void Canvas::Flush()
    {
        if (!vertices_.empty()) commands_->DrawTriangles(pipeline_, texture_, vertices_, scissor_);
        vertices_.clear(); texture_.reset();
    }
    void Canvas::BeginBatch(const RHITextureHandle& texture)
    {
        const auto clip = CurrentClip();
        // Disjoint clips can lie far outside the viewport. Clamp before integer conversion.
        const auto x = [&](double logical) { return std::clamp(window_.contentX + logical * window_.scale, 0.0, double(window_.width)); };
        const auto y = [&](double logical) { return std::clamp(window_.contentY + logical * window_.scale, 0.0, double(window_.height)); };
        // Hard clips assign each pixel to one side of a shared edge. Rounding
        // both ends the same way avoids both holes and overlapping scissors.
        const int left = static_cast<int>(std::round(x(clip.x) + physicalOrigin_.x) - physicalOrigin_.x);
        const int top = static_cast<int>(std::round(y(clip.y) + physicalOrigin_.y) - physicalOrigin_.y);
        const int right = static_cast<int>(std::round(x(clip.x + clip.width) + physicalOrigin_.x) - physicalOrigin_.x);
        const int bottom = static_cast<int>(std::round(y(clip.y + clip.height) + physicalOrigin_.y) - physicalOrigin_.y);
        const RHIRect scissor{left, top, std::max(0, right - left), std::max(0, bottom - top)};
        if (texture_ != texture || !Equal(scissor_, scissor) || vertices_.size() > 900000) Flush();
        texture_ = texture; scissor_ = scissor;
    }
    RHIVertex Canvas::Vertex(Point p, Point uv, RHIColor c) const
    {
        return PixelVertex({window_.contentX + p.x * window_.scale, window_.contentY + p.y * window_.scale}, uv, c);
    }
    RHIVertex Canvas::PixelVertex(Point p, Point uv, RHIColor c) const
    {
        return {p.x * 2 / window_.width - 1, p.y * 2 / window_.height - 1, uv.x, uv.y, c};
    }
    void Canvas::RoundedRect(Rect rect, float radius, RHIColor color, float borderWidth, RHIColor borderColor)
    { GradientRect(rect, radius, color, color, {0, 0}, {1, 1}, borderWidth, borderColor); }
    void Canvas::GradientRect(Rect rect, float radius, RHIColor color, RHIColor endColor, Point begin, Point end, float borderWidth, RHIColor borderColor)
    { Shape(rect, radius, color, endColor, begin, end, borderWidth, borderColor); }
    void Canvas::InkRipple(Rect bounds, float cornerRadius, Point origin, float radius, RHIColor color)
    { Shape(bounds, cornerRadius, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0}, {1, 1}, 0, {0, 0, 0, 0}, origin, radius, color); }
    void Canvas::Shape(Rect rect, float radius, RHIColor color, RHIColor endColor, Point begin, Point end,
        float borderWidth, RHIColor borderColor, Point inkOrigin, float inkRadius, RHIColor ink, float shadowSigma)
    {
        if (!commands_) throw std::logic_error("UI Canvas is not recording.");
        ValidateRect(rect); ValidateColor(color); ValidateColor(endColor); ValidateColor(borderColor); ValidateColor(ink);
        if (!std::isfinite(borderWidth) || borderWidth < 0 || !std::isfinite(inkRadius) || inkRadius < 0 ||
            !std::isfinite(inkOrigin.x) || !std::isfinite(inkOrigin.y)) throw std::invalid_argument("Invalid UI border or ink.");
        rect = Transform(rect);
        inkOrigin.x = inkOrigin.x * scale_ + translations_.back().x; inkOrigin.y = inkOrigin.y * scale_ + translations_.back().y;
        radius *= scale_; borderWidth *= scale_; inkRadius *= scale_; shadowSigma *= scale_;
        for (float v : {begin.x, begin.y, end.x, end.y})
            if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Gradient endpoints must be in [0,1].");
        const float dx = end.x - begin.x, dy = end.y - begin.y, length = dx * dx + dy * dy;
        if (length <= 0) throw std::invalid_argument("Gradient endpoints must differ.");
        if (!std::isfinite(radius)) throw std::invalid_argument("Invalid UI corner radius.");
        const float spread = shadowSigma * 4;
        const auto visible = Intersect({rect.x - spread, rect.y - spread, rect.width + spread * 2, rect.height + spread * 2}, CurrentClip());
        if (visible.width <= 0 || visible.height <= 0) return;
        if (rect.width <= 0 || rect.height <= 0 || (color.a <= 0 && endColor.a <= 0 && borderColor.a <= 0 && ink.a <= 0)) return;
        BeginBatch(white_);
        Flush(); // Shape constants differ per primitive; text retains its batching.
        const float scale = window_.scale;
        const auto position = [this](double value, float origin) { return static_cast<float>((snapShapes_ ? std::round(value) : value) - origin); };
        const float left = position(window_.contentX + physicalOrigin_.x + double(rect.x) * scale, physicalOrigin_.x);
        const float top = position(window_.contentY + physicalOrigin_.y + double(rect.y) * scale, physicalOrigin_.y);
        // Form endpoints in the same float geometry as Rect/Intersect/layout;
        // promoting only one side before addition can split a half-pixel tie.
        const float right = position(window_.contentX + physicalOrigin_.x + double(rect.x + rect.width) * scale, physicalOrigin_.x);
        const float bottom = position(window_.contentY + physicalOrigin_.y + double(rect.y + rect.height) * scale, physicalOrigin_.y);
        const float width = right - left, height = bottom - top;
        if (width <= 0 || height <= 0) return;
        RHIDrawConstants constants{};
        const auto put = [&](int index, RHIColor c) {
            constants[index * 4] = c.r; constants[index * 4 + 1] = c.g;
            constants[index * 4 + 2] = c.b; constants[index * 4 + 3] = c.a;
        };
        put(0, {width, height, std::clamp(radius * scale, 0.0f, std::min(width, height) / 2), std::min(borderWidth * scale, std::min(width, height) / 2)});
        put(1, Premultiply(color)); put(2, Premultiply(endColor)); put(3, {begin.x, begin.y, end.x, end.y});
        put(4, Premultiply(borderColor));
        put(5, {window_.contentX + inkOrigin.x * scale - left, window_.contentY + inkOrigin.y * scale - top, inkRadius * scale, ink.a > 0 ? 1.0f : 0.0f});
        put(6, Premultiply(ink)); put(7, {1, shadowSigma * scale, 0, 0});
        const RHIColor white{1, 1, 1, 1};
        const float fringe = std::ceil(spread * scale) + 1;
        const auto tl = PixelVertex({left - fringe, top - fringe}, {-fringe, -fringe}, white), tr = PixelVertex({right + fringe, top - fringe}, {width + fringe, -fringe}, white);
        const auto br = PixelVertex({right + fringe, bottom + fringe}, {width + fringe, height + fringe}, white), bl = PixelVertex({left - fringe, bottom + fringe}, {-fringe, height + fringe}, white);
        const std::array quad{tl, tr, br, tl, br, bl};
        commands_->DrawTriangles(shapePipeline_, white_, quad, scissor_, constants);
    }
    void Canvas::Shadow(Rect rect, float radius, float blur, Point offset, RHIColor color)
    {
        if (!std::isfinite(blur) || blur < 0 || blur > 128) throw std::invalid_argument("Shadow blur must be in [0,128].");
        ValidateColor(color); ValidateRect(rect);
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) throw std::invalid_argument("Invalid shadow offset.");
        struct Restore { bool& flag; bool previous; ~Restore() { flag = previous; } } restore{paintingShadow_, paintingShadow_};
        paintingShadow_ = true;
        // One continuous distance-field Gaussian, sigma = blur/2, support 4 sigma.
        // A zero blur is an ordinary offset silhouette. No stacked/snap-rounded bands.
        Shape({rect.x + offset.x, rect.y + offset.y, rect.width, rect.height}, radius, color, color,
            {0, 0}, {1, 1}, 0, {0, 0, 0, 0}, {}, 0, {0, 0, 0, 0}, blur * .5f);
    }
    void Canvas::Text(Point origin, const std::string& text, float fontSize, RHIColor color, FontFace face)
    {
        if (!commands_) throw std::logic_error("UI Canvas is not recording.");
        ValidateColor(color);
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(fontSize) || fontSize < 1 || fontSize > 256 || text.size() > 4096)
            throw std::invalid_argument("Invalid UI text paint arguments.");
        if (text.empty()) return;
        origin.x = origin.x * scale_ + translations_.back().x; origin.y = origin.y * scale_ + translations_.back().y;
        fontSize *= scale_;
        const auto clip = CurrentClip();
        const auto warmClip = clips_.back().warm;
        const bool discover = discoverWarmup_ && !snapShapes_;
        const bool nearby = discover && nearbyWarmLabels_ < 64 && warmClip.width > 0 && warmClip.height > 0 &&
            text.size() <= 128 && fontSize * window_.scale <= 96 &&
            origin.x < warmClip.x + warmClip.width && origin.y < warmClip.y + warmClip.height;
        if ((clip.width <= 0 || clip.height <= 0) && !nearby) return;
        const auto measured = Detail::MeasureText(text, fontSize, face);
        const auto intersects = [&](Rect r) { return r.width > 0 && r.height > 0 &&
            origin.x < r.x + r.width && origin.y < r.y + r.height &&
            origin.x + measured.width >= r.x && origin.y + measured.height >= r.y; };
        const bool visible = intersects(clip);
        const bool prefetch = nearby && intersects(warmClip);
        if (!visible && !prefetch) return;
        const float pixels = fontSize * window_.scale;
        // Quantize the rasterization phase to 1/64 physical pixel, not an entire
        // layout pixel. Fractional positioning is baked into glyph coverage.
        // Moving text needs a bounded set of native-coverage variants. 64
        // phases per axis thrash both atlas pages and prepared-run retention.
        // Eighth-pixel phases keep error <=1/16 pixel without texture filtering.
        const double phaseSteps = snapShapes_ ? 64.0 : 8.0;
        const auto phasePosition = [phaseSteps](double value) { return std::round(value * phaseSteps) / phaseSteps; };
        const double physicalX = phasePosition(window_.contentX + physicalOrigin_.x + double(origin.x) * window_.scale) - physicalOrigin_.x;
        const double physicalY = phasePosition(window_.contentY + physicalOrigin_.y + double(origin.y) * window_.scale) - physicalOrigin_.y;
        const float left = static_cast<float>(std::floor(physicalX)), top = static_cast<float>(std::floor(physicalY));
        const Point phase{static_cast<float>(physicalX - std::floor(physicalX)), static_cast<float>(physicalY - std::floor(physicalY))};
        if (discover && (visible || prefetch)) {
            if (!visible) ++nearbyWarmLabels_;
            if (scrollAxes_ & 1u) glyphAtlas_.QueueWarm(text, pixels, phase, face, Axis::Vertical);
            if (scrollAxes_ & 2u) glyphAtlas_.QueueWarm(text, pixels, phase, face, Axis::Horizontal);
        }
        // Nearby text is queued, never rasterized or painted in this frame.
        if (!visible) return;
        if (glyphAtlas_.Resolve(text, pixels, phase, face, color.a < 1, glyphQuads_))
        {
            const auto c = Premultiply(color);
            for (const auto& glyph : glyphQuads_)
            {
                const auto r = glyph.pixels, uv = glyph.uv;
                BeginBatch(glyph.texture);
                const auto tl = PixelVertex({left + r.x, top + r.y}, {uv.x, uv.y}, c);
                const auto tr = PixelVertex({left + r.x + r.width, top + r.y}, {uv.x + uv.width, uv.y}, c);
                const auto br = PixelVertex({left + r.x + r.width, top + r.y + r.height}, {uv.x + uv.width, uv.y + uv.height}, c);
                const auto bl = PixelVertex({left + r.x, top + r.y + r.height}, {uv.x, uv.y + uv.height}, c);
                vertices_.insert(vertices_.end(), {tl, tr, br, tl, br, bl});
            }
            return;
        }
        // Rasterize the natural line once, independent of viewport clipping.
        // The GPU scissor clips it; scrolling must not create a different image.
        // Preserve fractional physical sizes in the cache, including two DPI
        // values whose old ceil(pixelSize) key used to be identical.
        const std::string key = std::to_string(std::bit_cast<std::uint32_t>(pixels)) + "/" +
            std::to_string(static_cast<int>(phase.x * 64)) + "/" + std::to_string(static_cast<int>(phase.y * 64)) + "/" +
            std::to_string(static_cast<int>(face)) + "/" + text;
        auto found = text_.find(key);
        if (found == text_.end())
        {
            found = text_.emplace(key, CachedText{Detail::RasterizeText(text, pixels, 4096, phase, 4096, face), serial_}).first;
            const auto& bitmap = found->second.bitmap;
            textBytes_ += static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height) * 4;
        }
        found->second.used = serial_;
        const auto& bitmap = found->second.bitmap;
        if (!bitmap.texture) return;
        BeginBatch(bitmap.texture);
        const auto c = Premultiply(color);
        const float right = left + bitmap.width, bottom = top + bitmap.height;
        const auto tl = PixelVertex({left, top}, {0, 0}, c), tr = PixelVertex({right, top}, {1, 0}, c);
        const auto br = PixelVertex({right, bottom}, {1, 1}, c), bl = PixelVertex({left, bottom}, {0, 1}, c);
        vertices_.insert(vertices_.end(), {tl, tr, br, tl, br, bl});
    }
}
