#include "UI/Private/TextLayout.h"
#include "UI/TextEditing.h"
namespace Beast::UI::Detail {
std::vector<TextLine> WrapText(const std::string& text, float width, float size, FontFace face) {
    if (std::isnan(width) || width < 0) throw std::invalid_argument("Invalid text wrapping width.");
    const auto clusters = TextEditor::GraphemeBoundaries(text, true);
    std::vector<TextLine> result;
    const auto emit = [&](std::size_t start, std::size_t end, std::size_t next) {
        const auto measured = MeasureText(text.substr(start, end - start), size, face);
        result.push_back({start, end, next, measured.width, measured.height});
    };
    std::size_t start = 0, n = 0, breakAt = 0;
    while (n + 1 < clusters.size()) {
        const auto a = clusters[n], b = clusters[n + 1];
        if (text[a] == '\n' || text[a] == '\r') { emit(clusters[start], a, b); start = ++n; breakAt = start; continue; }
        const float measured = MeasureText(text.substr(clusters[start], b - clusters[start]), size, face).width;
        if (n > start && measured > width) {
            const auto end = breakAt > start ? breakAt : n;
            emit(clusters[start], clusters[end], clusters[end]); start = n = end; breakAt = start; continue;
        }
        ++n;
        if (text[a] == ' ' || text[a] == '-' || text[a] == '\t') breakAt = n;
    }
    emit(clusters[start], text.size(), text.size());
    return result;
}
}
