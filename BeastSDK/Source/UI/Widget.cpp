#include "UI/UI.h"
#include "UI/Canvas.h"
#include "UI/Private/Font.h"

namespace Beast::UI
{
    namespace
    {
        void Nonnegative(float value)
        { if (!std::isfinite(value) || value < 0) throw std::invalid_argument("UI dimensions must be finite and nonnegative."); }
        RHIColor Mix(RHIColor a, RHIColor b, float amount)
        { return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount}; }
    }
    Widget::~Widget() { for (auto& child : children_) { child->parent_ = nullptr; child->Attach(nullptr); } }
    void Widget::Attach(Context* owner)
    {
        if (!owner && owner_) if (auto* button = dynamic_cast<Button*>(this)) button->Cancel();
        owner_ = owner; for (auto& child : children_) child->Attach(owner);
    }
    void Widget::Adopt(WidgetPtr child)
    {
        if (!child || child->parent_ || child->owner_) throw std::invalid_argument("UI child is null or already belongs to a tree.");
        for (const Widget* ancestor = this; ancestor; ancestor = ancestor->parent_)
            if (ancestor == child.get()) throw std::invalid_argument("UI tree cycle.");
        children_.push_back(child); child->parent_ = this; child->Attach(owner_); Invalidate();
    }
    void Widget::ClearChildren()
    {
        for (auto& child : children_) { child->parent_ = nullptr; child->Attach(nullptr); }
        children_.clear(); Invalidate();
    }
    void Widget::MarkPaintDirty() noexcept
    { for (auto* node = this; node; node = node->parent_) ++node->paintRevision_; }
    void Widget::Invalidate() { MarkPaintDirty(); if (owner_) owner_->Invalidate(); }
    void Widget::InvalidatePaint(bool moved, bool redraw)
    { if (redraw) MarkPaintDirty(); if (owner_) owner_->InvalidatePaint(moved ? this : nullptr, redraw); }
    void Widget::Translate(Point delta)
    {
        bounds_.x += delta.x; bounds_.y += delta.y;
        for (const auto& child : children_) child->Translate(delta);
    }
    Size Widget::Measure(const Constraints& constraints) const
    { constraints.Validate(); return constraints.Constrain(OnMeasure(constraints)); }
    void Widget::Layout(Rect bounds)
    {
        if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y)) throw std::invalid_argument("Invalid UI position.");
        Nonnegative(bounds.width); Nonnegative(bounds.height);
        if (bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.width != bounds_.width || bounds.height != bounds_.height) MarkPaintDirty();
        bounds_ = bounds; OnLayout();
    }
    void Widget::OnPaint(Canvas& canvas) const { for (const auto& child : children_) child->Paint(canvas); }
    void Widget::Paint(Canvas& canvas) const
    {
        if (bounds_.width <= 0 || bounds_.height <= 0) return;
        OnPaintBehind(canvas);
        canvas.PushLayoutClip(bounds_); OnPaint(canvas); canvas.PopClip();
    }
    bool Widget::OnTick(float seconds)
    { bool active = false; for (const auto& child : children_) active |= child->Tick(seconds); return active; }
    bool Widget::Tick(float seconds)
    { const bool active = OnTick(seconds); if (active) MarkPaintDirty(); return active; }
    Rect Widget::GetPaintBounds() const
    {
        auto result = bounds_;
        for (const auto& child : children_) result = Unite(result, child->GetPaintBounds());
        return result;
    }
    Text::Text(std::string text, float fontSize, std::optional<RHIColor> color)
        : text_(std::move(text)), fontSize_(fontSize), color_(color)
    {
        Detail::MeasureText(text_, fontSize_);
        if (color_) for (float value : {color_->r, color_->g, color_->b, color_->a})
            if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument("Invalid UI text color.");
    }
    void Text::SetText(std::string text)
    { Detail::ValidateText(text); if (text_ != text) { text_ = std::move(text); Invalidate(); } }
    Size Text::OnMeasure(const Constraints& c) const { return c.Constrain(Detail::MeasureText(text_, fontSize_)); }
    void Text::OnPaint(Canvas& canvas) const
    { const auto r = GetBounds(); canvas.Text({r.x, r.y}, text_, fontSize_, color_.value_or(canvas.GetTheme().onSurface)); }
    Padding::Padding(float inset, WidgetPtr child) : inset_(inset) { Nonnegative(inset); Adopt(std::move(child)); }
    Size Padding::OnMeasure(const Constraints& c) const
    {
        const auto size = Children().front()->Measure({0, std::max(0.0f, c.maxWidth - 2 * inset_), 0, std::max(0.0f, c.maxHeight - 2 * inset_)});
        return c.Constrain({size.width + 2 * inset_, size.height + 2 * inset_});
    }
    void Padding::OnLayout()
    {
        const auto r = GetBounds(); const float x = std::min(inset_, r.width / 2), y = std::min(inset_, r.height / 2);
        Children().front()->Layout({r.x + x, r.y + y, std::max(0.0f, r.width - 2 * inset_), std::max(0.0f, r.height - 2 * inset_)});
    }
    Size Center::OnMeasure(const Constraints& c) const
    {
        const auto size = Children().front()->Measure({0, c.maxWidth, 0, c.maxHeight});
        return c.Constrain({std::isfinite(c.maxWidth) ? c.maxWidth : size.width, std::isfinite(c.maxHeight) ? c.maxHeight : size.height});
    }
    void Center::OnLayout()
    {
        const auto r = GetBounds(); const auto size = Children().front()->Measure(Constraints::Loose({r.width, r.height}));
        Children().front()->Layout({r.x + (r.width - size.width) / 2, r.y + (r.height - size.height) / 2, size.width, size.height});
    }
    Button::Button(std::string label, std::function<void()> onPressed) : label_(std::move(label)), callback_(std::move(onPressed))
    { Detail::ValidateText(label_); }
    void Button::SetText(std::string text)
    { Detail::ValidateText(text); if (label_ != text) { label_ = std::move(text); Invalidate(); } }
    void Button::SetEnabled(bool enabled)
    { if (enabled_ != enabled) { enabled_ = enabled; if (!IsEnabled()) Cancel(); Invalidate(); } }
    void Button::SetOnPressed(std::function<void()> callback)
    { callback_ = std::move(callback); if (!IsEnabled()) Cancel(); Invalidate(); }
    Size Button::OnMeasure(const Constraints& c) const
    { const auto text = Detail::MeasureText(label_, 14); return c.Constrain({std::max(64.0f, text.width + 48), std::max(48.0f, text.height + 24)}); }
    void Button::BeginPress(Point position)
    {
        if (!IsEnabled()) return;
        EndPress(false);
        pressed_ = true;
        const auto r = GetBounds();
        if (ripples_.size() == 4) ripples_.erase(ripples_.begin());
        ripples_.push_back({{position.x - r.x, position.y - r.y}});
        InvalidatePaint();
    }
    void Button::EndPress(bool confirmed)
    {
        pressed_ = false;
        for (auto& ripple : ripples_) if (ripple.releaseAge < 0)
        { ripple.releaseAge = ripple.age; ripple.cancelled = !confirmed; }
        // Mark ancestors without re-entering capture/sanitization.
        MarkPaintDirty();
    }
    bool Button::OnTick(float seconds)
    {
        hoverMotion_.AnimateTo(IsEnabled() && hovered_ ? 1.f : 0.f, .12f);
        focusMotion_.AnimateTo(IsEnabled() && IsFocusVisible() ? 1.f : 0.f, .15f);
        pressMotion_.AnimateTo(IsEnabled() && pressed_ ? 1.f : 0.f, pressed_ ? .08f : .18f);
        bool animated = hoverMotion_.Tick(seconds);
        animated |= focusMotion_.Tick(seconds);
        animated |= pressMotion_.Tick(seconds);
        for (auto& ripple : ripples_)
        {
            animated |= ripple.age < .45f || ripple.releaseAge >= 0;
            ripple.age += seconds;
        }
        std::erase_if(ripples_, [](const Ripple& ripple) {
            const float start = ripple.cancelled ? ripple.releaseAge : std::max(.1f, ripple.releaseAge);
            return ripple.releaseAge >= 0 && ripple.age >= start + (ripple.cancelled ? .09f : .22f);
        });
        return animated;
    }
    void Button::PaintInk(Canvas& canvas, Rect bounds, float radius, RHIColor color) const
    {
        if (!IsEnabled()) return;
        auto state = color;
        state.a *= std::max({.10f * focusMotion_.Get(), .04f * hoverMotion_.Get(), .06f * pressMotion_.Get()});
        if (state.a > 0) canvas.RoundedRect(bounds, radius, state);
        const auto r = GetBounds();
        for (const auto& ripple : ripples_)
        {
            const Point origin{std::clamp(r.x + ripple.origin.x, bounds.x, bounds.x + bounds.width),
                std::clamp(r.y + ripple.origin.y, bounds.y, bounds.y + bounds.height)};
            const float dx = std::max(origin.x - bounds.x, bounds.x + bounds.width - origin.x);
            const float dy = std::max(origin.y - bounds.y, bounds.y + bounds.height - origin.y);
            const float t = std::clamp(ripple.age / .45f, 0.0f, 1.0f);
            const float spread = 1 - (1 - t) * (1 - t) * (1 - t);
            const float start = ripple.cancelled ? ripple.releaseAge : std::max(.1f, ripple.releaseAge);
            const float fadeTime = ripple.releaseAge < 0 ? 0 : std::clamp((ripple.age - start) / (ripple.cancelled ? .09f : .22f), 0.0f, 1.0f);
            const float fade = 1 - fadeTime * fadeTime * (3 - 2 * fadeTime);
            auto ink = color; ink.a *= .12f * std::min(1.0f, ripple.age / .04f) * fade;
            if (ink.a > 0) canvas.InkRipple(bounds, radius, origin, std::hypot(dx, dy) * spread, ink);
        }
    }
    void Button::OnPaint(Canvas& canvas) const
    {
        const auto& theme = canvas.GetTheme(); const auto r = GetBounds();
        const float inset = std::min(4.0f, r.height / 2);
        Rect visual{r.x, r.y + inset, r.width, r.height - 2 * inset};
        auto fill = IsEnabled() ? theme.primary : Mix(theme.background, theme.onSurface, .12f);
        auto foreground = IsEnabled() ? theme.onPrimary : Mix(theme.background, theme.onSurface, .38f);
        if (IsEnabled() && focusMotion_.Get() > 0)
        {
            // Ring fits inside the 48dp hit target and cannot be clipped by its parent.
            auto ring = theme.primary; ring.a *= focusMotion_.Get();
            canvas.RoundedRect({visual.x, visual.y - 3, visual.width, visual.height + 6}, 23, ring);
            canvas.RoundedRect({visual.x + 2, visual.y - 1, std::max(0.0f, visual.width - 4), visual.height + 2}, 21, theme.background);
        }
        canvas.RoundedRect(visual, 20, fill);
        PaintInk(canvas, visual, 20, theme.onPrimary);
        const auto text = Detail::MeasureText(label_, 14);
        canvas.PushClip({visual.x + std::min(8.0f, visual.width / 2), visual.y, std::max(0.0f, visual.width - 16), visual.height});
        canvas.Text({std::max(visual.x + 8, r.x + (r.width - text.width) / 2), r.y + (r.height - text.height) / 2}, label_, 14, foreground);
        canvas.PopClip();
    }
}
