#pragma once
#include "UI/Geometry.h"
#include "RHI/RHIResources.h"
#include <string>

namespace Beast::UI::Detail
{
    void ValidateText(const std::string& text);
    bool HasGlyph(char32_t codepoint, FontFace face);
    // Immutable embedded font identity, independent of the requested UI style.
    enum class FontId : std::uint8_t { Regular, Bold, Icons, Devanagari, DevanagariBold, Symbols, Arabic, ArabicBold, Hebrew, HebrewBold };
    Size MeasureText(const std::string& text, float fontSize, FontFace face = FontFace::Regular);
    // LTR scalar positions derived from shaped clusters, shared with rendering.
    // Ligature carets use GDEF or grapheme-based interpolation. Not bidi.
    std::vector<float> TextCaretPositions(const std::string& text, float fontSize);
    // Logical scalar range -> possibly disjoint visual x intervals.
    std::vector<std::pair<float, float>> TextSelectionSpans(const std::string&, float size, std::size_t start, std::size_t end);
    struct TextBitmap
    {
        RHITextureHandle texture;
        float x = 0, y = 0, width = 0, height = 0; // Raster pixels from line origin.
    };
    // Exact physical em size, not rounded to an integer. Bake the fractional
    // line origin into coverage, then display the bitmap on integer pixel edges.
    TextBitmap RasterizeText(const std::string& text, float pixelSize, int maxWidth,
        Point subpixelOrigin = {}, int maxHeight = 4096, FontFace face = FontFace::Regular);
    struct PositionedGlyph
    {
        int index = 0, x = 0, y = 0, width = 0, height = 0;
        std::uint16_t phaseX = 0, phaseY = 0; // 1/256 physical pixel.
        FontId font = FontId::Regular;
    };
    struct GlyphRun
    {
        std::vector<PositionedGlyph> glyphs;
        int width = 0, height = 0; // Same line clipping as RasterizeText.
        bool overlaps = false, oversized = false;
    };
    // HarfBuzz LTR run shared by atlas and line rasterization. Existing embedded
    // fonts with grapheme fallback/script runs; paragraph bidi is separate.
    GlyphRun LayoutGlyphs(const std::string& text, float pixelSize, Point phase, FontFace face);
    std::vector<std::uint8_t> RasterizeGlyph(const PositionedGlyph&, float pixelSize);
}
