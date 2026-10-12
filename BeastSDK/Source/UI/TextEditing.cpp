#include "UI/TextEditing.h"
#include "UI/Private/TextSegmentation.h"
#include <stdexcept>
#include <cstdint>

namespace Beast::UI
{
    namespace {
        std::vector<std::size_t> Segment(const std::string& text, bool words, bool multiline) {
            const auto bytes = TextEditor::Boundaries(text, multiline);
            std::u32string scalars;
            for (std::size_t n = 1; n < bytes.size(); ++n) {
                const auto length = bytes[n] - bytes[n - 1];
                auto cp = static_cast<char32_t>(static_cast<unsigned char>(text[bytes[n - 1]]));
                if (length > 1) {
                    cp &= (1u << (7u - static_cast<unsigned>(length))) - 1;
                    for (auto i = bytes[n - 1] + 1; i < bytes[n]; ++i) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 63);
                }
                scalars.push_back(cp);
            }
            auto breaks = words ? Detail::WordBreaks(scalars) : Detail::GraphemeBreaks(scalars);
            for (auto& offset : breaks) offset = bytes[offset];
            return breaks;
        }
    }
    std::vector<std::size_t> TextEditor::GraphemeBoundaries(const std::string& text, bool multiline) { return Segment(text, false, multiline); }
    std::vector<std::size_t> TextEditor::WordBoundaries(const std::string& text, bool multiline) { return Segment(text, true, multiline); }
    std::vector<std::size_t> TextEditor::Boundaries(const std::string& text, bool multiline)
    {
        if (text.size() > 4096) throw std::invalid_argument("Text editing is limited to 4096 UTF-8 bytes.");
        std::vector<std::size_t> result{0};
        for (std::size_t i = 0; i < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[i]);
            std::uint32_t cp = first; std::size_t length = 1;
            if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; length = 2; }
            else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; length = 3; }
            else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; length = 4; }
            else if (first >= 0x80) throw std::invalid_argument("Invalid UTF-8 in text editor.");
            if (i + length > text.size()) throw std::invalid_argument("Truncated UTF-8 in text editor.");
            for (std::size_t n = 1; n < length; ++n)
            {
                const auto next = static_cast<unsigned char>(text[i + n]);
                if ((next & 0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation.");
                cp = (cp << 6) | (next & 63);
            }
            if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
                (length == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
                (cp < 32 && !(multiline && (cp == '\n' || cp == '\r' || cp == '\t'))) || (cp >= 0x7f && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029)
                throw std::invalid_argument("Invalid scalar or line control in single-line editor.");
            i += length; result.push_back(i);
        }
        return result;
    }
    std::string TextEditor::Encode(char32_t cp)
    {
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw std::invalid_argument("Invalid Unicode scalar.");
        std::string text;
        if (cp < 0x80) text += static_cast<char>(cp);
        else if (cp < 0x800) { text += static_cast<char>(0xc0 | (cp >> 6)); text += static_cast<char>(0x80 | (cp & 63)); }
        else if (cp < 0x10000) {
            text += static_cast<char>(0xe0 | (cp >> 12)); text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63));
        } else {
            text += static_cast<char>(0xf0 | (cp >> 18)); text += static_cast<char>(0x80 | ((cp >> 12) & 63));
            text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63));
        }
        return text;
    }
    TextEditor::TextEditor(std::string text, std::size_t maxBytes, bool multiline) : maxBytes_(maxBytes), multiline_(multiline)
    {
        if (!maxBytes || maxBytes > 4096) throw std::invalid_argument("TextField maxBytes must be in [1,4096].");
        SetText(std::move(text));
    }
    void TextEditor::SetText(std::string text)
    {
        Boundaries(text, multiline_);
        if (text.size() > maxBytes_) throw std::invalid_argument("TextField text exceeds maxBytes.");
        const auto end = text.size(); value_ = {std::move(text), {end, end}, {}}; undo_.clear(); redo_.clear(); compositionBefore_.reset();
    }
    void TextEditor::Select(TextSelection selection)
    {
        const auto boundaries = Boundaries(value_.text, multiline_);
        if (!std::binary_search(boundaries.begin(), boundaries.end(), selection.anchor) ||
            !std::binary_search(boundaries.begin(), boundaries.end(), selection.caret))
            throw std::invalid_argument("Text selection must be on UTF-8 scalar boundaries.");
        FinishComposition();
        value_.selection = selection;
    }
    void TextEditor::Remember()
    {
        // At most ~256 KiB of text per undo stack. No global or platform state.
        if (undo_.size() == 64) undo_.erase(undo_.begin());
        undo_.push_back(value_); redo_.clear();
    }
    bool TextEditor::ReplaceSelection(const std::string& text)
    {
        try { Boundaries(text, multiline_); } catch (const std::invalid_argument&) { return false; }
        const auto range = value_.selection;
        if (value_.text.size() - (range.End() - range.Start()) + text.size() > maxBytes_) return false;
        FinishComposition();
        auto next = value_.text; next.replace(range.Start(), range.End() - range.Start(), text);
        const auto end = range.Start() + text.size();
        if (next != value_.text) { Remember(); value_.text = std::move(next); }
        value_.selection = {end, end}; return true;
    }
    bool TextEditor::Delete(bool backwards)
    {
        FinishComposition();
        const auto boundaries = GraphemeBoundaries(value_.text, multiline_);
        auto range = value_.selection;
        // A caret supplied by an IME can be inside a cluster. Manual deletion
        // removes that whole cluster, never leaving half an emoji/diacritic.
        auto start = std::upper_bound(boundaries.begin(), boundaries.end(), range.Start()); --start;
        auto end = std::lower_bound(boundaries.begin(), boundaries.end(), range.End());
        if (range.Empty() && *start == *end) {
            if (backwards && start != boundaries.begin()) --start;
            else if (!backwards && end + 1 != boundaries.end()) ++end;
        }
        range = {*start, *end};
        if (range.Empty()) return false;
        Remember();
        value_.text.erase(range.Start(), range.End() - range.Start());
        value_.selection = {range.Start(), range.Start()}; return true;
    }
    void TextEditor::Move(bool backwards, bool extend)
    {
        FinishComposition();
        const auto range = value_.selection;
        const auto boundaries = GraphemeBoundaries(value_.text, multiline_);
        if (!extend && !range.Empty()) {
            auto it = std::lower_bound(boundaries.begin(), boundaries.end(), backwards ? range.Start() : range.End());
            if (backwards && *it > range.Start()) --it;
            Select({*it, *it}); return;
        }
        auto it = std::lower_bound(boundaries.begin(), boundaries.end(), range.caret);
        if (backwards && it != boundaries.begin()) --it;
        else if (!backwards && *it == range.caret && it + 1 != boundaries.end()) ++it;
        value_.selection = {extend ? range.anchor : *it, *it};
    }
    void TextEditor::MoveToEdge(bool end, bool extend)
    { const auto at = end ? value_.text.size() : 0; Select({extend ? value_.selection.anchor : at, at}); }
    bool TextEditor::Undo()
    {
        FinishComposition();
        if (undo_.empty()) return false;
        redo_.push_back(value_); value_ = std::move(undo_.back()); undo_.pop_back(); return true;
    }
    bool TextEditor::Redo()
    {
        FinishComposition();
        if (redo_.empty()) return false;
        undo_.push_back(value_); value_ = std::move(redo_.back()); redo_.pop_back(); return true;
    }
    std::u16string TextEditor::ToUtf16(const std::string& text)
    {
        const auto boundaries = Boundaries(text, true); std::u16string result;
        for (std::size_t n = 1; n < boundaries.size(); ++n) {
            const auto begin = boundaries[n - 1], end = boundaries[n];
            auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(text[begin]));
            if (end - begin > 1) {
                cp &= (1u << (7u - static_cast<unsigned>(end - begin))) - 1;
                for (auto i = begin + 1; i < end; ++i) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 63);
            }
            if (cp < 0x10000) result.push_back(static_cast<char16_t>(cp));
            else { cp -= 0x10000; result.push_back(static_cast<char16_t>(0xd800 + (cp >> 10))); result.push_back(static_cast<char16_t>(0xdc00 + (cp & 1023))); }
        }
        return result;
    }
    std::string TextEditor::FromUtf16(const std::u16string& text, bool multiline)
    {
        if (text.size() > 4096) throw std::invalid_argument("IME text too long.");
        std::string result;
        for (std::size_t i = 0; i < text.size(); ++i) {
            char32_t cp = text[i];
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (++i >= text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) throw std::invalid_argument("Unpaired UTF-16 surrogate.");
                cp = 0x10000 + ((cp - 0xd800) << 10) + (text[i] - 0xdc00);
            } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::invalid_argument("Unpaired UTF-16 surrogate.");
            result += Encode(cp);
        }
        Boundaries(result, multiline); return result;
    }
    std::size_t TextEditor::Utf16ToByte(const std::string& text, int offset)
    {
        if (offset < 0) throw std::invalid_argument("Negative UTF-16 offset.");
        int units = 0;
        const auto boundaries = Boundaries(text, true);
        for (std::size_t n = 0; n < boundaries.size(); ++n) {
            if (units == offset) return boundaries[n];
            if (n + 1 < boundaries.size()) units += boundaries[n + 1] - boundaries[n] == 4 ? 2 : 1;
        }
        throw std::invalid_argument("UTF-16 offset splits a scalar or exceeds text.");
    }
    int TextEditor::ByteToUtf16(const std::string& text, std::size_t offset)
    {
        const auto boundaries = Boundaries(text, true);
        if (!std::binary_search(boundaries.begin(), boundaries.end(), offset)) throw std::invalid_argument("Invalid UTF-8 offset.");
        return static_cast<int>(ToUtf16(text.substr(0, offset)).size());
    }
    void TextEditor::FinishComposition()
    {
        value_.composing.reset();
        if (compositionBefore_) {
            if (compositionBefore_->text != value_.text) {
                if (undo_.size() == 64) undo_.erase(undo_.begin());
                undo_.push_back(std::move(*compositionBefore_)); redo_.clear();
            }
            compositionBefore_.reset();
        }
    }
    bool TextEditor::ApplyIme(TextEditingValue next)
    {
        try {
            const auto boundaries = Boundaries(next.text, multiline_);
            const auto valid = [&](TextSelection range) { return std::binary_search(boundaries.begin(), boundaries.end(), range.anchor) && std::binary_search(boundaries.begin(), boundaries.end(), range.caret); };
            if (next.text.size() > maxBytes_ || !valid(next.selection) || (next.composing && (!valid(*next.composing) || next.composing->Empty()))) return false;
        } catch (const std::invalid_argument&) { return false; }
        if (next.composing && !compositionBefore_) compositionBefore_ = value_;
        if (!compositionBefore_ && value_.text != next.text) Remember();
        value_ = std::move(next);
        if (!value_.composing) FinishComposition();
        return true;
    }
}
