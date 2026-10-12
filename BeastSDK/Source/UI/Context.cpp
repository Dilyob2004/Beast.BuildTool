#include "UI/UI.h"
#include "Core/Trace.h"
#include "UI/Canvas.h"
#include "UI/Private/ScrollSampler.h"
#include <chrono>
#include <atomic>

namespace Beast::UI
{
    struct Context::Impl
    {
        Context* context;
        WidgetPtr root;
        Canvas canvas;
        Theme theme;
        Window viewport;
        bool dirty = true, layoutDirty = true, shift = false, control = false;
        bool warmupEnabled = false;
        bool discoverAfterMotion = false;
        std::uint64_t frameRevision = 0;
        std::vector<std::pair<int, std::weak_ptr<Widget>>> semanticTargets;
        bool semanticsEnabled = false;
        std::string Label(const WidgetPtr& node) const {
            if (!node->accessibilityLabel_.empty()) return node->accessibilityLabel_;
            if (auto text = std::dynamic_pointer_cast<Text>(node)) return text->GetText();
            if (auto text = std::dynamic_pointer_cast<Paragraph>(node)) return text->GetText();
            if (auto button = std::dynamic_pointer_cast<Button>(node); button && !button->GetText().empty()) return button->GetText();
            std::string label;
            for (const auto& child : node->children_) { auto part = Label(child); if (!part.empty()) { if (!label.empty()) label += " "; label += part; } }
            if (label.size() > 4096) return {}; return label;
        }
        void Semantics(const WidgetPtr& widget, Rect clip, std::vector<AccessibleNode>& nodes) {
            if (!widget) return;
            clip = Intersect(clip, widget->GetBounds()); if (clip.width <= 0 || clip.height <= 0) return;
            AccessibleNode node; node.id = widget->semanticId_; bool leaf = true;
            if (auto field = std::dynamic_pointer_cast<TextField>(widget)) {
                node.role = AccessibleRole::TextField; node.label = TextEditor::ToUtf16(widget->accessibilityLabel_.empty() ? field->options_.placeholder : widget->accessibilityLabel_);
                node.password = field->IsPassword(); node.readOnly = field->IsReadOnly(); node.multiline = field->IsMultiline(); node.focused = field->IsFocused();
                // Password plaintext never enters the semantic snapshot or native provider.
                node.value = TextEditor::ToUtf16(field->Display(field->GetText()));
                if (!node.password) { node.anchor = TextEditor::ByteToUtf16(field->GetText(), field->GetSelection().anchor); node.caret = TextEditor::ByteToUtf16(field->GetText(), field->GetSelection().caret); }
                if (field->GetError()) node.error = TextEditor::ToUtf16(*field->GetError());
            } else if (auto button = std::dynamic_pointer_cast<Button>(widget)) {
                node.role = AccessibleRole::Button; node.label = TextEditor::ToUtf16(Label(widget)); node.enabled = button->IsEnabled(); node.focused = button->IsFocused();
            } else if (auto scroll = std::dynamic_pointer_cast<ScrollView>(widget)) {
                node.role = AccessibleRole::ScrollView; node.label = TextEditor::ToUtf16(widget->accessibilityLabel_); leaf = false;
                node.horizontal = scroll->GetAxis() == Axis::Horizontal; node.scrollOffset = scroll->GetOffset(); node.scrollExtent = scroll->GetExtent();
                node.viewport = node.horizontal ? clip.width : clip.height;
            } else if (std::dynamic_pointer_cast<Text>(widget) || std::dynamic_pointer_cast<Paragraph>(widget) || !widget->accessibilityLabel_.empty()) node.label = TextEditor::ToUtf16(Label(widget));
            else { for (const auto& child : widget->children_) Semantics(child, clip, nodes); return; }
            node.x = viewport.contentX + clip.x * viewport.scale; node.y = viewport.contentY + clip.y * viewport.scale;
            node.width = clip.width * viewport.scale; node.height = clip.height * viewport.scale;
            nodes.push_back(std::move(node)); semanticTargets.emplace_back(widget->semanticId_, widget);
            if (!leaf) for (const auto& child : widget->children_) Semantics(child, clip, nodes);
        }
        void PublishSemantics() {
            if (!viewport.accessibility || !semanticsEnabled) return;
            std::vector<AccessibleNode> nodes; semanticTargets.clear();
            Semantics(root, {0, 0, viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale}, nodes);
            viewport.accessibility->Publish(std::move(nodes));
        }
        void PollSemantics() {
            if (!viewport.accessibility) return;
            for (int n = 0; n < 32; ++n) {
                auto action = viewport.accessibility->PollAction(); if (!action) break;
                WidgetPtr widget;
                for (const auto& [id, target] : semanticTargets) if (id == action->id) { widget = target.lock(); break; }
                if (!widget || widget->owner_ != context) continue;
                if (action->action == AccessibleAction::Focus) { if (Focusable(widget)) Focus(widget, true); }
                else if (auto field = std::dynamic_pointer_cast<TextField>(widget)) {
                    try {
                        if (action->action == AccessibleAction::SetValue && !field->IsReadOnly()) {
                            const auto before = field->GetText(); auto value = TextEditor::FromUtf16(action->value, field->IsMultiline());
                            if (value.size() > field->options_.maxBytes) continue;
                            // Atomic model replacement, normal callbacks and byte limit apply.
                            field->editor_.SelectAll(); field->editor_.ReplaceSelection(value); field->Refresh(before != field->GetText());
                        } else if (action->action == AccessibleAction::SetSelection && !field->IsPassword())
                            field->SetSelection({TextEditor::Utf16ToByte(field->GetText(), action->anchor), TextEditor::Utf16ToByte(field->GetText(), action->caret)});
                    } catch (const std::invalid_argument&) {}
                } else if (auto button = std::dynamic_pointer_cast<Button>(widget); Valid(button) && action->action == AccessibleAction::Invoke) {
                    auto callback = button->callback_; if (callback) callback();
                } else if (auto scroll = std::dynamic_pointer_cast<ScrollView>(widget)) {
                    if (action->action == AccessibleAction::ScrollTo && std::isfinite(action->scrollPercent) && action->scrollPercent >= 0 && action->scrollPercent <= 100)
                        scroll->SetOffset(scroll->GetExtent() * static_cast<float>(action->scrollPercent / 100));
                    if (action->action == AccessibleAction::ScrollForward || action->action == AccessibleAction::ScrollBackward) {
                        const float page = scroll->GetAxis() == Axis::Vertical ? scroll->GetBounds().height : scroll->GetBounds().width;
                        scroll->ScrollBy(page * .8f * (action->action == AccessibleAction::ScrollForward ? 1 : -1));
                    }
                }
            }
        }
        std::shared_ptr<Button> hovered, capture, keyboard;
        WidgetPtr focused;
        std::shared_ptr<TextField> textCapture;
        std::weak_ptr<TextField> lastTextTap;
        std::chrono::steady_clock::time_point lastTextTapAt;
        Point lastTextTapPoint;
        double textHold = 0;
        bool textSelected = false, textTools = false;
        int textHandle = 0, toolCapture = -1;
        int toolPointerId = 0;
        bool toolTouch = false;
        Rect ToolsRect() const {
            const auto field = std::dynamic_pointer_cast<TextField>(focused);
            if (!field) return {};
            const auto r = field->GetBounds(); const float width = std::min(320.f, viewport.contentWidth / viewport.scale);
            return {std::clamp(r.x, 0.f, std::max(0.f, viewport.contentWidth / viewport.scale - width)),
                std::max(0.f, r.y - 44), width, 40};
        }
        void TapText(const std::shared_ptr<TextField>& field, Point point, bool isTouch) {
            const auto now = std::chrono::steady_clock::now();
            const float dx = point.x - lastTextTapPoint.x, dy = point.y - lastTextTapPoint.y;
            const bool twice = lastTextTap.lock() == field && now - lastTextTapAt < std::chrono::milliseconds(400) && dx * dx + dy * dy < 100;
            Focus(field, false);
            if (twice) { field->SelectWord(point); textTools = isTouch; field->touchSelection_ = isTouch; lastTextTap.reset(); }
            else { field->PlaceCaret(point, false); lastTextTap = field; }
            lastTextTapAt = now; lastTextTapPoint = point;
        }
        std::uint64_t imeSession = 0, imeRevision = 0;
        std::optional<NativeTextState> publishedIme;
        bool renewImeSession = false;
        bool revealEditor = false;
        void CloseIme()
        {
            if (imeSession && viewport.textInput) viewport.textInput->Close(imeSession);
            imeSession = imeRevision = 0; publishedIme.reset(); renewImeSession = false;
        }
        NativeTextState EditorState(const TextField& field) const
        {
            NativeTextState state; state.session = imeSession; state.revision = imeRevision;
            state.multiline = field.IsMultiline(); state.password = field.IsPassword();
            const auto& value = field.editor_.GetValue(); state.text = TextEditor::ToUtf16(value.text);
            state.anchor = TextEditor::ByteToUtf16(value.text, value.selection.anchor);
            state.caret = TextEditor::ByteToUtf16(value.text, value.selection.caret);
            if (value.composing) {
                state.composingStart = TextEditor::ByteToUtf16(value.text, value.composing->Start());
                state.composingEnd = TextEditor::ByteToUtf16(value.text, value.composing->End());
            }
            return state;
        }
        void SyncIme()
        {
            auto field = std::dynamic_pointer_cast<TextField>(focused);
            if (!viewport.textInput || !field || field->IsReadOnly()) { CloseIme(); return; }
            if (!imeSession || renewImeSession) {
                static std::atomic<std::uint64_t> next{1}; imeSession = next.fetch_add(1); imeRevision = 0;
                publishedIme.reset(); renewImeSession = false;
            }
            auto state = EditorState(*field);
            if (publishedIme && state == *publishedIme) return;
            state.revision = ++imeRevision; publishedIme = state;
            viewport.textInput->Update(state, static_cast<int>(field->options_.maxBytes));
        }
        void PollIme()
        {
            if (!viewport.textInput) return;
            // Bounded drain; Java coalesces edit snapshots but preserves submit/dismiss.
            for (int i = 0; i < 8; ++i) {
                auto packet = viewport.textInput->Poll(); if (!packet) break;
                auto field = std::dynamic_pointer_cast<TextField>(focused);
                if (!field || !imeSession || packet->session != imeSession || packet->revision != imeRevision) continue;
                const int action = packet->action; packet->action = 0;
                packet->multiline = field->IsMultiline(); packet->password = field->IsPassword();
                publishedIme = *packet;
                if (!field->ApplyIme(*packet)) { publishedIme.reset(); SyncIme(); continue; }
                revealEditor = true;
                if (action == 1 && focused == field) { Event enter; enter.type = EventType::KeyDown; enter.key = Key::Enter; field->Input(enter, false, false); }
                if ((action == 2 || (action == 1 && !field->IsMultiline() && !field->GetError())) && focused == field) Focus(nullptr, false);
            }
        }
        bool pointerActive = false, touch = false;
        int pointerId = 0;
        Point start;
        Point last;
        std::vector<std::shared_ptr<ScrollView>> scrollCandidates;
        std::shared_ptr<ScrollView> scrolling;
        std::vector<std::weak_ptr<ScrollView>> releasedScrolls;
        std::chrono::steady_clock::time_point movedAt;
        float scrollSpeed = 0;
        double movedTimestamp = 0;
        Detail::ScrollSampler scrollSampler;
        Key armedKey = Key::Unknown;
        RHICommandList commands;
        explicit Impl(Context* owner) : context(owner) {}
        bool Valid(const std::shared_ptr<Button>& button) const { return button && button->owner_ == context && button->IsEnabled(); }
        bool Focusable(const WidgetPtr& node) const
        { return node && node->owner_ == context && (std::dynamic_pointer_cast<TextField>(node) || Valid(std::dynamic_pointer_cast<Button>(node))); }
        void Hover(std::shared_ptr<Button> next)
        {
            if (hovered == next) return;
            if (hovered) { hovered->hovered_ = false; hovered->MarkPaintDirty(); }
            hovered = std::move(next); if (hovered) { hovered->hovered_ = true; hovered->MarkPaintDirty(); } dirty = true;
        }
        void Focus(WidgetPtr next, bool visible)
        {
            textTools = false; toolCapture = -1;
            if (focused != next) {
                auto editor = std::dynamic_pointer_cast<TextField>(next);
                // Transfer a live connection without hiding/reopening the keyboard.
                // Keep the old session until SyncIme so cancellation can still close it.
                if (imeSession && editor && !editor->IsReadOnly()) renewImeSession = true;
                else CloseIme();
            }
            revealEditor = true;
            if (auto button = std::dynamic_pointer_cast<Button>(focused)) { button->focused_ = button->focusVisible_ = false; button->MarkPaintDirty(); }
            if (auto field = std::dynamic_pointer_cast<TextField>(focused)) field->Focus(false);
            focused = std::move(next);
            if (auto button = std::dynamic_pointer_cast<Button>(focused)) { button->focused_ = true; button->focusVisible_ = visible; button->MarkPaintDirty(); }
            if (auto field = std::dynamic_pointer_cast<TextField>(focused)) field->Focus(true);
            dirty = true;
        }
        void CancelPointer(bool confirmed = false, double timestamp = 0, float velocity = 0)
        {
            if (capture) capture->EndPress(confirmed);
            capture.reset(); textCapture.reset(); pointerActive = false; dirty = true;
            textHandle = 0; textHold = 0; textSelected = false;
            toolCapture = -1;
            // Release all held scrolls, including a cancelled gesture. An
            // overscrolled view must not remain frozen outside its bounds.
            auto candidates = std::move(scrollCandidates);
            auto active = scrolling;
            scrolling.reset(); scrollCandidates.clear(); scrollSpeed = 0;
            scrollSampler.Reset();
            for (const auto& scroll : candidates) {
                scroll->Fling(scroll == active ? velocity : 0);
                if (std::isfinite(timestamp) && timestamp > 0) {
                    scroll->releaseTimestamp_ = timestamp;
                    releasedScrolls.push_back(scroll);
                }
            }
        }
        void CancelKeyboard(bool confirmed = false)
        { if (keyboard) keyboard->EndPress(confirmed); keyboard.reset(); armedKey = Key::Unknown; dirty = true; }
        void StopScrolls(const WidgetPtr& node)
        {
            if (!node) return;
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(node)) scroll->Stop();
            for (const auto& child : node->children_) StopScrolls(child);
        }
        void ClearInk(const WidgetPtr& node)
        {
            if (!node) return;
            if (auto button = std::dynamic_pointer_cast<Button>(node)) button->Cancel();
            for (const auto& child : node->children_) ClearInk(child);
        }
        void CancelAll() { StopScrolls(root); CancelPointer(); releasedScrolls.clear(); CancelKeyboard(); ClearInk(root); Hover(nullptr); Focus(nullptr, false); shift = control = false; }
        void Sanitize()
        {
            if (capture && !Valid(capture)) CancelPointer();
            if (keyboard && !Valid(keyboard)) CancelKeyboard();
            if (hovered && !Valid(hovered)) Hover(nullptr);
            if (focused && !Focusable(focused)) Focus(nullptr, false);
            if (textCapture && !Focusable(textCapture)) CancelPointer();
            if (scrolling && scrolling->owner_ != context) CancelPointer();
            for (const auto& scroll : scrollCandidates) if (scroll->owner_ != context) { CancelPointer(); break; }
        }
        void Layout(const Window& window)
        {
            if (!std::isfinite(window.scale) || window.scale <= 0 || window.scale > 16 || window.width < 0 || window.height < 0)
                throw std::invalid_argument("Invalid UI viewport or device scale.");
            auto next = window;
            viewport.clipboard = window.clipboard;
            viewport.accessibility = window.accessibility;
            if (viewport.textInput != window.textInput) { CloseIme(); viewport.textInput = window.textInput; }
            if (viewport.imeBottom != window.imeBottom) revealEditor = true;
            viewport.imeBottom = window.imeBottom;
            next.contentX = std::clamp(next.contentX, 0, next.width); next.contentY = std::clamp(next.contentY, 0, next.height);
            next.contentWidth = std::clamp(next.contentWidth, 0, next.width - next.contentX);
            next.contentHeight = std::clamp(next.contentHeight, 0, next.height - next.contentY);
            if (next.width != viewport.width || next.height != viewport.height || next.scale != viewport.scale ||
                next.contentX != viewport.contentX || next.contentY != viewport.contentY ||
                next.contentWidth != viewport.contentWidth || next.contentHeight != viewport.contentHeight)
            { viewport = next; revealEditor = true; StopScrolls(root); CancelPointer(); CancelKeyboard(); Hover(nullptr); layoutDirty = dirty = true; }
            Sanitize();
            if (layoutDirty && root)
            {
                const auto captureBounds = capture ? capture->GetBounds() : Rect{};
                const auto keyboardBounds = keyboard ? keyboard->GetBounds() : Rect{};
                const auto hoverBounds = hovered ? hovered->GetBounds() : Rect{};
                const auto textBounds = textCapture ? textCapture->GetBounds() : Rect{};
                const Size size{viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale};
                root->Measure(Constraints::Tight(size)); root->Layout({0, 0, size.width, size.height});
                const auto moved = [](const WidgetPtr& button, Rect before) {
                    if (!button) return false;
                    const auto after = button->GetBounds();
                    return before.x != after.x || before.y != after.y || before.width != after.width || before.height != after.height;
                };
                // A reflow must not turn an old press into an action on a moved/resized target.
                if (moved(capture, captureBounds)) CancelPointer();
                if (moved(textCapture, textBounds)) CancelPointer();
                if (moved(keyboard, keyboardBounds)) CancelKeyboard();
                if (moved(hovered, hoverBounds)) Hover(nullptr);
            }
            layoutDirty = false;
            if (revealEditor && viewport.imeBottom > 0 && focused && !pointerActive) {
                revealEditor = false;
                const Rect visible{0, 0, viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale};
                for (Widget* node = focused->parent_; node; node = node->parent_) {
                    auto* scroll = dynamic_cast<ScrollView*>(node);
                    if (!scroll || scroll->GetAxis() != Axis::Vertical) continue;
                    const auto clip = Intersect(visible, scroll->GetBounds()), field = focused->GetBounds();
                    if (clip.height <= 0) continue;
                    float delta = 0;
                    if (field.y + field.height > clip.y + clip.height - 8) delta = field.y + field.height - (clip.y + clip.height - 8);
                    if (field.y < clip.y + 8) delta = field.y - (clip.y + 8);
                    if (delta != 0) scroll->SetOffset(scroll->GetOffset() + delta);
                }
            }
        }
        WidgetPtr HitTarget(const WidgetPtr& node, Point point, Rect clip) const
        {
            if (!node) return nullptr;
            clip = Intersect(clip, node->GetBounds()); if (!clip.Contains(point) || !node->ContainsPoint(point)) return nullptr;
            for (auto it = node->children_.rbegin(); it != node->children_.rend(); ++it)
                if (auto hit = HitTarget(*it, point, clip)) return hit;
            return Focusable(node) ? node : nullptr;
        }
        WidgetPtr HitTarget(Point point) const
        { return HitTarget(root, point, {0, 0, viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale}); }
        std::shared_ptr<Button> Hit(Point point) const
        { return std::dynamic_pointer_cast<Button>(HitTarget(point)); }
        bool ScrollPath(const WidgetPtr& node, Point point, Rect clip, std::vector<std::shared_ptr<ScrollView>>& result) const
        {
            if (!node) return false;
            clip = Intersect(clip, node->GetBounds()); if (!clip.Contains(point) || !node->ContainsPoint(point)) return false;
            for (auto it = node->children_.rbegin(); it != node->children_.rend(); ++it)
                if (ScrollPath(*it, point, clip, result)) break;
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(node); scroll && scroll->CanDrag()) result.push_back(scroll);
            return true;
        }
        std::vector<std::shared_ptr<ScrollView>> ScrollPath(Point point) const
        {
            std::vector<std::shared_ptr<ScrollView>> result;
            ScrollPath(root, point, {0, 0, viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale}, result); return result;
        }
        void Collect(const WidgetPtr& node, std::vector<WidgetPtr>& buttons, Rect clip) const
        {
            if (!node) return;
            clip = Intersect(clip, node->GetBounds()); if (clip.width <= 0 || clip.height <= 0) return;
            if (Focusable(node)) buttons.push_back(node);
            for (const auto& child : node->children_) Collect(child, buttons, clip);
        }
        void Tab()
        {
            CancelKeyboard(); std::vector<WidgetPtr> buttons;
            Collect(root, buttons, {0, 0, viewport.contentWidth / viewport.scale, viewport.contentHeight / viewport.scale});
            if (buttons.empty()) { Focus(nullptr, false); return; }
            auto found = std::find(buttons.begin(), buttons.end(), focused);
            std::size_t index = found == buttons.end() ? (shift ? buttons.size() - 1 : 0) :
                (static_cast<std::size_t>(found - buttons.begin()) + (shift ? buttons.size() - 1 : 1)) % buttons.size();
            Focus(buttons[index], true);
        }
    };
    Context::Context() : impl_(std::make_unique<Impl>(this)) {}
    Context::~Context() { impl_->CancelAll(); if (impl_->root) impl_->root->Attach(nullptr); }
    bool Context::DismissTextInput()
    {
        if (!std::dynamic_pointer_cast<TextField>(impl_->focused)) return false;
        impl_->Focus(nullptr, false); return true;
    }
    void Context::Invalidate() { impl_->dirty = impl_->layoutDirty = true; impl_->Sanitize(); }
    void Context::InvalidatePaint(const Widget* moved, bool redraw)
    {
        auto& state = *impl_; state.dirty |= redraw;
        if (!moved) return;
        const auto affected = [moved](const WidgetPtr& button) {
            for (const Widget* node = button.get(); node; node = node->parent_) if (node == moved) return true;
            return false;
        };
        if (affected(state.capture)) state.CancelPointer();
        if (affected(state.textCapture)) state.CancelPointer();
        if (affected(state.keyboard)) state.CancelKeyboard();
        if (affected(state.hovered)) state.Hover(nullptr);
    }
    void Context::SetRoot(WidgetPtr root)
    {
        if (root == impl_->root) return;
        if (root && (root->parent_ || root->owner_)) throw std::invalid_argument("UI root already belongs to a tree.");
        impl_->CancelAll(); if (impl_->root) impl_->root->Attach(nullptr);
        impl_->root = std::move(root); if (impl_->root) impl_->root->Attach(this); Invalidate();
    }
    WidgetPtr Context::GetRoot() const { return impl_->root; }
    void Context::SetRepaintCacheBudget(std::uint64_t bytes)
    { impl_->canvas.SetRepaintCacheBudget(bytes); impl_->dirty = true; }
    RepaintCacheStats Context::GetRepaintCacheStats() const { return impl_->canvas.GetRepaintCacheStats(); }
    void Context::SetGlyphAtlasBudget(std::uint64_t bytes) { impl_->canvas.SetGlyphAtlasBudget(bytes); impl_->dirty = true; }
    TextCacheStats Context::GetTextCacheStats() const { return impl_->canvas.GetTextCacheStats(); }
    void Context::SetTextWarmupEnabled(bool enabled) { impl_->warmupEnabled = enabled; impl_->dirty = true; }
    void Context::SetTheme(Theme theme)
    {
        for (auto color : {theme.background, theme.primary, theme.onPrimary, theme.onSurface})
            for (float value : {color.r, color.g, color.b, color.a})
                if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument("Invalid UI theme color.");
        impl_->theme = theme; Invalidate();
    }
    const RHICommandList& Context::BuildFrame(const Window& window, double deltaSeconds)
    {
        Beast::Detail::TraceScope trace("Beast.UI.Build");
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) throw std::invalid_argument("Invalid UI frame delta.");
        auto& state = *impl_; state.Layout(window); state.PollSemantics(); state.SyncIme(); state.PollIme(); state.SyncIme();
        const bool semanticsEnabled = window.accessibility && window.accessibility->Enabled();
        if (semanticsEnabled != state.semanticsEnabled) { state.semanticsEnabled = semanticsEnabled; state.dirty = true; }
        if (state.pointerActive && state.touch && state.textCapture && !state.scrolling && !state.textHandle && !state.textSelected && state.textHold >= 0) {
            state.textHold += std::min(deltaSeconds, .1);
            if (state.textHold >= .5) {
                auto field = state.textCapture; state.Focus(field, false); field->SelectWord(state.start);
                field->touchSelection_ = true; state.textSelected = true; state.textTools = true; state.dirty = true;
            }
        }
        // Input is dispatched before BuildFrame. A release near the end of
        // this interval must not advance a newly started spring by the whole
        // interval (most of which was still a drag). Keep future timestamps
        // pending: Android's frame timestamp can precede a queued input event.
        std::erase_if(state.releasedScrolls, [&](const std::weak_ptr<ScrollView>& pending) {
            const auto scroll = pending.lock();
            if (!scroll || scroll->owner_ != this || scroll->releaseTimestamp_ <= 0) return true;
            if (!std::isfinite(window.frameTimeSeconds) || window.frameTimeSeconds <= 0) {
                scroll->releaseTimestamp_ = 0; return true; // Synthetic callers without a common clock.
            }
            const double elapsed = window.frameTimeSeconds - scroll->releaseTimestamp_;
            scroll->firstTickSeconds_ = static_cast<float>(std::clamp(elapsed, 0.0, std::min(deltaSeconds, .1)));
            if (elapsed < 0) return false;
            scroll->releaseTimestamp_ = 0; return true;
        });
        if (state.pointerActive && state.touch && state.scrolling && state.scrollSampler.HasValue() && window.frameTimeSeconds > 0) {
            auto sampled = state.scrollSampler.Sample(window.frameTimeSeconds);
            if (state.scrolling->physics_ == ScrollPhysics::Clamping) sampled = std::clamp(sampled, 0.f, state.scrolling->extent_);
            state.scrolling->MoveTo(sampled);
        }
        const bool animating = state.root && state.root->Tick(static_cast<float>(std::min(deltaSeconds, .1)));
        state.dirty |= animating;
        state.Layout(window); // A kinetic scroll may have invalidated geometry during Tick.
        // The last animated paint deliberately skipped discovery. Visit the new
        // neighborhood once after motion, even if Tick no longer dirties the UI.
        if (state.warmupEnabled && (state.pointerActive || animating)) state.discoverAfterMotion = true;
        else if (state.discoverAfterMotion) {
            state.dirty |= state.warmupEnabled;
            state.discoverAfterMotion = false;
        }
        // Speculative work never competes with a drag or an animated repaint.
        if (state.warmupEnabled && !state.dirty && !state.pointerActive && state.commands.IsReady()) state.dirty = state.canvas.WarmText();
        state.canvas.DiscoverWarmText(state.warmupEnabled && !state.pointerActive && !animating);
        if (state.dirty || !state.commands.IsReady())
        {
            Beast::Detail::TraceScope paintTrace("Beast.UI.Paint");
            state.canvas.Begin(state.commands, state.viewport, state.theme);
            if (state.root && window.width > 0 && window.height > 0) state.root->Paint(state.canvas);
            if (state.textTools) {
                const auto menu = state.ToolsRect();
                state.canvas.RoundedRect(menu, 8, state.theme.onSurface);
                const char* labels[]{"Copy", "Cut", "Paste", "All"};
                for (int i = 0; i < 4; ++i) state.canvas.Text({menu.x + menu.width * i / 4 + 8, menu.y + 10}, labels[i], 14, state.theme.background);
            }
            state.canvas.End(); state.dirty = false; ++state.frameRevision; state.PublishSemantics();
            if (Beast::Detail::TraceEnabled()) {
                const auto stats = state.canvas.GetTextCacheStats();
                Beast::Detail::TraceCounter("Beast.Text.GlyphMisses", static_cast<std::int64_t>(stats.glyphMisses));
                Beast::Detail::TraceCounter("Beast.Text.RunMisses", static_cast<std::int64_t>(stats.runMisses));
                Beast::Detail::TraceCounter("Beast.Text.SealedBytes", static_cast<std::int64_t>(stats.sealedBytes));
            }
        }
        return state.commands;
    }
    std::uint64_t Context::GetFrameRevision() const { return impl_->frameRevision; }
    bool Context::HandleEvent(const Event& event, const Window& window)
    {
        Beast::Detail::TraceScope trace(event.type == EventType::TouchUp ? "Beast.Input.Release" : "Beast.Input.Event");
        auto& state = *impl_;
        if (event.type == EventType::FocusLost || event.type == EventType::Suspended || event.type == EventType::SurfaceDestroyed)
        { state.CancelAll(); return false; }
        state.Layout(window);
        if (!state.root || !window.width || !window.height) return false;
        if (event.type == EventType::MouseWheel)
        {
            if (!std::isfinite(event.x) || !std::isfinite(event.y) || !std::isfinite(event.wheel)) return false;
            const Point point{(event.x - state.viewport.contentX) / state.viewport.scale, (event.y - state.viewport.contentY) / state.viewport.scale};
            for (const auto& scroll : state.ScrollPath(point))
                if (scroll->GetAxis() == (state.shift ? Axis::Horizontal : Axis::Vertical))
                {
                    scroll->Stop(); state.CancelPointer(); state.CancelKeyboard();
                    if (scroll->ScrollBy(-event.wheel * 48)) { state.Hover(nullptr); return true; }
                }
            return false;
        }
        if (event.type == EventType::MouseCancel) { if (!state.touch) state.CancelPointer(false, event.timestamp); return false; }
        if (event.type == EventType::MouseLeave)
        {
            state.Hover(nullptr);
            if (state.capture && !state.touch) { state.capture->EndPress(false); state.dirty = true; }
            return false;
        }
        if (event.type == EventType::TextInput)
        {
            auto field = std::dynamic_pointer_cast<TextField>(state.focused);
            return field && field->Input(event, state.shift, state.control);
        }
        if (event.type == EventType::KeyDown || event.type == EventType::KeyUp)
        {
            const bool down = event.type == EventType::KeyDown;
            if (event.key == Key::Shift) { state.shift = down; return false; }
            if (event.key == Key::Control) { state.control = down; return false; }
            if (down && event.key == Key::Tab) { if (!event.repeat) { state.CancelPointer(); state.Tab(); } return true; }
            if (auto field = std::dynamic_pointer_cast<TextField>(state.focused)) {
                if (down && event.key == Key::Escape) { state.CancelPointer(); state.Focus(nullptr, false); return true; }
                if (down && state.control && (event.key == Key::C || event.key == Key::X || event.key == Key::V)) {
                    if (!event.repeat) field->Edit(event.key == Key::C ? TextEditAction::Copy : event.key == Key::X ? TextEditAction::Cut : TextEditAction::Paste, window.clipboard.get());
                    return true;
                }
                return field->Input(event, state.shift, state.control);
            }
            if (event.key != Key::Space && event.key != Key::Enter) return false;
            if (down)
            {
                const auto focused = std::dynamic_pointer_cast<Button>(state.focused);
                if (!event.repeat && !state.keyboard && !state.pointerActive && state.Valid(focused))
                {
                    state.keyboard = focused; state.armedKey = event.key;
                    const auto bounds = state.keyboard->GetBounds();
                    state.keyboard->BeginPress({bounds.x + bounds.width / 2, bounds.y + bounds.height / 2}); state.dirty = true;
                }
                return bool(state.keyboard);
            }
            if (!state.keyboard || state.armedKey != event.key) return false;
            auto button = state.keyboard; const bool activate = state.Valid(button) && button == state.focused && button->pressed_;
            state.CancelKeyboard(activate);
            auto callback = activate ? button->callback_ : std::function<void()>{};
            if (callback) callback(); // State settled first; callback may remove/rebuild the tree.
            return true;
        }
        const bool touch = event.type == EventType::TouchDown || event.type == EventType::TouchMove ||
            event.type == EventType::TouchUp || event.type == EventType::TouchCancel;
        const bool down = event.type == EventType::MouseDown || event.type == EventType::TouchDown;
        const bool move = event.type == EventType::MouseMove || event.type == EventType::TouchMove;
        const bool up = event.type == EventType::MouseUp || event.type == EventType::TouchUp;
        if (!touch && !down && !move && !up) return false;
        if (!touch && (down || up) && event.button != MouseButton::Left) return false;
        if (!std::isfinite(event.x) || !std::isfinite(event.y)) return false;
        Point point{(event.x - state.viewport.contentX) / state.viewport.scale, (event.y - state.viewport.contentY) / state.viewport.scale};
        if (state.textTools && !state.pointerActive && (down || up)) {
            const auto menu = state.ToolsRect();
            if (menu.width > 0 && menu.Contains(point)) {
                const int index = std::clamp(static_cast<int>((point.x - menu.x) / (menu.width / 4)), 0, 3);
                if (down && state.toolCapture < 0) { state.toolCapture = index; state.toolPointerId = event.pointerId; state.toolTouch = touch; }
                else if (up && state.toolCapture == index && state.toolPointerId == event.pointerId && state.toolTouch == touch) {
                    auto field = std::dynamic_pointer_cast<TextField>(state.focused);
                    state.toolCapture = -1; state.textTools = false; state.dirty = true;
                    if (field) field->Edit(static_cast<TextEditAction>(index), window.clipboard.get());
                }
                return true;
            }
            state.toolCapture = -1;
            if (down) { state.textTools = false; state.dirty = true; }
        }
        const int id = touch ? event.pointerId : 0;
        if (state.pointerActive && (state.touch != touch || state.pointerId != id)) return false;
        if (event.type == EventType::TouchCancel)
        { const bool active = state.pointerActive; state.CancelPointer(false, event.timestamp); return active; }
        if (down)
        {
            if (state.pointerActive) return false;
            state.CancelKeyboard(); const auto target = state.HitTarget(point);
            auto hit = std::dynamic_pointer_cast<Button>(target);
            const auto field = std::dynamic_pointer_cast<TextField>(target);
            // Touch focuses on confirmed tap, not while beginning a scroll.
            if (!touch || !field) state.Focus(target, false);
            if (field && !touch) { if (state.shift) field->PlaceCaret(point, true); else state.TapText(field, point, false); }
            if (field && touch && field->IsFocused() && field->touchSelection_ && !field->GetSelection().Empty()) {
                for (int handle = 1; handle <= 2; ++handle) { const auto p = field->HandlePoint(handle == 1);
                    if (std::abs(point.x - p.x) <= 18 && std::abs(point.y - p.y) <= 18) { state.textHandle = handle; break; } }
            }
            if (!touch) state.Hover(hit); else state.Hover(nullptr);
            state.scrollCandidates = state.ScrollPath(point);
            if (!target && state.scrollCandidates.empty()) return false;
            for (const auto& scroll : state.scrollCandidates) scroll->BeginDrag();
            state.capture = hit; state.textCapture = field; state.pointerActive = true; state.touch = touch; state.pointerId = id; state.start = point;
            state.last = point; state.movedAt = std::chrono::steady_clock::now(); state.scrollSpeed = 0;
            state.movedTimestamp = event.timestamp;
            if (hit) hit->BeginPress(point); state.dirty = true; return true;
        }
        if (move)
        {
            if (!touch && !state.scrolling) state.Hover(state.Hit(point));
            if (!state.pointerActive) return false;
            if (state.textCapture && state.textHandle) { state.textCapture->DragHandle(point, state.textHandle == 1); return true; }
            if (state.textSelected) return true;
            if (state.textCapture && !touch) { state.textCapture->PlaceCaret(point, true); return true; }
            const float totalX = point.x - state.start.x, totalY = point.y - state.start.y;
            if (totalX * totalX + totalY * totalY > 8 * 8) state.textHold = -1;
            if (!state.scrolling && totalX * totalX + totalY * totalY > 8 * 8)
            {
                const auto axis = std::abs(totalX) > std::abs(totalY) ? Axis::Horizontal : Axis::Vertical;
                for (const auto& scroll : state.scrollCandidates) if (scroll->GetAxis() == axis) { state.scrolling = scroll; break; }
                if (state.scrolling)
                {
                    if (state.capture) state.capture->EndPress(false);
                    state.capture.reset(); state.textCapture.reset(); state.CancelKeyboard(); state.Hover(nullptr);
                }
            }
            if (state.scrolling)
            {
                const auto now = std::chrono::steady_clock::now();
                const bool nativeTime = std::isfinite(event.timestamp) && event.timestamp > state.movedTimestamp && state.movedTimestamp > 0;
                const float elapsed = nativeTime ? static_cast<float>(event.timestamp - state.movedTimestamp) : std::chrono::duration<float>(now - state.movedAt).count();
                const float dt = std::clamp(elapsed, .001f, .1f);
                const float delta = state.scrolling->GetAxis() == Axis::Vertical ? state.last.y - point.y : state.last.x - point.x;
                const bool sampleTouch = touch && nativeTime && window.frameTimeSeconds > 0;
                if (sampleTouch && state.scrollSampler.HasValue()) state.scrolling->MoveTo(state.scrollSampler.Latest());
                const float previousOffset = state.scrolling->GetOffset();
                if (sampleTouch && !state.scrollSampler.HasValue()) state.scrollSampler.Push(previousOffset, state.movedTimestamp);
                state.scrolling->DragBy(delta);
                const float travelled = state.scrolling->GetOffset() - previousOffset;
                if (sampleTouch) state.scrollSampler.Push(state.scrolling->GetOffset(), event.timestamp);
                else state.scrollSampler.Reset();
                const float blend = 1 - std::exp(-dt / .04f);
                state.scrollSpeed += blend * (travelled / dt - state.scrollSpeed);
                state.last = point; state.movedAt = now; state.movedTimestamp = event.timestamp; return true;
            }
            if (state.capture)
            {
                const float dx = point.x - state.start.x, dy = point.y - state.start.y;
                if (touch && dx * dx + dy * dy > 18 * 18)
                { state.capture->EndPress(false); state.capture.reset(); } // Tap slop cancels this entire gesture.
                else if (state.Hit(point) != state.capture) state.capture->EndPress(false);
                else { state.capture->pressed_ = true; state.capture->MarkPaintDirty(); }
                state.dirty = true;
            }
            return true;
        }
        if (up && state.pointerActive)
        {
            if (state.textCapture && !state.scrolling) {
                auto field = state.textCapture;
                if (state.textHandle || state.textSelected) { state.CancelPointer(false, event.timestamp); state.textTools = true; state.dirty = true; return true; }
                const float dx = point.x - state.start.x, dy = point.y - state.start.y;
                const bool tap = touch && dx * dx + dy * dy <= 18 * 18 && state.HitTarget(point) == field;
                state.CancelPointer(false, event.timestamp);
                if (tap) state.TapText(field, point, true);
                return true;
            }
            if (state.scrolling)
            {
                const bool nativeTime = std::isfinite(event.timestamp) && event.timestamp >= state.movedTimestamp && state.movedTimestamp > 0;
                const double elapsed = nativeTime ? event.timestamp - state.movedTimestamp : std::chrono::duration<double>(std::chrono::steady_clock::now() - state.movedAt).count();
                // Native move events stop when the pointer is held still. Do
                // not keep its last velocity alive until an abrupt timeout:
                // a paused release otherwise kicks the content outward again.
                const float pause = static_cast<float>(std::clamp((elapsed - .020) / .060, 0.0, 1.0));
                const float speed = state.scrollSpeed * (1 - pause * pause * (3 - 2 * pause));
                state.CancelPointer(false, nativeTime ? event.timestamp : 0, speed); return true;
            }
            auto button = state.capture;
            const float dx = point.x - state.start.x, dy = point.y - state.start.y;
            const bool activate = state.Valid(button) && button->pressed_ && state.Hit(point) == button &&
                (!touch || dx * dx + dy * dy <= 18 * 18);
            state.CancelPointer(activate, event.timestamp);
            auto callback = activate ? button->callback_ : std::function<void()>{};
            if (callback) callback();
            return true;
        }
        return false;
    }
}
