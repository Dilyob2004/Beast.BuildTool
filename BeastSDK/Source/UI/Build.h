#pragma once
#include "UI/UI.h"

// Optional short builders. They preserve the ordinary typed shared_ptr API;
// no hidden ownership, reflection or application-specific styles.
namespace Beast::UI::Build
{
    template<class T, class... Args> auto Make(Args&&... args)
    { return std::make_shared<T>(std::forward<Args>(args)...); }
    inline auto Box(WidgetPtr child, ContainerStyle style = {}) { return Make<Container>(std::move(child), style); }
    inline auto Space(float height) { ContainerStyle s; s.height = height; return Box(nullptr, s); }
    inline auto Inset(WidgetPtr child, float horizontal, float vertical = 0)
    { ContainerStyle s; s.padding = EdgeInsets::Symmetric(horizontal, vertical); return Box(std::move(child), s); }
    // Negative dimension means unconstrained, as in the original sample builders.
    inline auto Sized(WidgetPtr child, float width, float height)
    { ContainerStyle s; if (width >= 0) s.width = width; if (height >= 0) s.height = height; return Box(std::move(child), s); }
    inline auto Centered(WidgetPtr child) { return Make<Center>(std::move(child)); }
    inline auto Cached(WidgetPtr child) { return Make<RepaintBoundary>(std::move(child)); }
    inline auto Fill(WidgetPtr child, unsigned weight = 1) { return Make<Expanded>(std::move(child), weight); }
    inline auto VBox(std::vector<WidgetPtr> children, float gap = 0, CrossAxisAlignment cross = CrossAxisAlignment::Stretch)
    { return Make<Column>(std::move(children), FlexOptions{gap, MainAxisSize::Min, MainAxisAlignment::Start, cross}); }
    inline auto HBox(std::vector<WidgetPtr> children, float gap = 12) { return Make<Row>(std::move(children), gap); }
    // A bounded full-height screen: optional header, flexible body, bottom bar.
    inline auto Scaffold(WidgetPtr body, WidgetPtr bottomBar = {}, WidgetPtr header = {}, float maxWidth = std::numeric_limits<float>::infinity())
    {
        std::vector<WidgetPtr> children;
        if (header) children.push_back(std::move(header));
        children.push_back(Fill(std::move(body)));
        if (bottomBar) children.push_back(std::move(bottomBar));
        ContainerStyle limit; limit.constraints.maxWidth = maxWidth;
        return Centered(Box(VBox(std::move(children)), limit));
    }
}
