#include "UI/Private/TextBidi.h"
#include <SheenBidi/SheenBidi.h>
#include <memory>
#include <new>
namespace Beast::UI::Detail {
std::vector<BidiRun> VisualBidiRuns(std::u32string_view text) {
    if (text.empty()) return {};
    const SBCodepointSequence sequence{SBStringEncodingUTF32, text.data(), text.size()};
    auto algorithm = SBAlgorithmCreate(&sequence);
    if (!algorithm) throw std::bad_alloc();
    std::unique_ptr<const _SBAlgorithm, decltype(&SBAlgorithmRelease)> a(algorithm, SBAlgorithmRelease);
    auto paragraph = SBAlgorithmCreateParagraph(algorithm, 0, text.size(), SBLevelDefaultLTR);
    if (!paragraph) throw std::bad_alloc();
    std::unique_ptr<const _SBParagraph, decltype(&SBParagraphRelease)> p(paragraph, SBParagraphRelease);
    auto line = SBParagraphCreateLine(paragraph, 0, SBParagraphGetLength(paragraph));
    if (!line) throw std::bad_alloc();
    std::unique_ptr<const _SBLine, decltype(&SBLineRelease)> l(line, SBLineRelease);
    std::vector<BidiRun> result;
    const auto runs = SBLineGetRunsPtr(line);
    for (SBUInteger i = 0; i < SBLineGetRunCount(line); ++i) result.push_back({runs[i].offset, runs[i].offset + runs[i].length, (runs[i].level & 1) != 0});
    return result;
}
}
