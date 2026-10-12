#pragma once
#include <string_view>
#include <vector>
#include <cstddef>
namespace Beast::UI::Detail {
    struct BidiRun { std::size_t start, end; bool rtl; };
    std::vector<BidiRun> VisualBidiRuns(std::u32string_view text);
}
