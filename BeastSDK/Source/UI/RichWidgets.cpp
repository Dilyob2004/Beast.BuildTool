#include "UI/UI.h"
#include "UI/Canvas.h"
#include "UI/Private/TextLayout.h"

namespace Beast::UI
{
    namespace
    {
        std::string Utf8(char32_t cp)
        {
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw std::invalid_argument("Invalid icon codepoint.");
            std::string text;
            if (cp < 0x80) text += static_cast<char>(cp);
            else if (cp < 0x800) { text += static_cast<char>(0xc0 | (cp >> 6)); text += static_cast<char>(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { text += static_cast<char>(0xe0 | (cp >> 12)); text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63)); }
            else { text += static_cast<char>(0xf0 | (cp >> 18)); text += static_cast<char>(0x80 | ((cp >> 12) & 63)); text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63)); }
            return text;
        }
        void RemoveLast(std::string& text)
        {
            if (text.empty()) return;
            const auto boundaries = TextEditor::GraphemeBoundaries(text, true);
            text.resize(boundaries[boundaries.size() - 2]);
        }
        void CheckColor(RHIColor color)
        { for (float v : {color.r, color.g, color.b, color.a}) if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Invalid UI color."); }
    }
    Paragraph::Paragraph(std::string text, ParagraphStyle style) : text_(std::move(text)), style_(style)
    {
        Detail::MeasureText(text_, style.fontSize, style.face);
        if (!std::isfinite(style.lineHeight) || style.lineHeight < 1 || style.lineHeight > 4 || style.maxLines > 4096 ||
            style.align < TextAlign::Start || style.align > TextAlign::End) throw std::invalid_argument("Invalid paragraph style.");
        if (style.color) CheckColor(*style.color);
    }
    void Paragraph::SetText(std::string text)
    { Detail::ValidateText(text); if (text != text_) { text_ = std::move(text); measuredWidth_ = -1; Invalidate(); } }
    void Paragraph::Wrap(float width) const
    {
        if (width == measuredWidth_) return;
        measuredWidth_ = width; lines_.clear();
        const auto measure = [&](const std::string& s) { return Detail::MeasureText(s, style_.fontSize, style_.face).width; };
        for (const auto& range : Detail::WrapText(text_, width, style_.fontSize, style_.face)) {
            auto line = text_.substr(range.start, range.end - range.start);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            lines_.push_back(std::move(line));
        }
        if (style_.maxLines && lines_.size() > style_.maxLines)
        {
            lines_.resize(style_.maxLines);
            if (style_.ellipsis)
            {
                auto& last = lines_.back();
                while (!last.empty() && measure(last + "…") > width) RemoveLast(last);
                last += "…";
            }
        }
    }
    Size Paragraph::OnMeasure(const Constraints& c) const
    {
        Wrap(c.maxWidth); float width = 0;
        for (const auto& line : lines_) width = std::max(width, Detail::MeasureText(line, style_.fontSize, style_.face).width);
        float height = 0;
        for (const auto& line : lines_) height += std::max(Detail::MeasureText(line, style_.fontSize, style_.face).height, style_.fontSize * style_.lineHeight);
        return c.Constrain({width, height});
    }
    void Paragraph::OnPaint(Canvas& canvas) const
    {
        const auto r = GetBounds(); Wrap(r.width);
        float y = r.y;
        for (const auto& line : lines_)
        {
            const float width = Detail::MeasureText(line, style_.fontSize, style_.face).width;
            const float natural = Detail::MeasureText(line, style_.fontSize, style_.face).height;
            const float advance = std::max(natural, style_.fontSize * style_.lineHeight);
            const float extra = std::max(0.0f, r.width - width);
            const float x = r.x + (style_.align == TextAlign::Center ? extra / 2 : style_.align == TextAlign::End ? extra : 0);
            canvas.Text({x, y + (advance - natural) / 2}, line, style_.fontSize, style_.color.value_or(canvas.GetTheme().onSurface), style_.face); y += advance;
        }
    }
    Icon::Icon(char32_t codepoint, float size, RHIColor color) : glyph_(Utf8(codepoint)), size_(size), color_(color)
    {
        Detail::MeasureText(glyph_, size, FontFace::Icons); CheckColor(color);
        if (!Detail::HasGlyph(codepoint, FontFace::Icons)) throw std::invalid_argument("Icon is not included in the embedded Material Icons subset.");
    }
    Size Icon::OnMeasure(const Constraints&) const { return {size_, size_}; }
    void Icon::OnPaint(Canvas& canvas) const
    {
        const auto r = GetBounds(); const auto m = Detail::MeasureText(glyph_, size_, FontFace::Icons);
        canvas.Text({r.x + (r.width - m.width) / 2, r.y + (r.height - m.height) / 2}, glyph_, size_, color_, FontFace::Icons);
    }
    Pressable::Pressable(WidgetPtr child, std::function<void()> onPressed, std::optional<float> inkRadius)
        : Button("", std::move(onPressed)), inkRadius_(inkRadius)
    {
        if (inkRadius && (!std::isfinite(*inkRadius) || *inkRadius < 0)) throw std::invalid_argument("Invalid ink radius.");
        Adopt(std::move(child));
    }
    Size Pressable::OnMeasure(const Constraints& c) const { return Children().front()->Measure(c); }
    void Pressable::OnLayout() { Children().front()->Layout(GetBounds()); }
    void Pressable::OnPaint(Canvas& canvas) const
    {
        Widget::OnPaint(canvas);
        const auto* container = dynamic_cast<const Container*>(Children().front().get());
        PaintInk(canvas, GetBounds(), inkRadius_.value_or(container ? container->GetStyle().cornerRadius : 0), {0, 0, 0, 1});
    }
    bool Pressable::OnTick(float seconds) { return Button::OnTick(seconds) || Widget::OnTick(seconds); }
    ScrollView::ScrollView(WidgetPtr child, Axis axis, ScrollPhysics physics) : axis_(axis), physics_(physics)
    {
        if (axis != Axis::Horizontal && axis != Axis::Vertical) throw std::invalid_argument("Invalid scroll axis.");
        if (physics != ScrollPhysics::Bouncing && physics != ScrollPhysics::Clamping) throw std::invalid_argument("Invalid scroll physics.");
        Adopt(std::move(child));
    }
    Size ScrollView::ContentSize(Size viewport) const
    {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return Children().front()->Measure(axis_ == Axis::Vertical ? Constraints{viewport.width, viewport.width, 0, inf} : Constraints{0, inf, viewport.height, viewport.height});
    }
    Size ScrollView::OnMeasure(const Constraints& c) const
    {
        if (!std::isfinite(c.maxWidth) || !std::isfinite(c.maxHeight))
            throw std::invalid_argument("ScrollView needs a finite viewport; use Expanded or a sized Container.");
        return {c.maxWidth, c.maxHeight};
    }
    void ScrollView::OnLayout()
    {
        rasterScale_ = 0; // Layout/DPI/transform changes must repaint conservatively.
        const auto r = GetBounds(); const auto size = ContentSize({r.width, r.height});
        const float viewport = axis_ == Axis::Vertical ? r.height : r.width;
        const float extent = std::max(0.0f, (axis_ == Axis::Vertical ? size.height : size.width) - viewport);
        if (viewport != viewport_ || extent != extent_)
        {
            viewport_ = viewport; extent_ = extent;
            velocity_ = 0; dragging_ = springing_ = false;
            offset_ = std::clamp(offset_, 0.0f, extent_);
        }
        Children().front()->Layout({r.x - (axis_ == Axis::Horizontal ? offset_ : 0), r.y - (axis_ == Axis::Vertical ? offset_ : 0), size.width, size.height});
    }
    void ScrollView::OnPaint(Canvas& canvas) const
    {
        rasterScale_ = canvas.GetPhysicalScale();
        canvas.PushClip(GetBounds()); // Real viewport: also clips descendant glow/shadows.
        canvas.PushScrollOffset(axis_ == Axis::Vertical ? Point{0, offset_} : Point{offset_, 0},
            rasterization_ == ScrollRasterization::PixelAligned, axis_);
        Widget::OnPaint(canvas);
        canvas.PopScrollOffset();
        canvas.PopClip();
    }
    bool ScrollView::VisibleMovement(float before, float after) const
    {
        // Only the legacy mode suppresses same-pixel frames. Subpixel geometry
        // changes coverage even when the rounded integer offset is unchanged.
        return before != after && (rasterization_ == ScrollRasterization::Subpixel || rasterScale_ <= 0 ||
            std::round(before * rasterScale_) != std::round(after * rasterScale_));
    }
    void ScrollView::SetRasterization(ScrollRasterization mode)
    {
        if (mode != ScrollRasterization::PixelAligned && mode != ScrollRasterization::Subpixel)
            throw std::invalid_argument("Invalid scroll rasterization mode.");
        if (rasterization_ == mode) return;
        rasterization_ = mode; InvalidatePaint();
    }
    void ScrollView::MoveTo(float next)
    {
        if (next == offset_) return;
        const bool redraw = VisibleMovement(offset_, next);
        const float delta = offset_ - next; offset_ = next;
        // Content size is unchanged: move the retained subtree, not a full
        // Measure/Layout pass. Hit testing follows the same logical geometry.
        Children().front()->Translate(axis_ == Axis::Vertical ? Point{0, delta} : Point{delta, 0});
        InvalidatePaint(true, redraw);
    }
    void ScrollView::SetOffset(float offset)
    {
        if (!std::isfinite(offset)) throw std::invalid_argument("Invalid scroll offset.");
        velocity_ = 0; dragging_ = springing_ = false;
        releaseTimestamp_ = 0; firstTickSeconds_ = -1;
        MoveTo(std::clamp(offset, 0.0f, extent_));
    }
    bool ScrollView::ScrollBy(float delta)
    {
        if (!std::isfinite(delta)) throw std::invalid_argument("Invalid scroll delta.");
        const float before = offset_; SetOffset(static_cast<float>(std::clamp(double(offset_) + delta, 0.0, double(extent_)))); return before != offset_;
    }
    void ScrollView::BeginDrag()
    { velocity_ = 0; springing_ = false; dragging_ = true; releaseTimestamp_ = 0; firstTickSeconds_ = -1; }
    bool ScrollView::DragBy(float delta)
    {
        if (!std::isfinite(delta)) throw std::invalid_argument("Invalid scroll delta.");
        const float before = offset_;
        if (!dragging_) BeginDrag();
        if (physics_ == ScrollPhysics::Clamping)
            MoveTo(static_cast<float>(std::clamp(double(offset_) + delta, 0.0, double(extent_))));
        else
        {
            // Invert the rubber mapping before adding finger travel. This
            // makes resistance independent of input frequency and reversible.
            const double limit = std::max(1.0, double(viewport_) * .5);
            const double edge = std::clamp(offset_, 0.0f, extent_);
            const double overshoot = double(offset_) - edge;
            const double raw = edge + overshoot * limit / std::max(.001, limit - std::abs(overshoot));
            const double next = raw + delta, target = std::clamp(next, 0.0, double(extent_));
            const double excess = next - target;
            MoveTo(static_cast<float>(target + limit * excess / (limit + std::abs(excess))));
        }
        return before != offset_;
    }
    void ScrollView::Stop()
    { SetOffset(offset_); }
    void ScrollView::Fling(float velocity)
    {
        if (!std::isfinite(velocity)) throw std::invalid_argument("Invalid scroll velocity.");
        dragging_ = false; velocity_ = std::clamp(velocity, -4000.0f, 4000.0f);
        releaseTimestamp_ = 0; firstTickSeconds_ = -1;
        springTarget_ = std::clamp(offset_, 0.0f, extent_);
        springing_ = offset_ != springTarget_;
    }
    bool ScrollView::OnTick(float seconds)
    {
        bool changed = Widget::OnTick(seconds);
        if (firstTickSeconds_ >= 0) { seconds = std::min(seconds, firstTickSeconds_); firstTickSeconds_ = -1; }
        if (dragging_ || seconds <= 0) return changed;
        const float before = offset_;
        constexpr float friction = 4.2f, omega = 18;
        if (!springing_ && std::abs(velocity_) >= 8)
        {
            const float decay = std::exp(-friction * seconds);
            const float next = offset_ + velocity_ * (1 - decay) / friction;
            const float edge = std::clamp(next, 0.0f, extent_);
            if (next == edge) { MoveTo(next); velocity_ *= decay; seconds = 0; }
            else if (physics_ == ScrollPhysics::Clamping) { MoveTo(edge); velocity_ = 0; }
            else
            {
                // Integrate exactly to the boundary, then spend the remainder
                // of this frame in the spring (same path at 30/60/120 Hz).
                const float crossing = std::clamp(-std::log(std::clamp(1 - friction * (edge - offset_) / velocity_, .000001f, 1.0f)) / friction, 0.0f, seconds);
                velocity_ *= std::exp(-friction * crossing); seconds -= crossing;
                springTarget_ = edge; springing_ = true; MoveTo(edge);
            }
        }
        if (springing_)
        {
            const float displacement = offset_ - springTarget_, b = velocity_ + omega * displacement;
            const float decay = std::exp(-omega * seconds);
            const float next = (displacement + b * seconds) * decay;
            velocity_ = (velocity_ - omega * b * seconds) * decay;
            // End in screen-space, not at an arbitrarily tiny logical epsilon.
            // Pixel-aligned mode may finish within 1 physical pixel; fractional
            // painting keeps the tail down to .05 pixel to avoid a visible snap.
            // The speed check prevents ending a fast crossing near the target.
            const float scale = rasterScale_ > 0 ? rasterScale_ : 1.f;
            const float tolerance = rasterization_ == ScrollRasterization::Subpixel ? .05f : 1.f;
            if (std::abs(next) * scale < tolerance && std::abs(velocity_) * scale < 20.f * tolerance)
            { MoveTo(springTarget_); velocity_ = 0; springing_ = false; }
            else MoveTo(springTarget_ + next);
        }
        else if (std::abs(velocity_) < 8) velocity_ = 0;
        return changed || VisibleMovement(before, offset_);
    }
}
