#include "UI/Private/TextSegmentation.h"
#include "UI/Private/UnicodeData.h"
#include <algorithm>
#include <iterator>
namespace Beast::UI::Detail {
namespace {
    using namespace UnicodeData;
    template<class T, std::size_t N> auto Property(char32_t cp, const T (&table)[N]) {
        const auto it = std::lower_bound(std::begin(table), std::end(table), cp,
            [](const T& r, char32_t c) { return r.last < c; });
        return it != std::end(table) && it->first <= cp ? it->value : decltype(it->value)::Other;
    }
    bool Control(G p) { return p == G::Control || p == G::CR || p == G::LF; }
    bool Newline(W p) { return p == W::Newline || p == W::CR || p == W::LF; }
    bool Ignore(W p) { return p == W::Extend || p == W::Format || p == W::ZWJ; }
    bool Letter(W p) { return p == W::ALetter || p == W::Hebrew_Letter; }
    bool MidLetter(W p) { return p == W::MidLetter || p == W::MidNumLet || p == W::Single_Quote; }
    bool MidNumber(W p) { return p == W::MidNum || p == W::MidNumLet || p == W::Single_Quote; }
    bool WordPart(W p) { return Letter(p) || p == W::Numeric || p == W::Katakana || p == W::ExtendNumLet; }
}
std::vector<std::size_t> GraphemeBreaks(std::u32string_view text) {
    using namespace UnicodeData;
    std::vector<std::size_t> result{0};
    G previous = G::Other;
    std::size_t regional = 0;
    bool pictographicExtend = false, previousEmojiZwj = false;
    bool indicConsonant = false, indicLinker = false;
    for (std::size_t n = 0; n < text.size(); ++n) {
        const auto current = Property(text[n], GRanges);
        const auto indic = Property(text[n], IRanges);
        const bool emoji = Property(text[n], ERanges) == E::Yes;
        bool split = true;
        if (previous == G::CR && current == G::LF) split = false;
        else if (Control(previous) || Control(current)) {}
        else if (previous == G::L && (current == G::L || current == G::V || current == G::LV || current == G::LVT)) split = false;
        else if ((previous == G::LV || previous == G::V) && (current == G::V || current == G::T)) split = false;
        else if ((previous == G::LVT || previous == G::T) && current == G::T) split = false;
        else if (current == G::Extend || current == G::ZWJ || current == G::SpacingMark || previous == G::Prepend) split = false;
        else if (indic == I::Consonant && indicConsonant && indicLinker) split = false;
        else if (previousEmojiZwj && emoji) split = false;
        else if (current == G::Regional_Indicator && regional % 2 == 1) split = false;
        if (n && split) result.push_back(n);
        regional = current == G::Regional_Indicator ? regional + 1 : 0;
        previousEmojiZwj = current == G::ZWJ && pictographicExtend;
        pictographicExtend = emoji || (current == G::Extend && pictographicExtend);
        if (indic == I::Consonant) { indicConsonant = true; indicLinker = false; }
        else if (indic == I::Linker) indicLinker = indicConsonant;
        else if (indic != I::Extend) { indicConsonant = false; indicLinker = false; }
        previous = current;
    }
    if (!text.empty()) result.push_back(text.size());
    return result;
}
std::vector<std::size_t> WordBreaks(std::u32string_view text) {
    using namespace UnicodeData;
    const auto size = text.size();
    std::vector<W> properties; properties.reserve(size);
    for (auto cp : text) properties.push_back(Property(cp, WRanges));
    // Significant neighbours and RI counts keep processing linear.
    std::vector<std::size_t> left(size + 1, size), right(size + 1, size), regional(size, 0);
    for (std::size_t n = 0; n < size; ++n) {
        left[n + 1] = Ignore(properties[n]) ? left[n] : n;
        if (properties[n] == W::Regional_Indicator) regional[n] = 1 + (left[n] < size ? regional[left[n]] : 0);
    }
    for (auto n = size; n > 0; --n) right[n - 1] = Ignore(properties[n - 1]) ? right[n] : n - 1;
    const auto at = [&](std::size_t n) { return n < size ? properties[n] : W::Other; };
    std::vector<std::size_t> result{0};
    for (std::size_t n = 1; n < size; ++n) {
        const auto immediate = properties[n - 1], b = properties[n];
        const auto l = left[n]; const auto a = at(l);
        const auto before = l < size ? at(left[l]) : W::Other, after = at(right[n + 1]);
        bool split = true;
        if (immediate == W::CR && b == W::LF) split = false;
        else if (Newline(immediate) || Newline(b)) {}
        else if (immediate == W::ZWJ && Property(text[n], ERanges) == E::Yes) split = false;
        else if (immediate == W::WSegSpace && b == W::WSegSpace) split = false;
        else if (Ignore(b)) split = false;
        else if (Letter(a) && Letter(b)) split = false;
        else if (Letter(a) && MidLetter(b) && Letter(after)) split = false;
        else if (Letter(before) && MidLetter(a) && Letter(b)) split = false;
        else if (a == W::Hebrew_Letter && b == W::Single_Quote) split = false;
        else if (a == W::Hebrew_Letter && b == W::Double_Quote && after == W::Hebrew_Letter) split = false;
        else if (before == W::Hebrew_Letter && a == W::Double_Quote && b == W::Hebrew_Letter) split = false;
        else if ((a == W::Numeric && b == W::Numeric) || (Letter(a) && b == W::Numeric) || (a == W::Numeric && Letter(b))) split = false;
        else if (before == W::Numeric && MidNumber(a) && b == W::Numeric) split = false;
        else if (a == W::Numeric && MidNumber(b) && after == W::Numeric) split = false;
        else if (a == W::Katakana && b == W::Katakana) split = false;
        else if ((WordPart(a) && b == W::ExtendNumLet) || (a == W::ExtendNumLet && WordPart(b))) split = false;
        else if (a == W::Regional_Indicator && b == W::Regional_Indicator && regional[l] % 2 == 1) split = false;
        if (split) result.push_back(n);
    }
    if (size) result.push_back(size);
    return result;
}
}
