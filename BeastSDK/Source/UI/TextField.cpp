#include "UI/UI.h"
#include "UI/Canvas.h"
#include "UI/Private/Font.h"
#include "UI/Private/TextLayout.h"

namespace Beast::UI
{
    TextField::TextField(std::string text, TextFieldOptions options)
        : editor_(std::move(text), options.maxBytes, options.multiline), options_(std::move(options))
    {
        TextEditor::Boundaries(options_.placeholder);
        Detail::MeasureText(options_.placeholder, options_.fontSize);
        if (!options_.minLines || options_.maxLines < options_.minLines || options_.maxLines > 100 || (options_.password && options_.multiline)) throw std::invalid_argument("Invalid TextField line options.");
    }
    std::string TextField::Display(const std::string& value) const {
        if (!options_.password) return value;
        std::string result;
        const auto clusters = TextEditor::GraphemeBoundaries(value);
        for (std::size_t n = 1; n < clusters.size(); ++n) result += "•";
        return result;
    }
    void TextField::Metrics() const
    {
        const float width = options_.multiline ? std::max(1.f, GetBounds().width - 26) : std::numeric_limits<float>::infinity();
        if (!lines_.empty() && metricsText_ == GetText() && metricsWidth_ == width) return;
        lines_.clear(); float y = 0;
        const auto ranges = options_.multiline ? Detail::WrapText(GetText(), width, options_.fontSize) : std::vector<Detail::TextLine>{{0, GetText().size(), GetText().size()}};
        for (const auto& r : ranges) {
            const auto raw = GetText().substr(r.start, r.end - r.start);
            auto display = Display(raw);
            auto offsets = options_.password ? TextEditor::GraphemeBoundaries(raw) : TextEditor::Boundaries(raw, true);
            auto carets = Detail::TextCaretPositions(display, options_.fontSize);
            for (auto& offset : offsets) offset += r.start;
            const auto size = Detail::MeasureText(display, options_.fontSize);
            lines_.push_back({r.start, r.end, r.next, std::move(display), std::move(offsets), std::move(carets), y, size.height, size.width}); y += size.height;
        }
        metricsText_ = GetText(); metricsWidth_ = width;
    }
    std::pair<std::size_t, std::size_t> TextField::CaretIndex(std::size_t offset) const {
        Metrics(); std::size_t row = 0;
        while (row + 1 < lines_.size() && offset >= lines_[row + 1].start) ++row;
        const auto& l = lines_[row];
        auto it = std::lower_bound(l.offsets.begin(), l.offsets.end(), offset);
        if (it == l.offsets.end()) --it;
        return {row, static_cast<std::size_t>(it - l.offsets.begin())};
    }
    float TextField::ContentHeight(float width) const {
        const float base = Detail::MeasureText("Mg", options_.fontSize).height;
        if (!options_.multiline) return std::max(base, Detail::MeasureText(GetText().empty() ? options_.placeholder : Display(GetText()), options_.fontSize).height);
        float height = 0;
        for (const auto& l : Detail::WrapText(GetText(), std::max(1.f, width - 26), options_.fontSize)) height += l.height;
        return std::clamp(height, base * options_.minLines, base * options_.maxLines);
    }
    void TextField::EnsureVisible() const
    {
        const auto [row, index] = CaretIndex(GetSelection().caret);
        const auto& line = lines_[row];
        const float width = std::max(0.f, GetBounds().width - 26.f), caret = line.carets[index];
        if (caret < scroll_) scroll_ = caret;
        if (caret > scroll_ + width) scroll_ = caret - width;
        scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, line.width - width));
        const float height = std::max(1.f, GetBounds().height - 24 - (error_ ? 22.f : 0.f));
        if (line.y < scrollY_) scrollY_ = line.y;
        if (line.y + line.height > scrollY_ + height) scrollY_ = line.y + line.height - height;
        scrollY_ = std::clamp(scrollY_, 0.f, std::max(0.f, lines_.back().y + lines_.back().height - height));
    }
    void TextField::Refresh(bool textChanged)
    {
        if (metricsText_ != GetText()) Invalidate();
        if (textChanged && options_.validator) Validate();
        blink_ = 0; caretVisible_ = true; EnsureVisible(); InvalidatePaint();
        // Snapshot and settle state first: callbacks may detach/rebuild this tree.
        const auto callback = changed_; const auto text = GetText();
        if (textChanged && callback) callback(text);
    }
    void TextField::SetText(std::string text)
    { editor_.SetText(std::move(text)); error_.reset(); Refresh(false); }
    bool TextField::Validate() {
        auto next = options_.validator ? options_.validator(GetText()) : std::optional<std::string>{};
        if (next) TextEditor::Boundaries(*next);
        if (next != error_) { error_ = std::move(next); Invalidate(); }
        return !error_;
    }
    void TextField::SetSelection(TextSelection selection)
    { editor_.Select(selection); Refresh(false); }
    void TextField::Focus(bool focused)
    { focused_ = focused; if (!focused) { editor_.FinishComposition(); touchSelection_ = false; } Refresh(false); }
    bool TextField::Edit(TextEditAction action, Clipboard* clipboard)
    {
        const auto before = GetText();
        if (action == TextEditAction::SelectAll) editor_.SelectAll();
        else if (action == TextEditAction::Paste) {
            if (options_.readOnly || !clipboard) return false;
            const auto text = clipboard->ReadClipboard(); if (!text) return false;
            try { if (!editor_.ReplaceSelection(TextEditor::FromUtf16(*text, options_.multiline))) return false; }
            catch (const std::invalid_argument&) { return false; }
        } else {
            if (options_.password || !clipboard || GetSelection().Empty() || (action == TextEditAction::Cut && options_.readOnly)) return false;
            const auto selection = GetSelection();
            if (!clipboard->WriteClipboard(TextEditor::ToUtf16(before.substr(selection.Start(), selection.End() - selection.Start())))) return false;
            if (action == TextEditAction::Cut) editor_.ReplaceSelection("");
        }
        Refresh(before != GetText()); return true;
    }
    bool TextField::ApplyIme(const NativeTextState& state)
    {
        if (options_.readOnly) return false;
        TextEditingValue next;
        try {
            next.text = TextEditor::FromUtf16(state.text, options_.multiline);
            next.selection = {TextEditor::Utf16ToByte(next.text, state.anchor), TextEditor::Utf16ToByte(next.text, state.caret)};
            if (state.composingStart >= 0 && state.composingEnd > state.composingStart)
                next.composing = TextSelection{TextEditor::Utf16ToByte(next.text, state.composingStart), TextEditor::Utf16ToByte(next.text, state.composingEnd)};
            else if (state.composingStart != -1 || state.composingEnd != -1) return false;
        } catch (const std::invalid_argument&) { return false; }
        const auto before = GetText();
        if (!editor_.ApplyIme(std::move(next))) return false;
        Refresh(before != GetText()); return true;
    }
    std::size_t TextField::HitOffset(Point point) const
    {
        EnsureVisible(); const float x = point.x - GetBounds().x - 12.f + scroll_;
        const float y = point.y - GetBounds().y - 12 + scrollY_;
        std::size_t row = 0; while (row + 1 < lines_.size() && y >= lines_[row].y + lines_[row].height) ++row;
        const auto& line = lines_[row];
        const auto clusters = TextEditor::GraphemeBoundaries(GetText(), options_.multiline);
        std::size_t best = line.start; float distance = std::numeric_limits<float>::infinity();
        for (std::size_t n = 0; n < line.offsets.size(); ++n) if (std::binary_search(clusters.begin(), clusters.end(), line.offsets[n])) {
            const float d = std::abs(x - line.carets[n]); if (d < distance) { distance = d; best = line.offsets[n]; }
        }
        return best;
    }
    void TextField::PlaceCaret(Point point, bool extend)
    {
        const auto at = HitOffset(point);
        SetSelection({extend ? GetSelection().anchor : at, at});
    }
    void TextField::SelectWord(Point point)
    {
        const auto& text = GetText(); auto boundaries = TextEditor::WordBoundaries(text, options_.multiline);
        if (text.empty()) { SetSelection({0, 0}); return; }
        // Selection tailoring: a word boundary must not split a grapheme.
        const auto clusters = TextEditor::GraphemeBoundaries(text, options_.multiline);
        std::erase_if(boundaries, [&](std::size_t b) { return !std::binary_search(clusters.begin(), clusters.end(), b); });
        auto end = std::upper_bound(boundaries.begin(), boundaries.end(), HitOffset(point));
        if (end == boundaries.end()) --end;
        SetSelection({*(end - 1), *end});
    }
    Point TextField::HandlePoint(bool anchor) const
    {
        EnsureVisible();
        const auto offset = anchor ? GetSelection().anchor : GetSelection().caret;
        const auto [row, index] = CaretIndex(offset); const auto& line = lines_[row];
        const auto bounds = GetBounds();
        const float margin = std::min(6.f, std::max(0.f, bounds.width * .5f));
        return {std::clamp(bounds.x + 12 + line.carets[index] - scroll_, bounds.x + margin, bounds.x + bounds.width - margin), bounds.y + 12 + line.y + line.height - scrollY_ + 4};
    }
    void TextField::DragHandle(Point point, bool anchor)
    { auto selection = GetSelection(); if (anchor) selection.anchor = HitOffset(point); else selection.caret = HitOffset(point); SetSelection(selection); }
    void TextField::MoveVisual(Key key, bool extend)
    {
        const auto selection = GetSelection();
        const auto [row, index] = CaretIndex(selection.caret);
        const auto& line = lines_[row];
        const auto clusters = TextEditor::GraphemeBoundaries(GetText(), options_.multiline);
        std::vector<std::pair<float, std::size_t>> stops;
        const auto collect = [&](const FieldLine& l) {
            for (std::size_t n = 0; n < l.offsets.size(); ++n)
                if (std::binary_search(clusters.begin(), clusters.end(), l.offsets[n])) stops.emplace_back(l.carets[n], l.offsets[n]);
            std::sort(stops.begin(), stops.end());
        };
        collect(line);
        auto at = std::find_if(stops.begin(), stops.end(), [&](auto s) { return s.second == selection.caret; });
        std::size_t next = selection.caret;
        if (key == Key::Home) next = stops.front().second;
        else if (key == Key::End) next = stops.back().second;
        else if (key == Key::Up || key == Key::Down) {
            const auto target = key == Key::Up ? (row ? row - 1 : row) : std::min(row + 1, lines_.size() - 1);
            stops.clear(); collect(lines_[target]);
            next = std::min_element(stops.begin(), stops.end(), [&](auto a, auto b) { return std::abs(a.first - line.carets[index]) < std::abs(b.first - line.carets[index]); })->second;
        } else if (at != stops.end()) {
            if (key == Key::Left && at != stops.begin()) next = (at - 1)->second;
            else if (key == Key::Right && at + 1 != stops.end()) next = (at + 1)->second;
            else if (key == Key::Left && row) next = lines_[row - 1].end;
            else if (key == Key::Right && row + 1 < lines_.size()) next = lines_[row + 1].start;
        }
        editor_.Select({extend ? selection.anchor : next, next});
    }
    bool TextField::Input(const Event& event, bool shift, bool control)
    {
        const auto before = GetText(); bool handled = true;
        if (event.type == EventType::TextInput) {
            // Committed text is authoritative (AltGr can report Ctrl held).
            if (options_.readOnly || event.character < 32 || event.character == 127) return true;
            try { editor_.ReplaceSelection(TextEditor::Encode(event.character)); }
            catch (const std::invalid_argument&) { return true; }
        } else if (event.type == EventType::KeyDown) {
            if (control) {
                if (event.key == Key::A) editor_.SelectAll();
                else if (event.key == Key::Z && !options_.readOnly) { if (shift) editor_.Redo(); else editor_.Undo(); }
                else if (event.key == Key::Y && !options_.readOnly) editor_.Redo();
                else return false;
            } else switch (event.key) {
                case Key::Left: case Key::Right: case Key::Up: case Key::Down:
                case Key::Home: case Key::End: MoveVisual(event.key, shift); break;
                case Key::Backspace: if (!options_.readOnly) editor_.Delete(true); break;
                case Key::Delete: if (!options_.readOnly) editor_.Delete(false); break;
                case Key::Enter: {
                    if (options_.multiline) { if (!options_.readOnly) editor_.ReplaceSelection("\n"); break; }
                    if (!Validate()) return true;
                    const auto callback = submitted_; const auto text = GetText();
                    if (!event.repeat && callback) callback(text); return true;
                }
                default: handled = false; break;
            }
        } else return false;
        if (handled) Refresh(before != GetText()); return handled;
    }
    Size TextField::OnMeasure(const Constraints& constraints) const
    { const float width = constraints.Constrain({240, 0}).width; return constraints.Constrain({width, std::max(48.f, ContentHeight(width) + 24.f) + (error_ ? 22.f : 0.f)}); }
    void TextField::OnPaint(Canvas& canvas) const
    {
        const auto bounds = GetBounds(); const auto& theme = canvas.GetTheme();
        const RHIColor muted{.45f, .45f, .48f, 1};
        const RHIColor errorColor{.75f, .12f, .12f, 1};
        const Rect box{bounds.x, bounds.y, bounds.width, std::max(0.f, bounds.height - (error_ ? 22.f : 0.f))};
        canvas.RoundedRect(box, 8, theme.background, focused_ ? 2.f : 1.f, error_ ? errorColor : focused_ ? theme.primary : muted);
        const Rect content{bounds.x + 12, bounds.y + 12, std::max(0.f, bounds.width - 24), std::max(0.f, box.height - 24)};
        EnsureVisible(); canvas.PushClip(content);
        const auto selection = GetSelection();
        const float x = content.x - scroll_;
        if (GetText().empty()) canvas.Text({x, content.y}, options_.placeholder, options_.fontSize, muted);
        for (const auto& line : lines_) {
            const float y = content.y + line.y - scrollY_;
            if (y + line.height < content.y || y > content.y + content.height) continue;
            const auto paintRange = [&](TextSelection range, bool underline) {
                const auto first = std::max(range.Start(), line.start), last = std::min(range.End(), line.end);
                if (last <= first) return;
                const auto a = static_cast<std::size_t>(std::lower_bound(line.offsets.begin(), line.offsets.end(), first) - line.offsets.begin());
                const auto b = static_cast<std::size_t>(std::lower_bound(line.offsets.begin(), line.offsets.end(), last) - line.offsets.begin());
                auto color = theme.primary; if (!underline) color.a = .25f;
                for (auto [left, right] : Detail::TextSelectionSpans(line.display, options_.fontSize, a, b))
                    canvas.RoundedRect({x + left, underline ? y + line.height - 1 : y, right - left, underline ? 1.f : line.height}, 0, color);
            };
            if (focused_ && !selection.Empty()) paintRange(selection, false);
            if (!line.display.empty()) canvas.Text({x, y}, line.display, options_.fontSize, theme.onSurface);
            if (focused_ && editor_.GetValue().composing) paintRange(*editor_.GetValue().composing, true);
        }
        if (focused_ && caretVisible_ && selection.Empty()) {
            const auto [row, index] = CaretIndex(selection.caret); const auto& line = lines_[row];
            const float scale = canvas.GetPhysicalScale();
            const float caret = std::round((x + line.carets[index]) * scale) / scale;
            canvas.RoundedRect({caret, content.y + line.y - scrollY_, 1.f / scale, line.height}, 0, theme.primary);
        }
        canvas.PopClip();
        if (error_) { canvas.PushClip(bounds); canvas.Text({bounds.x + 12, box.y + box.height + 3}, *error_, 13, errorColor); canvas.PopClip(); }
        if (focused_ && touchSelection_ && !selection.Empty() && bounds.width >= 12) {
            for (bool anchor : {true, false}) { const auto p = HandlePoint(anchor); canvas.RoundedRect({p.x - 5, p.y - 5, 10, 10}, 5, theme.primary); }
        }
    }
    bool TextField::OnTick(float seconds)
    {
        if (!focused_ || !GetSelection().Empty()) return false;
        blink_ += seconds;
        if (blink_ < .5f) return false;
        blink_ = std::fmod(blink_, .5f); caretVisible_ = !caretVisible_; return true;
    }
}
