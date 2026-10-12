#pragma once
#include "UI/Private/Font.h"
namespace Beast::UI::Detail {
    struct TextLine { std::size_t start = 0, end = 0, next = 0; float width = 0, height = 0; };
    // UTF-8 offsets. Hard line separators are excluded from end, included in
    // next. Soft wraps preserve all bytes and never split an extended grapheme.
    std::vector<TextLine> WrapText(const std::string&, float width, float size, FontFace face = FontFace::Regular);
}
