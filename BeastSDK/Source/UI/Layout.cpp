#include "UI/UI.h"
#include "UI/Canvas.h"

namespace Beast::UI
{
    namespace
    {
        constexpr float Infinite = std::numeric_limits<float>::infinity();
        float Finite(double value)
        {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                throw std::invalid_argument("UI layout size/position overflow.");
            return static_cast<float>(value);
        }
        void Nonnegative(float value)
        { if (!std::isfinite(value) || value < 0) throw std::invalid_argument("UI layout dimensions must be finite and nonnegative."); }
        void Validate(FlexOptions options)
        {
            Nonnegative(options.spacing);
            if (options.mainAxisSize != MainAxisSize::Min && options.mainAxisSize != MainAxisSize::Max)
                throw std::invalid_argument("Invalid UI main-axis size mode.");
            switch (options.mainAxisAlignment)
            {
            case MainAxisAlignment::Start: case MainAxisAlignment::Center: case MainAxisAlignment::End:
            case MainAxisAlignment::SpaceBetween: case MainAxisAlignment::SpaceAround: case MainAxisAlignment::SpaceEvenly: break;
            default: throw std::invalid_argument("Invalid UI main-axis alignment.");
            }
            switch (options.crossAxisAlignment)
            {
            case CrossAxisAlignment::Start: case CrossAxisAlignment::Center: case CrossAxisAlignment::End: case CrossAxisAlignment::Stretch: break;
            default: throw std::invalid_argument("Invalid UI cross-axis alignment.");
            }
        }
        void Validate(const ContainerStyle& style)
        {
            style.constraints.Validate();
            for (auto size : {style.width, style.height}) if (size) Nonnegative(*size);
            for (float inset : {style.padding.left, style.padding.top, style.padding.right, style.padding.bottom}) Nonnegative(inset);
            Finite(double(style.padding.left) + style.padding.right); Finite(double(style.padding.top) + style.padding.bottom);
            Nonnegative(style.cornerRadius);
            Nonnegative(style.borderWidth); Nonnegative(style.shadowBlur);
            if (style.shadowBlur > 128 || !std::isfinite(style.shadowOffset.x) || !std::isfinite(style.shadowOffset.y))
                throw std::invalid_argument("Invalid UI shadow.");
            std::vector<RHIColor> colors{style.borderColor, style.shadowColor};
            if (style.gradient)
            {
                const auto g = *style.gradient; colors.push_back(g.from); colors.push_back(g.to);
                for (float v : {g.begin.x, g.begin.y, g.end.x, g.end.y})
                    if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Invalid UI gradient.");
                if (g.begin.x == g.end.x && g.begin.y == g.end.y) throw std::invalid_argument("Gradient endpoints must differ.");
            }
            for (const auto color : colors) for (float v : {color.r, color.g, color.b, color.a})
                if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Invalid UI decoration color.");
            if (style.background) for (float v : {style.background->r, style.background->g, style.background->b, style.background->a})
                if (!std::isfinite(v) || v < 0 || v > 1) throw std::invalid_argument("Invalid UI container color.");
            if (style.alignment && (*style.alignment < Alignment::TopLeft || *style.alignment > Alignment::BottomRight))
                throw std::invalid_argument("Invalid UI container alignment.");
        }
        float Main(Size size, Axis axis) { return axis == Axis::Horizontal ? size.width : size.height; }
        float Cross(Size size, Axis axis) { return axis == Axis::Horizontal ? size.height : size.width; }
        Size Axes(float main, float cross, Axis axis) { return axis == Axis::Horizontal ? Size{main, cross} : Size{cross, main}; }
        Constraints ChildConstraints(float minMain, float maxMain, float minCross, float maxCross, Axis axis)
        { return axis == Axis::Horizontal ? Constraints{minMain, maxMain, minCross, maxCross} : Constraints{minCross, maxCross, minMain, maxMain}; }
        Constraints ContainerConstraints(const ContainerStyle& style, const Constraints& parent)
        {
            auto own = style.constraints;
            if (style.width) own.minWidth = own.maxWidth = std::clamp(*style.width, own.minWidth, own.maxWidth);
            if (style.height) own.minHeight = own.maxHeight = std::clamp(*style.height, own.minHeight, own.maxHeight);
            // Ancestor constraints always win, including conflicting min/max sizes.
            return {std::clamp(own.minWidth, parent.minWidth, parent.maxWidth), std::clamp(own.maxWidth, parent.minWidth, parent.maxWidth),
                std::clamp(own.minHeight, parent.minHeight, parent.maxHeight), std::clamp(own.maxHeight, parent.minHeight, parent.maxHeight)};
        }
    }
    struct Flex::Plan
    {
        Size size;
        std::vector<Size> children;
        float occupied = 0;
    };
    Flex::Flex(Axis axis, std::vector<WidgetPtr> children, FlexOptions options) : axis_(axis), options_(options)
    {
        if (axis != Axis::Horizontal && axis != Axis::Vertical) throw std::invalid_argument("Invalid UI flex axis.");
        Validate(options); for (auto& child : children) Adopt(std::move(child));
    }
    void Flex::SetOptions(FlexOptions options) { Validate(options); options_ = options; Invalidate(); }
    Flex::Plan Flex::MeasureChildren(const Constraints& c) const
    {
        const float maxMain = Main({c.maxWidth, c.maxHeight}, axis_), maxCross = Cross({c.maxWidth, c.maxHeight}, axis_);
        const bool stretch = options_.crossAxisAlignment == CrossAxisAlignment::Stretch;
        if (stretch && !Children().empty() && !std::isfinite(maxCross))
            throw std::invalid_argument("Flex Stretch needs a bounded cross axis; give the Row/Column a width/height.");
        Plan plan; plan.children.resize(Children().size());
        double used = Children().empty() ? 0 : double(options_.spacing) * (Children().size() - 1), totalFlex = 0;
        float crossSize = 0;
        for (std::size_t i = 0; i < Children().size(); ++i)
        {
            if (const auto* expanded = dynamic_cast<const Expanded*>(Children()[i].get())) totalFlex += expanded->GetFlex();
            else
            {
                const auto size = Children()[i]->Measure(ChildConstraints(0, Infinite, stretch ? maxCross : 0, maxCross, axis_));
                plan.children[i] = size; used += Main(size, axis_); crossSize = std::max(crossSize, Cross(size, axis_));
            }
        }
        if (totalFlex && !std::isfinite(maxMain))
            throw std::invalid_argument("Expanded needs a bounded main axis; wrap the nested Row/Column in Expanded or give it a finite size.");
        if (totalFlex)
        {
            const double remaining = std::max(0.0, double(maxMain) - used);
            double weight = 0; float previous = 0;
            for (std::size_t i = 0; i < Children().size(); ++i)
                if (const auto* expanded = dynamic_cast<const Expanded*>(Children()[i].get()))
                {
                    weight += expanded->GetFlex();
                    const float boundary = Finite(remaining * weight / totalFlex), allocated = std::max(0.0f, boundary - previous);
                    previous = boundary;
                    const auto size = Children()[i]->Measure(ChildConstraints(allocated, allocated, stretch ? maxCross : 0, maxCross, axis_));
                    plan.children[i] = size; used += Main(size, axis_); crossSize = std::max(crossSize, Cross(size, axis_));
                }
        }
        plan.occupied = Finite(used);
        const float mainSize = (totalFlex || options_.mainAxisSize == MainAxisSize::Max) && std::isfinite(maxMain) ? maxMain : plan.occupied;
        plan.size = c.Constrain(Axes(mainSize, crossSize, axis_));
        return plan;
    }
    Size Flex::OnMeasure(const Constraints& c) const { return MeasureChildren(c).size; }
    void Flex::OnLayout()
    {
        const auto r = GetBounds(); const auto plan = MeasureChildren(Constraints::Tight({r.width, r.height}));
        const auto count = Children().size(); const float main = Main(plan.size, axis_), cross = Cross(plan.size, axis_);
        overflow_ = std::max(0.0f, plan.occupied - main);
        const double free = std::max(0.0, double(main) - plan.occupied);
        double leading = 0, gap = options_.spacing;
        switch (options_.mainAxisAlignment)
        {
        case MainAxisAlignment::Center: leading = free / 2; break;
        case MainAxisAlignment::End: leading = free; break;
        case MainAxisAlignment::SpaceBetween: if (count > 1) gap += free / (count - 1); break;
        case MainAxisAlignment::SpaceAround: if (count) { leading = free / (2 * count); gap += free / count; } break;
        case MainAxisAlignment::SpaceEvenly: if (count) { leading = free / (count + 1); gap += leading; } break;
        default: break;
        }
        double position = leading;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto size = plan.children[i]; const float spareCross = std::max(0.0f, cross - Cross(size, axis_));
            const float offset = options_.crossAxisAlignment == CrossAxisAlignment::Center ? spareCross / 2 :
                options_.crossAxisAlignment == CrossAxisAlignment::End ? spareCross : 0;
            const auto xy = Axes(Finite(position), offset, axis_);
            Children()[i]->Layout({Finite(double(r.x) + xy.width), Finite(double(r.y) + xy.height), size.width, size.height});
            position += Main(size, axis_) + gap;
        }
    }
    Expanded::Expanded(WidgetPtr child, unsigned flex) : flex_(flex)
    { if (!flex) throw std::invalid_argument("Expanded flex must be positive."); Adopt(std::move(child)); }
    void Expanded::SetFlex(unsigned flex)
    { if (!flex) throw std::invalid_argument("Expanded flex must be positive."); if (flex_ != flex) { flex_ = flex; Invalidate(); } }
    void Expanded::RequireFlexParent() const
    { if (!dynamic_cast<const Flex*>(Parent())) throw std::invalid_argument("Expanded must be a direct child of Row, Column or Flex."); }
    Size Expanded::OnMeasure(const Constraints& c) const { RequireFlexParent(); return Children().front()->Measure(c); }
    void Expanded::OnLayout() { RequireFlexParent(); Children().front()->Layout(GetBounds()); }

    Container::Container(WidgetPtr child, ContainerStyle style) : style_(style)
    { Validate(style); if (child) Adopt(std::move(child)); }
    void Container::SetStyle(ContainerStyle style) { Validate(style); style_ = style; Invalidate(); }
    Size Container::OnMeasure(const Constraints& parent) const
    {
        const auto c = ContainerConstraints(style_, parent); const auto p = style_.padding;
        const float horizontal = p.left + p.right, vertical = p.top + p.bottom;
        Size size{horizontal, vertical};
        if (!Children().empty())
        {
            const auto child = Children().front()->Measure({style_.alignment ? 0 : std::max(0.0f, c.minWidth - horizontal), std::max(0.0f, c.maxWidth - horizontal),
                style_.alignment ? 0 : std::max(0.0f, c.minHeight - vertical), std::max(0.0f, c.maxHeight - vertical)});
            size = {Finite(double(child.width) + horizontal), Finite(double(child.height) + vertical)};
        }
        const auto own = style_.constraints;
        const bool ownSize = style_.width || style_.height || own.minWidth || own.minHeight || std::isfinite(own.maxWidth) || std::isfinite(own.maxHeight);
        if (style_.alignment || (Children().empty() && !ownSize))
        {
            if (std::isfinite(c.maxWidth)) size.width = c.maxWidth;
            if (std::isfinite(c.maxHeight)) size.height = c.maxHeight;
        }
        return c.Constrain(size);
    }
    void Container::OnLayout()
    {
        if (Children().empty()) return;
        const auto r = GetBounds(); const auto p = style_.padding;
        const float horizontal = p.left + p.right, vertical = p.top + p.bottom;
        // If padding alone exceeds the available size, collapse content to zero
        // and proportionally reduce opposing insets; never place it outside bounds.
        const float left = horizontal > r.width ? (p.left / horizontal) * r.width : p.left;
        const float top = vertical > r.height ? (p.top / vertical) * r.height : p.top;
        Rect content{r.x + left, r.y + top, std::max(0.0f, r.width - horizontal), std::max(0.0f, r.height - vertical)};
        if (style_.alignment)
        {
            const auto size = Children().front()->Measure(Constraints::Loose({content.width, content.height}));
            const int alignment = static_cast<int>(*style_.alignment);
            content.x += (content.width - size.width) * ((alignment % 3) * .5f);
            content.y += (content.height - size.height) * ((alignment / 3) * .5f);
            content.width = size.width; content.height = size.height;
        }
        Children().front()->Layout(content);
    }
    void Container::OnPaint(Canvas& canvas) const
    {
        const auto rect = GetBounds(); const float radius = style_.cornerRadius;
        if (style_.gradient) canvas.GradientRect(rect, radius, style_.gradient->from, style_.gradient->to, style_.gradient->begin, style_.gradient->end, style_.borderWidth, style_.borderColor);
        else canvas.RoundedRect(rect, radius, style_.background.value_or(RHIColor{0, 0, 0, 0}), style_.borderWidth, style_.borderColor);
        Widget::OnPaint(canvas); // Corner radius decorates the background; children retain rectangular clipping.
    }
    void Container::OnPaintBehind(Canvas& canvas) const
    {
        if (style_.shadowColor.a > 0) canvas.Shadow(GetBounds(), style_.cornerRadius, style_.shadowBlur, style_.shadowOffset, style_.shadowColor);
    }
    Rect Container::GetPaintBounds() const
    {
        const auto r = GetBounds(); const float spread = style_.shadowBlur * 2;
        auto result = Widget::GetPaintBounds();
        if (style_.shadowColor.a > 0) result = Unite(result, {r.x + style_.shadowOffset.x - spread,
            r.y + style_.shadowOffset.y - spread, r.width + spread * 2, r.height + spread * 2});
        return result;
    }
}
