#pragma once
#include "UI/Geometry.h"
#include "UI/Motion.h"
#include "UI/TextEditing.h"
#include "Core/Window.h"
#include "Input/Event.h"
#include "RHI/RHI.h"
#include <functional>
#include <optional>
#include <string>
#include <atomic>

namespace Beast::UI
{
    class Canvas;
    class Context;
    struct TextCacheStats
    {
        std::uint64_t glyphHits = 0, glyphMisses = 0, runHits = 0, runMisses = 0;
        // Includes copy-on-write page versions; sealedBytes is CPU payload,
        // not measured GPU upload traffic. Live snapshots can retain old versions.
        std::uint64_t pagesCreated = 0, sealedBytes = 0, evictions = 0, fallbackLines = 0;
        std::uint64_t pageBytes = 0, runBytes = 0;
        std::size_t pages = 0, glyphs = 0, runs = 0;
    };
    struct Theme
    {
        RHIColor background{0.98f, 0.97f, 0.99f, 1};
        RHIColor primary{0.404f, 0.314f, 0.643f, 1};
        RHIColor onPrimary{1, 1, 1, 1};
        RHIColor onSurface{0.11f, 0.105f, 0.125f, 1};
    };
    class Widget;
    using WidgetPtr = std::shared_ptr<Widget>;
    // Retained, single-parent tree. Construct nodes using shared_ptr/make_shared.
    // All mutations, layout, input and painting run on the application's Run thread.
    class Widget : public std::enable_shared_from_this<Widget>
    {
        friend class Context;
        friend class ScrollView;
        friend class Canvas;
        friend class RepaintBoundary;
        Widget* parent_ = nullptr;
        Context* owner_ = nullptr;
        Rect bounds_;
        std::vector<WidgetPtr> children_;
        std::uint64_t paintRevision_ = 1;
        inline static std::atomic<int> nextSemanticId_{1};
        const int semanticId_ = nextSemanticId_.fetch_add(1);
        std::string accessibilityLabel_;
        void Attach(Context* owner);
        void Translate(Point delta);
    protected:
        void MarkPaintDirty() noexcept;
        void Adopt(WidgetPtr child);
        void ClearChildren();
        const std::vector<WidgetPtr>& Children() const noexcept { return children_; }
        const Widget* Parent() const noexcept { return parent_; }
        void Invalidate();
        void InvalidatePaint(bool moved = false, bool redraw = true);
        virtual Size OnMeasure(const Constraints& constraints) const = 0;
        virtual void OnLayout() {}
        virtual void OnPaintBehind(Canvas&) const {}
        virtual void OnPaint(Canvas& canvas) const;
        virtual bool OnTick(float seconds);
    public:
        Widget() = default;
        virtual ~Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Size Measure(const Constraints& constraints) const;
        void Layout(Rect bounds);
        void Paint(Canvas& canvas) const;
        bool Tick(float seconds);
        Rect GetBounds() const noexcept { return bounds_; }
        void SetAccessibilityLabel(std::string label) { TextEditor::Boundaries(label); accessibilityLabel_ = std::move(label); InvalidatePaint(); }
        // Conservative visual bounds in layout coordinates. Custom widgets that
        // paint shadows outside their box must include that overflow here.
        virtual Rect GetPaintBounds() const;
        virtual bool ContainsPoint(Point point) const noexcept { return bounds_.Contains(point); }
    };
    class Text final : public Widget
    {
        std::string text_;
        float fontSize_;
        std::optional<RHIColor> color_;
        Size OnMeasure(const Constraints&) const override;
        void OnPaint(Canvas&) const override;
    public:
        explicit Text(std::string text, float fontSize = 16, std::optional<RHIColor> color = {});
        void SetText(std::string text);
        const std::string& GetText() const noexcept { return text_; }
    };
    enum class TextEditAction { Copy, Cut, Paste, SelectAll };
    struct TextFieldOptions
    {
        std::string placeholder;
        float fontSize = 16;
        std::size_t maxBytes = 4096;
        bool readOnly = false;
        bool multiline = false;
        bool password = false;
        unsigned minLines = 1, maxLines = 6;
        std::function<std::optional<std::string>(const std::string&)> validator;
    };
    // Shaped Unicode editor with visual bidi carets, wrapping and platform IME.
    class TextField final : public Widget
    {
        friend class Context;
        TextEditor editor_;
        TextFieldOptions options_;
        std::function<void(const std::string&)> changed_, submitted_;
        bool focused_ = false, caretVisible_ = true;
        float blink_ = 0;
        mutable float scroll_ = 0;
        mutable std::string metricsText_;
        struct FieldLine { std::size_t start, end, next; std::string display; std::vector<std::size_t> offsets; std::vector<float> carets; float y, height, width; };
        mutable std::vector<FieldLine> lines_;
        mutable float metricsWidth_ = -1, scrollY_ = 0;
        std::optional<std::string> error_;
        std::string Display(const std::string&) const;
        std::pair<std::size_t, std::size_t> CaretIndex(std::size_t) const;
        float ContentHeight(float width) const;
        void MoveVisual(Key key, bool extend);
        void Metrics() const;
        void EnsureVisible() const;
        void Refresh(bool textChanged);
        void Focus(bool focused);
        void PlaceCaret(Point point, bool extend);
        std::size_t HitOffset(Point point) const;
        void SelectWord(Point point);
        Point HandlePoint(bool anchor) const;
        void DragHandle(Point point, bool anchor);
        bool touchSelection_ = false;
        bool Input(const Event&, bool shift, bool control);
        bool ApplyIme(const NativeTextState& state);
        Size OnMeasure(const Constraints&) const override;
        void OnPaint(Canvas&) const override;
        bool OnTick(float seconds) override;
    public:
        explicit TextField(std::string text = {}, TextFieldOptions options = {});
        const std::string& GetText() const noexcept { return editor_.GetValue().text; }
        TextSelection GetSelection() const noexcept { return editor_.GetValue().selection; }
        bool IsFocused() const noexcept { return focused_; }
        bool IsReadOnly() const noexcept { return options_.readOnly; }
        bool IsPassword() const noexcept { return options_.password; }
        bool IsMultiline() const noexcept { return options_.multiline; }
        const std::optional<std::string>& GetError() const noexcept { return error_; }
        bool Validate();
        void SetText(std::string text);
        void SetSelection(TextSelection selection);
        bool Edit(TextEditAction action, Clipboard* clipboard = nullptr);
        void SetOnChanged(std::function<void(const std::string&)> callback) { changed_ = std::move(callback); }
        void SetOnSubmitted(std::function<void(const std::string&)> callback) { submitted_ = std::move(callback); }
    };
    enum class TextAlign { Start, Center, End };
    struct ParagraphStyle
    {
        float fontSize = 14, lineHeight = 1.4f;
        FontFace face = FontFace::Regular;
        std::optional<RHIColor> color;
        TextAlign align = TextAlign::Start;
        unsigned maxLines = 0; // Zero means unlimited, within the 4096-byte text limit.
        bool ellipsis = false;
    };
    class Paragraph final : public Widget
    {
        std::string text_;
        ParagraphStyle style_;
        mutable float measuredWidth_ = -1;
        mutable std::vector<std::string> lines_;
        void Wrap(float width) const;
        Size OnMeasure(const Constraints&) const override;
        void OnPaint(Canvas&) const override;
    public:
        explicit Paragraph(std::string text, ParagraphStyle style = {});
        void SetText(std::string text);
        const std::string& GetText() const noexcept { return text_; }
        const ParagraphStyle& GetStyle() const noexcept { return style_; }
    };
    class Icon final : public Widget
    {
        std::string glyph_;
        float size_;
        RHIColor color_;
        Size OnMeasure(const Constraints&) const override;
        void OnPaint(Canvas&) const override;
    public:
        Icon(char32_t codepoint, float size, RHIColor color);
    };
    class Padding final : public Widget
    {
        float inset_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
    public:
        Padding(float inset, WidgetPtr child);
    };
    class Center final : public Widget
    {
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
    public:
        explicit Center(WidgetPtr child) { Adopt(std::move(child)); }
    };
    enum class Axis { Horizontal, Vertical };
    enum class ScrollPhysics { Bouncing, Clamping };
    enum class ScrollRasterization { PixelAligned, Subpixel };
    class ScrollView final : public Widget
    {
        friend class Context;
        Axis axis_;
        ScrollPhysics physics_;
        ScrollRasterization rasterization_ = ScrollRasterization::PixelAligned;
        float offset_ = 0, extent_ = 0, velocity_ = 0;
        float viewport_ = 0, springTarget_ = 0;
        double releaseTimestamp_ = 0;
        float firstTickSeconds_ = -1; // Context limits only physics, not child animations.
        mutable float rasterScale_ = 0; // Effective physical scale from the last paint.
        bool VisibleMovement(float before, float after) const;
        bool dragging_ = false, springing_ = false;
        void MoveTo(float offset);
        Size ContentSize(Size viewport) const;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
        bool OnTick(float seconds) override;
    public:
        explicit ScrollView(WidgetPtr child, Axis axis = Axis::Vertical, ScrollPhysics physics = ScrollPhysics::Bouncing);
        Axis GetAxis() const noexcept { return axis_; }
        float GetOffset() const noexcept { return offset_; }
        float GetExtent() const noexcept { return extent_; }
        ScrollPhysics GetPhysics() const noexcept { return physics_; }
        ScrollRasterization GetRasterization() const noexcept { return rasterization_; }
        void SetRasterization(ScrollRasterization mode);
        Rect GetPaintBounds() const override { return GetBounds(); }
        bool CanDrag() const noexcept { return extent_ > 0 || physics_ == ScrollPhysics::Bouncing; }
        // Programmatic offsets and wheel scrolling remain bounded. Dragging
        // uses resistance outside the bounds, then a spring on release.
        bool ScrollBy(float delta);
        void SetOffset(float offset);
        void BeginDrag();
        bool DragBy(float delta);
        void Stop();
        void Fling(float velocity);
    };
    enum class MainAxisSize { Min, Max };
    enum class MainAxisAlignment { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
    enum class CrossAxisAlignment { Start, Center, End, Stretch };
    struct FlexOptions
    {
        float spacing = 12;
        MainAxisSize mainAxisSize = MainAxisSize::Min; // Preserve the original Column defaults.
        MainAxisAlignment mainAxisAlignment = MainAxisAlignment::Start;
        CrossAxisAlignment crossAxisAlignment = CrossAxisAlignment::Center;
    };
    class Flex : public Widget
    {
        Axis axis_;
        FlexOptions options_;
        float overflow_ = 0;
        struct Plan;
        Plan MeasureChildren(const Constraints&) const;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
    public:
        Flex(Axis axis, std::vector<WidgetPtr> children = {}, FlexOptions options = {});
        void Add(WidgetPtr child) { Adopt(std::move(child)); }
        void Clear() { ClearChildren(); }
        Axis GetAxis() const noexcept { return axis_; }
        const FlexOptions& GetOptions() const noexcept { return options_; }
        void SetOptions(FlexOptions options);
        float GetOverflow() const noexcept { return overflow_; } // Main-axis overflow after Layout, in dp.
    };
    class Row final : public Flex
    {
    public:
        explicit Row(std::vector<WidgetPtr> children = {}, float spacing = 12)
            : Flex(Axis::Horizontal, std::move(children), FlexOptions{spacing}) {}
        Row(std::vector<WidgetPtr> children, FlexOptions options) : Flex(Axis::Horizontal, std::move(children), options) {}
    };
    class Column final : public Flex
    {
    public:
        explicit Column(std::vector<WidgetPtr> children = {}, float spacing = 12)
            : Flex(Axis::Vertical, std::move(children), FlexOptions{spacing}) {}
        Column(std::vector<WidgetPtr> children, FlexOptions options) : Flex(Axis::Vertical, std::move(children), options) {}
    };
    class Expanded final : public Widget
    {
        unsigned flex_;
        void RequireFlexParent() const;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
    public:
        explicit Expanded(WidgetPtr child, unsigned flex = 1);
        unsigned GetFlex() const noexcept { return flex_; }
        void SetFlex(unsigned flex);
    };
    struct EdgeInsets
    {
        float left = 0, top = 0, right = 0, bottom = 0;
        static EdgeInsets All(float value) { return {value, value, value, value}; }
        static EdgeInsets Symmetric(float horizontal, float vertical) { return {horizontal, vertical, horizontal, vertical}; }
    };
    enum class Alignment { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight };
    struct ContainerStyle
    {
        std::optional<float> width, height;
        Constraints constraints;
        EdgeInsets padding;
        std::optional<RHIColor> background;
        float cornerRadius = 0;
        std::optional<Alignment> alignment;
        struct Gradient { RHIColor from, to; Point begin{0, 0}, end{1, 1}; };
        std::optional<Gradient> gradient;
        RHIColor borderColor{0, 0, 0, 0};
        float borderWidth = 0;
        RHIColor shadowColor{0, 0, 0, 0};
        float shadowBlur = 0;
        Point shadowOffset;
    };
    class Container final : public Widget
    {
        ContainerStyle style_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
        void OnPaintBehind(Canvas&) const override;
    public:
        explicit Container(WidgetPtr child = {}, ContainerStyle style = {});
        const ContainerStyle& GetStyle() const noexcept { return style_; }
        void SetStyle(ContainerStyle style);
        Rect GetPaintBounds() const override;
    };
    // Explicit subtree effects, unlike Container's background-only radius.
    class ClipRRect final : public Widget
    {
        float radius_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
    public:
        ClipRRect(WidgetPtr child, float radius);
        bool ContainsPoint(Point point) const noexcept override;
        Rect GetPaintBounds() const override { return GetBounds(); }
    };
    class Opacity final : public Widget
    {
        float opacity_;
        MotionValue motion_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
        bool OnTick(float seconds) override;
    public:
        // The group is bounded by its layout box, including decorative effects.
        // Opacity zero still participates in input, just like any invisible content.
        Opacity(WidgetPtr child, float opacity);
        void SetOpacity(float opacity);
        void AnimateTo(float opacity, float seconds = .18f);
        float GetOpacity() const noexcept { return opacity_; }
        Rect GetPaintBounds() const override { return GetBounds(); }
    };
    // Opt-in retained raster cache. Does not change layout, hit testing or clipping.
    // Prefer moderately sized, mostly static cards over whole scrolling pages.
    class RepaintBoundary final : public Widget
    {
        mutable std::uint64_t boundsRevision_ = 0;
        mutable Rect relativePaintBounds_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
    public:
        explicit RepaintBoundary(WidgetPtr child);
        Rect GetPaintBounds() const override;
    };
    struct RepaintCacheStats
    {
        std::uint64_t hits = 0, misses = 0, bypasses = 0, evictions = 0;
        std::uint64_t cachedBytes = 0; // Conservative snapshot graph bytes, not driver allocation size.
        std::size_t entries = 0;
    };
    class Button : public Widget
    {
        friend class Context;
        friend class Widget;
        std::string label_;
        std::function<void()> callback_;
        bool enabled_ = true, hovered_ = false, pressed_ = false, focused_ = false, focusVisible_ = false;
        MotionValue hoverMotion_, focusMotion_, pressMotion_;
        struct Ripple { Point origin; float age = 0, releaseAge = -1; bool cancelled = false; };
        std::vector<Ripple> ripples_;
        void BeginPress(Point position);
        void EndPress(bool confirmed);
    protected:
        Size OnMeasure(const Constraints&) const override;
        void OnPaint(Canvas&) const override;
        bool OnTick(float seconds) override;
        void Cancel() noexcept { pressed_ = hovered_ = false; ripples_.clear(); MarkPaintDirty(); }
        void PaintInk(Canvas&, Rect bounds, float radius, RHIColor color) const;
    public:
        Button(std::string label, std::function<void()> onPressed = {});
        void SetText(std::string label);
        void SetEnabled(bool enabled);
        void SetOnPressed(std::function<void()> onPressed);
        const std::string& GetText() const noexcept { return label_; }
        bool IsEnabled() const noexcept { return enabled_ && bool(callback_); }
        bool IsHovered() const noexcept { return hovered_; }
        bool IsPressed() const noexcept { return pressed_; }
        bool IsFocused() const noexcept { return focused_; }
        bool IsFocusVisible() const noexcept { return focused_ && focusVisible_; }
    };
    // A tappable child using the same capture, keyboard and gesture cancellation as Button.
    class Pressable final : public Button
    {
        std::optional<float> inkRadius_;
        Size OnMeasure(const Constraints&) const override;
        void OnLayout() override;
        void OnPaint(Canvas&) const override;
        bool OnTick(float seconds) override;
    public:
        // By default use a direct Container child's radius, otherwise a square
        // ink boundary. Override for other custom-shaped tappable children.
        Pressable(WidgetPtr child, std::function<void()> onPressed, std::optional<float> inkRadius = {});
    };
    // Owns focus, hit testing, pointer capture and cached RHI commands, but NOT
    // a native surface/RHI. Keep Context when recreating RHI on Android resume.
    class Context
    {
        friend class Widget;
        struct Impl;
        std::unique_ptr<Impl> impl_;
        void Invalidate();
        void InvalidatePaint(const Widget* moved, bool redraw = true);
    public:
        Context();
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        void SetRoot(WidgetPtr root);
        WidgetPtr GetRoot() const;
        void SetTheme(Theme theme);
        // Per-context retained snapshot budget; 0 disables caching. Maximum 64 MiB.
        void SetRepaintCacheBudget(std::uint64_t bytes);
        RepaintCacheStats GetRepaintCacheStats() const;
        // Atlas retention budget (default 16 MiB, max 64 MiB); 0 uses the legacy
        // line rasterizer for comparison. Does not change the text widget API.
        void SetGlyphAtlasBudget(std::uint64_t bytes);
        TextCacheStats GetTextCacheStats() const;
        // UI::Application enables idle warmup automatically; manual Context hosts opt in.
        void SetTextWarmupEnabled(bool enabled);
        // Events have physical coordinates; layout/hit testing uses logical dp.
        bool HandleEvent(const Event& event, const Window& window);
        bool DismissTextInput(); // Back hides editor before navigating/exiting.
        const RHICommandList& BuildFrame(const Window& window, double deltaSeconds);
        std::uint64_t GetFrameRevision() const;
        bool Render(RHI& renderer, const Window& window, double deltaSeconds)
        { return renderer.Submit(window, BuildFrame(window, deltaSeconds)); }
    };
}
