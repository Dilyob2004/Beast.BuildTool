#pragma once
#include <cstdint>
#include <string>
#include <optional>

namespace Beast
{
    // Platform protocol uses UTF-16 offsets (Android InputConnection units).
    // Session changes on focus; revision changes only for native authoritative edits.
    struct NativeTextState
    {
        std::uint64_t session = 0, revision = 0;
        std::u16string text;
        int anchor = 0, caret = 0, composingStart = -1, composingEnd = -1;
        int action = 0; // 0 edit, 1 submit, 2 keyboard dismissed
        bool multiline = false, password = false;
        bool operator==(const NativeTextState&) const = default;
    };
    class Clipboard
    {
    public:
        virtual ~Clipboard() = default;
        virtual std::optional<std::u16string> ReadClipboard() { return {}; }
        virtual bool WriteClipboard(const std::u16string&) { return false; }
    };
    class TextInput : public Clipboard
    {
    public:
        virtual ~TextInput() = default;
        virtual void Update(const NativeTextState& state, int maxBytes) = 0;
        virtual void Close(std::uint64_t session) = 0;
        virtual std::optional<NativeTextState> Poll() = 0;
    };
}
