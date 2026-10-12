#include "UI/UI.h"
#include "UI/Private/Font.h"
#include <cstdio>
#include <source_location>
#include <fstream>
#include <cstring>
using namespace Beast;
using namespace Beast::UI;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Font fallback test at line " + std::to_string(at.line()));
}
void Test() {
    using namespace Beast::UI::Detail;
    Check(!HasGlyph(U'क', FontFace::Regular) && !HasGlyph(U'♜', FontFace::Regular));
    for (auto face : {FontFace::Regular, FontFace::Bold}) {
        const auto expected = face == FontFace::Bold ? FontId::DevanagariBold : FontId::Devanagari;
        for (const std::string text : {"क", "कि", "क्षि", "नमस्ते"}) {
            const auto run = LayoutGlyphs(text, 32, {}, face);
            Check(!run.glyphs.empty());
            for (const auto& g : run.glyphs) { Check(g.font == expected && g.index != 0); Check(!RasterizeGlyph(g, 32).empty()); }
            const auto mixed = LayoutGlyphs("ffi " + text + " ffi", 32, {}, face);
            std::vector<int> local, embedded;
            for (const auto& g : run.glyphs) local.push_back(g.index);
            for (const auto& g : mixed.glyphs) if (g.font == expected) embedded.push_back(g.index);
            Check(local == embedded); // Script selection must not depend on first Latin letter.
        }
        const auto symbol = LayoutGlyphs("♜", 32, {}, face);
        Check(symbol.glyphs.size() == 1 && symbol.glyphs[0].font == FontId::Symbols && symbol.glyphs[0].index != 0);
    }
    for (float size : {12.f, 32.f, 83.5f}) {
        for (const std::string text : {"ffi कि ♜ café", "क्षि", "नमस्ते", "♜"}) {
            const auto metrics = MeasureText(text, size);
            const auto caret = TextCaretPositions(text, size);
            Check(std::abs(caret.back() - metrics.width) < .001f && std::is_sorted(caret.begin(), caret.end()));
            const auto raster = RasterizeText(text, size, 4096);
            Check(raster.texture && raster.height >= metrics.height && raster.width >= metrics.width);
            const auto scaled = MeasureText(text, size * 2);
            Check(std::abs(scaled.width - metrics.width * 2) < .001f && std::abs(scaled.height - metrics.height * 2) < .001f);
        }
    }
    // Missing coverage is deterministic; never borrow Material Icons for text.
    const auto unknown = LayoutGlyphs("\U0010ffff", 32, {}, FontFace::Regular);
    Check(!unknown.glyphs.empty() && unknown.glyphs[0].font == FontId::Regular);
    const auto icons = LayoutGlyphs("क♜", 32, {}, FontFace::Icons);
    for (const auto& glyph : icons.glyphs) Check(glyph.font == FontId::Icons);
    // Returning to an earlier mixed run gives identical metrics after cache churn.
    const auto before = MeasureText("Latin कि ♜", 24);
    for (int i = 0; i < 800; ++i) LayoutGlyphs("नमस्ते " + std::to_string(i), 24, {}, FontFace::Regular);
    const auto after = MeasureText("Latin कि ♜", 24);
    Check(before.width == after.width && before.height == after.height);
    Window window; window.width = window.contentWidth = 500; window.height = window.contentHeight = 300;
    auto field = std::make_shared<TextField>("a", TextFieldOptions{.fontSize = 32});
    Context ui; ui.SetRoot(std::make_shared<Column>(std::vector<WidgetPtr>{field})); ui.BuildFrame(window, 0);
    const float originalHeight = field->GetBounds().height;
    field->SetText("कि ♜"); ui.BuildFrame(window, 0);
    Check(field->GetBounds().height >= MeasureText(field->GetText(), 32).height + 24);
    field->SetText("a"); ui.BuildFrame(window, 0); Check(field->GetBounds().height == originalHeight);
    // Keep a reproducible, CPU-rasterized specimen for visual inspection.
    constexpr int width = 900, height = 360;
    std::vector<unsigned char> pixels(width * height * 4, 255);
    const std::array<std::string, 4> lines{"Regular: ffi café Привет", "Mixed: ffi कि क्षि नमस्ते ♜", "Bold: ffi कि क्षि नमस्ते ♜", "Symbols: ♜ ♞ ♻ ⚓"};
    for (int row = 0; row < 4; ++row) {
        const auto bitmap = RasterizeText(lines[row], 36, width - 40, {}, 80, row == 2 ? FontFace::Bold : FontFace::Regular);
        const auto& desc = bitmap.texture->GetDesc();
        for (unsigned y = 0; y < desc.height; ++y) for (unsigned x = 0; x < desc.width; ++x) {
            const auto offset = ((row * 90 + y) * width + x + 20) * 4;
            for (int c = 0; c < 3; ++c) pixels[offset + c] = static_cast<unsigned char>(255 - desc.pixels[(y * desc.width + x) * 4]);
        }
    }
    std::array<unsigned char, 54> header{}; header[0] = 'B'; header[1] = 'M';
    const auto put = [&](int offset, std::int32_t value) { std::memcpy(header.data() + offset, &value, 4); };
    put(2, 54 + width * height * 4); put(10, 54); put(14, 40); put(18, width); put(22, -height); header[26] = 1; header[28] = 32;
    std::ofstream out("Test/EcoSbor/Intermediate/Verification/font-fallback.bmp", std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    Check(out.good());
    std::puts("Font fallback: whole clusters, mixed LTR scripts, bold, symbols, metrics/carets/DPI, cache and TextField height passed.");
}
}
int main() { try { Test(); return 0; } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; } }
