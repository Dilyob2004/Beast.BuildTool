#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>

namespace Beast::UI
{
    // UTF-8 byte offsets. IME/programmatic selections may use scalar boundaries;
    // manual movement and deletion use extended grapheme clusters.
    struct TextSelection
    {
        std::size_t anchor = 0, caret = 0;
        std::size_t Start() const { return std::min(anchor, caret); }
        std::size_t End() const { return std::max(anchor, caret); }
        bool Empty() const { return anchor == caret; }
        bool operator==(const TextSelection&) const = default;
    };
    struct TextEditingValue
    {
        std::string text;
        TextSelection selection;
        std::optional<TextSelection> composing;
        bool operator==(const TextEditingValue&) const = default;
    };
    // Single-line editing model, independent of the window/render backend.
    // Invalid UTF-8, controls and over-limit edits are rejected atomically.
    class TextEditor
    {
        TextEditingValue value_;
        std::size_t maxBytes_;
        bool multiline_ = false;
        std::vector<TextEditingValue> undo_, redo_;
        std::optional<TextEditingValue> compositionBefore_;
        void Remember();
    public:
        explicit TextEditor(std::string text = {}, std::size_t maxBytes = 4096, bool multiline = false);
        static std::vector<std::size_t> Boundaries(const std::string& text, bool multiline = false);
        static std::vector<std::size_t> GraphemeBoundaries(const std::string& text, bool multiline = false);
        static std::vector<std::size_t> WordBoundaries(const std::string& text, bool multiline = false);
        static std::string Encode(char32_t character);
        static std::u16string ToUtf16(const std::string& text);
        static std::string FromUtf16(const std::u16string& text, bool multiline = false);
        static std::size_t Utf16ToByte(const std::string& text, int offset);
        static int ByteToUtf16(const std::string& text, std::size_t offset);
        // Atomic full-state IME update. Composition is one undo transaction.
        bool ApplyIme(TextEditingValue value);
        void FinishComposition();
        const TextEditingValue& GetValue() const noexcept { return value_; }
        // Programmatic replacement starts a new history; no user callback.
        void SetText(std::string text);
        void Select(TextSelection selection);
        void SelectAll() { Select({0, value_.text.size()}); }
        bool ReplaceSelection(const std::string& text);
        bool Delete(bool backwards);
        void Move(bool backwards, bool extend = false);
        void MoveToEdge(bool end, bool extend = false);
        bool Undo();
        bool Redo();
    };
}
