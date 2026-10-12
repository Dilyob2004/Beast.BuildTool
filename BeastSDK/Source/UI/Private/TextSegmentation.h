#pragma once
#include <string_view>
#include <vector>
#include <cstddef>
namespace Beast::UI::Detail {
    // UAX #29 revision 47, Unicode 17.0.0. Scalar indices, including 0/end.
    // Default segmentation, without dictionary tailoring or text shaping.
    std::vector<std::size_t> GraphemeBreaks(std::u32string_view text);
    std::vector<std::size_t> WordBreaks(std::u32string_view text);
}
