#include "UI/Canvas.h"

namespace Beast::UI
{
    ClipRRect::ClipRRect(WidgetPtr child, float radius) : radius_(radius)
    {
        if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid clip radius.");
        Adopt(std::move(child));
    }
    Size ClipRRect::OnMeasure(const Constraints& c) const { return Children().empty() ? Size{} : Children().front()->Measure(c); }
    void ClipRRect::OnLayout() { if (!Children().empty()) Children().front()->Layout(GetBounds()); }
    void ClipRRect::OnPaint(Canvas& canvas) const
    { canvas.Save(); canvas.ClipRRect(GetBounds(), radius_); Widget::OnPaint(canvas); canvas.Restore(); }
    bool ClipRRect::ContainsPoint(Point point) const noexcept
    {
        const auto r = GetBounds(); if (!r.Contains(point)) return false;
        const float radius = std::min(radius_, std::min(r.width, r.height) / 2);
        const float dx = std::max(std::abs(point.x - (r.x + r.width / 2)) - (r.width / 2 - radius), 0.f);
        const float dy = std::max(std::abs(point.y - (r.y + r.height / 2)) - (r.height / 2 - radius), 0.f);
        return dx * dx + dy * dy <= radius * radius;
    }
    Opacity::Opacity(WidgetPtr child, float opacity) : opacity_(opacity)
    { SetOpacity(opacity); Adopt(std::move(child)); }
    void Opacity::SetOpacity(float opacity)
    {
        if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) throw std::invalid_argument("Opacity must be in [0,1].");
        motion_.Set(opacity);
        if (opacity_ != opacity) { opacity_ = opacity; InvalidatePaint(); }
    }
    void Opacity::AnimateTo(float opacity, float seconds)
    {
        if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) throw std::invalid_argument("Opacity must be in [0,1].");
        motion_.AnimateTo(opacity, seconds);
        if (seconds == 0) { opacity_ = motion_.Get(); InvalidatePaint(); }
    }
    bool Opacity::OnTick(float seconds)
    {
        const bool childChanged = Widget::OnTick(seconds);
        const bool changed = motion_.Tick(seconds);
        opacity_ = motion_.Get();
        return childChanged || changed;
    }
    Size Opacity::OnMeasure(const Constraints& c) const { return Children().empty() ? Size{} : Children().front()->Measure(c); }
    void Opacity::OnLayout() { if (!Children().empty()) Children().front()->Layout(GetBounds()); }
    void Opacity::OnPaint(Canvas& canvas) const
    {
        if (opacity_ == 0) return;
        // Keep the same explicit effect clip at opacity=1 without allocating a target.
        if (opacity_ == 1) { canvas.PushClip(GetBounds()); Widget::OnPaint(canvas); canvas.PopClip(); return; }
        canvas.SaveLayer(GetBounds(), opacity_); Widget::OnPaint(canvas); canvas.Restore();
    }
}
