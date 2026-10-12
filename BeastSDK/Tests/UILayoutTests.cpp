#include "Beast.h"
#include "Launch/ButtonScene.h"
#include <cstdio>
#include <source_location>

using namespace Beast;
using namespace Beast::UI;
namespace
{
    void Check(bool value, std::source_location line = std::source_location::current())
    { if (!value) throw std::runtime_error("UI layout assertion at line " + std::to_string(line.line())); }
    void Near(float a, float b, std::source_location line = std::source_location::current()) { Check(std::abs(a - b) < .002f, line); }
    template<class F> void Reject(F&& action)
    { bool rejected = false; try { action(); } catch (const std::logic_error&) { rejected = true; } Check(rejected); }
    class Leaf final : public Widget
    {
        Size natural_;
        Size OnMeasure(const Constraints&) const override { return natural_; }
    public:
        explicit Leaf(Size size) : natural_(size) {}
    };
    auto Box(float width, float height) { return std::make_shared<Leaf>(Size{width, height}); }
    void Place(Widget& widget, Size size, Point position = {})
    { const auto result = widget.Measure(Constraints::Tight(size)); Near(result.width, size.width); Near(result.height, size.height); widget.Layout({position.x, position.y, size.width, size.height}); }
    void FlexTests()
    {
        auto fixed = Box(30, 20), one = Box(10, 10), two = Box(10, 15);
        auto first = std::make_shared<Expanded>(one), second = std::make_shared<Expanded>(two, 2u);
        Row row({fixed, first, second}, 10);
        Place(row, {350, 100}, {5, 7});
        Near(fixed->GetBounds().x, 5); Near(fixed->GetBounds().y, 47);
        Near(first->GetBounds().width, 100); Near(second->GetBounds().width, 200);
        Near(first->GetBounds().x, 45); Near(second->GetBounds().x, 155);
        Near(one->GetBounds().width, 100); Near(two->GetBounds().height, 15); Near(row.GetOverflow(), 0);
        first->SetFlex(2); second->SetFlex(1); Place(row, {350, 100});
        Near(first->GetBounds().width, 200); Near(second->GetBounds().width, 100);
        Place(row, {20, 10}); // Non-flex child retains its natural main size, flex receives zero.
        Near(first->GetBounds().width, 0); Near(second->GetBounds().width, 0); Near(row.GetOverflow(), 30);
        Reject([&] { row.Measure({}); });
        auto wrong = std::make_shared<Expanded>(Box(10, 10));
        Reject([&] { wrong->Measure({}); });
        Container badParent(wrong); Reject([&] { badParent.Measure(Constraints::Loose({100, 100})); });
        Reject([] { Expanded invalid(Box(1, 1), 0); });
        Reject([&] { first->SetFlex(0); }); Check(first->GetFlex() == 2);
        auto verticalOne = Box(20, 1), verticalTwo = Box(40, 1);
        Column column({std::make_shared<Expanded>(verticalOne), std::make_shared<Expanded>(verticalTwo, 2u)}, 0);
        Place(column, {100, 300}); Near(verticalOne->GetBounds().height, 100); Near(verticalTwo->GetBounds().height, 200);
        Near(verticalOne->GetBounds().x, 40); Near(verticalTwo->GetBounds().y, 100);
        auto inner = std::make_shared<Row>(std::vector<WidgetPtr>{std::make_shared<Expanded>(Box(1, 1))});
        Row outer({inner}); Reject([&] { Place(outer, {100, 100}); }); // Nested flex on an unbounded main axis.
        outer.Clear(); Row bounded({std::make_shared<Expanded>(inner)}); Place(bounded, {100, 100});
        auto hugeA = std::make_shared<Expanded>(Box(1, 1), std::numeric_limits<unsigned>::max());
        auto hugeB = std::make_shared<Expanded>(Box(1, 1), std::numeric_limits<unsigned>::max());
        Row weights({hugeA, hugeB}, 0); Place(weights, {101.5f, 20}); Near(hugeA->GetBounds().width, 50.75f); Near(hugeB->GetBounds().width, 50.75f);
        for (auto axis : {Axis::Horizontal, Axis::Vertical})
            for (float extent : {0.0f, .5f, 17.3f, 100.0f, 511.25f})
                for (unsigned a : {1u, 2u, 7u}) for (unsigned b : {1u, 3u, 11u})
                {
                    auto x = std::make_shared<Expanded>(Box(0, 0), a), y = std::make_shared<Expanded>(Box(0, 0), b);
                    Flex flex(axis, {x, y}, FlexOptions{0});
                    Place(flex, axis == Axis::Horizontal ? Size{extent, 40} : Size{40, extent});
                    const float actualA = axis == Axis::Horizontal ? x->GetBounds().width : x->GetBounds().height;
                    const float actualB = axis == Axis::Horizontal ? y->GetBounds().width : y->GetBounds().height;
                    Near(actualA, extent * (float(a) / (a + b))); Near(actualA + actualB, extent);
                }
        std::puts("UI flex tests passed: both axes, ratios, zero space, overflow, nested/unbounded layout, invalid parents and large weights.");
    }
    void AlignmentTests()
    {
        const std::vector<MainAxisAlignment> modes{MainAxisAlignment::Start, MainAxisAlignment::Center, MainAxisAlignment::End,
            MainAxisAlignment::SpaceBetween, MainAxisAlignment::SpaceAround, MainAxisAlignment::SpaceEvenly};
        const float leading[]{0, 85, 170, 0, 42.5f, 170.0f / 3};
        const float secondPosition[]{30, 115, 200, 200, 157.5f, 30 + 340.0f / 3};
        for (std::size_t i = 0; i < modes.size(); ++i)
            for (auto axis : {Axis::Horizontal, Axis::Vertical})
            {
                auto first = Box(20, 20), second = Box(20, 20);
                Flex layout(axis, {first, second}, {10, MainAxisSize::Max, modes[i], CrossAxisAlignment::End});
                Place(layout, axis == Axis::Horizontal ? Size{220, 100} : Size{100, 220});
                Near(axis == Axis::Horizontal ? first->GetBounds().x : first->GetBounds().y, leading[i]);
                Near(axis == Axis::Horizontal ? second->GetBounds().x : second->GetBounds().y, secondPosition[i]);
                Near(axis == Axis::Horizontal ? first->GetBounds().y : first->GetBounds().x, 80);
            }
        for (auto mode : modes) for (int count : {0, 1})
        {
            Row row({}, FlexOptions{0, MainAxisSize::Max, mode});
            if (count) row.Add(Box(10, 10)); Place(row, {100, 100}); Near(row.GetOverflow(), 0);
        }
        auto leaf = Box(20, 10);
        Row stretch({leaf}, FlexOptions{0, MainAxisSize::Min, MainAxisAlignment::Start, CrossAxisAlignment::Stretch});
        const auto size = stretch.Measure(Constraints::Loose({200, 100})); Near(size.width, 20); Near(size.height, 100);
        Place(stretch, {200, 100}); Near(leaf->GetBounds().height, 100); Near(leaf->GetBounds().y, 0);
        Reject([&] { stretch.Measure({}); });
        auto options = stretch.GetOptions(); options.spacing = -1; Reject([&] { stretch.SetOptions(options); }); Near(stretch.GetOptions().spacing, 0);
        options = stretch.GetOptions(); options.crossAxisAlignment = static_cast<CrossAxisAlignment>(99); Reject([&] { stretch.SetOptions(options); });
        Column legacy({Box(20, 10), Box(30, 40)}, 12);
        const auto oldSize = legacy.Measure({}); Near(oldSize.width, 30); Near(oldSize.height, 62);
        Row shrink({Box(20, 10), Box(30, 40)}, 12);
        const auto minSize = shrink.Measure(Constraints::Loose({100, 100})); Near(minSize.width, 62); Near(minSize.height, 40);
        std::puts("UI alignment tests passed: six main alignments, stretch/end, empty/single child, shrink-wrap and legacy Column defaults.");
    }
    void ContainerTests()
    {
        auto leaf = Box(20, 10);
        ContainerStyle style; style.padding = {3, 5, 7, 11};
        Container container(leaf, style);
        const auto natural = container.Measure({}); Near(natural.width, 30); Near(natural.height, 26);
        container.Layout({10, 20, natural.width, natural.height}); Near(leaf->GetBounds().x, 13); Near(leaf->GetBounds().y, 25);
        style.width = 200.0f; style.height = 100.0f; style.alignment = Alignment::BottomRight; container.SetStyle(style);
        const auto explicitSize = container.Measure({}); Near(explicitSize.width, 200); Near(explicitSize.height, 100);
        container.Layout({0, 0, 200, 100}); Near(leaf->GetBounds().x, 173); Near(leaf->GetBounds().y, 79);
        for (int i = 0; i < 9; ++i)
        {
            style.alignment = static_cast<Alignment>(i); container.SetStyle(style); Place(container, {200, 100});
            Near(leaf->GetBounds().x, 3 + (i % 3) * 85.0f); Near(leaf->GetBounds().y, 5 + (i / 3) * 37.0f);
        }
        style.constraints.maxWidth = 80; container.SetStyle(style); Near(container.Measure({}).width, 80);
        Place(container, {40, 30}); Near(container.GetBounds().width, 40); // Parent wins over requested 200/min/max.
        Place(container, {300, 300}); Near(container.GetBounds().width, 300);
        style.padding = {100, 100, 300, 300}; container.SetStyle(style); Place(container, {40, 20});
        Near(leaf->GetBounds().width, 0); Near(leaf->GetBounds().height, 0); Near(leaf->GetBounds().x, 10); Near(leaf->GetBounds().y, 5);
        auto invalid = style; invalid.cornerRadius = -1; Reject([&] { container.SetStyle(invalid); });
        invalid = style; invalid.padding.left = std::numeric_limits<float>::quiet_NaN(); Reject([&] { container.SetStyle(invalid); });
        invalid = style; invalid.constraints.minWidth = 1000; Reject([&] { container.SetStyle(invalid); });
        invalid = style; invalid.background = RHIColor{2, 0, 0, 1}; Reject([&] { container.SetStyle(invalid); });
        Container empty; auto emptySize = empty.Measure({}); Near(emptySize.width, 0); Near(emptySize.height, 0);
        emptySize = empty.Measure(Constraints::Loose({200, 100})); Near(emptySize.width, 200); Near(emptySize.height, 100);
        std::puts("UI container tests passed: padding, nine alignments, explicit sizes/limits, parent precedence, empty and zero content, rejected styles.");
    }
    void ReflowInputTests()
    {
        Context context; int clicks = 0;
        auto button = std::make_shared<Button>("First", [&] { ++clicks; });
        auto container = std::make_shared<Container>(button);
        auto first = std::make_shared<Expanded>(container), second = std::make_shared<Expanded>(std::make_shared<Button>("Second", [] {}));
        context.SetRoot(std::make_shared<Row>(std::vector<WidgetPtr>{first, second}, 0.0f));
        Window window; window.width = window.contentWidth = 300; window.height = window.contentHeight = 100;
        context.BuildFrame(window, 0);
        auto pointer = [&](EventType type) { Event e; e.type = type; e.x = 50; e.y = 50; context.HandleEvent(e, window); };
        pointer(EventType::MouseDown); Check(button->IsPressed()); first->SetFlex(2);
        pointer(EventType::MouseUp); Check(clicks == 0 && !button->IsPressed());
        pointer(EventType::MouseDown);
        auto style = container->GetStyle(); style.background = RHIColor{.1f, .2f, .3f, 1}; container->SetStyle(style);
        pointer(EventType::MouseUp); Check(clicks == 1); // Paint-only style change must not cancel a stationary target.
        context.HandleEvent(Event{EventType::FocusLost}, window);
        Event key; key.type = EventType::KeyDown; key.key = Key::Tab; context.HandleEvent(key, window);
        key.key = Key::Space; context.HandleEvent(key, window); Check(button->IsPressed()); first->SetFlex(1);
        key.type = EventType::KeyUp; context.HandleEvent(key, window); Check(clicks == 1 && !button->IsPressed());
        std::puts("UI reflow tests passed: pointer/keyboard press canceled on geometry change, retained across color-only changes.");
    }
    void DemoViewportTests()
    {
        for (float scale : {1.0f, 2.625f})
            for (float width : {96.0f, 320.0f, 412.0f, 560.0f, 1920.0f})
                for (float height : {120.0f, 480.0f, 960.0f})
                {
                    ButtonScene scene; Window window; window.scale = scale;
                    window.width = window.contentWidth = static_cast<int>(std::lround(width * scale));
                    window.height = window.contentHeight = static_cast<int>(std::lround(height * scale));
                    scene.ui.BuildFrame(window, 0);
                    Near(scene.leftPane->GetBounds().width * 2, scene.rightPane->GetBounds().width);
                    Near(scene.primary->GetBounds().width, scene.toggle->GetBounds().width);
                    scene.leftPane->SetFlex(2); scene.rightPane->SetFlex(1); scene.ui.BuildFrame(window, 0);
                    Near(scene.leftPane->GetBounds().width, scene.rightPane->GetBounds().width * 2);
                }
        std::puts("UI demo viewport tests passed: 30 layouts, narrow/clipped windows, wide windows, fractional DPI, live ratios and equal button widths.");
    }
}
int main()
{
    try { FlexTests(); AlignmentTests(); ContainerTests(); ReflowInputTests(); DemoViewportTests(); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
