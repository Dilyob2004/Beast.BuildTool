#include "UI/Private/Font.h"
#include "UI/Private/TextMetricsCache.h"
#include "UI/Private/TextSegmentation.h"
#include "UI/Private/TextBidi.h"
#include "../../../ThirdParty/HarfBuzz/src/hb.h"
#include "../../../ThirdParty/HarfBuzz/src/hb-ot.h"
#include "UI/Private/UIFontData.generated.h"
#include "UI/Private/UIFontBold.generated.h"
#include "UI/Private/UIIcons.generated.h"
#include "UI/Private/UIFallbackFonts.generated.h"
#include "RHI/RHI.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <array>

// Only the trusted embedded font reaches stb. Suppress warnings in the
// unmodified third-party implementation, not in Beast's code.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../../ThirdParty/stb/imstb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace Beast::UI::Detail
{
    namespace
    {
        struct FontData { const unsigned char* bytes; unsigned length; };
        constexpr FontData EmbeddedFonts[]{
            {FontBytes, sizeof(FontBytes)}, {FontBoldBytes, sizeof(FontBoldBytes)}, {FontIconBytes, sizeof(FontIconBytes)},
            {FontDevanagariBytes, sizeof(FontDevanagariBytes)}, {FontDevanagariBoldBytes, sizeof(FontDevanagariBoldBytes)}, {FontSymbolsBytes, sizeof(FontSymbolsBytes)},
            {FontArabicBytes, sizeof(FontArabicBytes)}, {FontArabicBoldBytes, sizeof(FontArabicBoldBytes)},
            {FontHebrewBytes, sizeof(FontHebrewBytes)}, {FontHebrewBoldBytes, sizeof(FontHebrewBoldBytes)}
        };
        FontId Primary(FontFace face) {
            if (face < FontFace::Regular || face > FontFace::Icons) throw std::invalid_argument("Invalid UI font face.");
            return static_cast<FontId>(face);
        }
        const stbtt_fontinfo& Font(FontId id)
        {
            static const auto font = [] {
                std::array<stbtt_fontinfo, std::size(EmbeddedFonts)> value{};
                for (std::size_t i = 0; i < value.size(); ++i)
                    if (!stbtt_InitFont(&value[i], EmbeddedFonts[i].bytes, 0)) throw std::runtime_error("Invalid embedded Beast UI font.");
                return value;
            }();
            if (static_cast<std::size_t>(id) >= font.size()) throw std::invalid_argument("Invalid embedded font identity.");
            return font[static_cast<std::size_t>(id)];
        }
        const stbtt_fontinfo& Font(FontFace face) { return Font(Primary(face)); }
        bool Ignorable(hb_codepoint_t cp) {
            return hb_unicode_general_category(hb_unicode_funcs_get_default(), cp) == HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
                (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
        }
        std::vector<hb_codepoint_t> Decode(const std::string& text)
        {
            ValidateText(text);
            std::vector<hb_codepoint_t> points;
            for (std::size_t i = 0; i < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[i]);
                std::uint32_t cp = first; std::size_t length = 1;
                if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; length = 2; }
                else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; length = 3; }
                else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; length = 4; }
                else if (first >= 0x80) cp = 0xfffd;
                bool valid = i + length <= text.size();
                if (valid) for (std::size_t n = 1; n < length; ++n)
                {
                    const auto next = static_cast<unsigned char>(text[i + n]);
                    if ((next & 0xc0) != 0x80) { valid = false; break; }
                    cp = (cp << 6) | (next & 63);
                }
                if (!valid || (length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
                    (length == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                { cp = 0xfffd; length = 1; }
                if (cp < 32 || cp == 127) cp = ' '; // Single-line controls are spaces.
                points.push_back(cp); i += length;
            }
            return points;
        }
        void ValidateFontSize(float size)
        {
            if (!std::isfinite(size) || size < 1 || size > 256) throw std::invalid_argument("UI font size must be in [1,256].");
        }
        struct ShapedGlyph { int index; float x, y; FontId font; };
        // All positions/line metrics are in the primary font's units. Per-glyph
        // rasterization uses its actual font's UPEM, not the primary font's.
        struct ShapedCluster { std::size_t start, end; float left, right; bool rtl; std::vector<float> carets; };
        struct ShapedLine { std::vector<ShapedGlyph> glyphs; std::vector<float> carets; std::vector<ShapedCluster> clusters; float advance = 0, ascent = 0, descent = 0, gap = 0; };
        hb_font_t* ShapingFont(FontId id)
        {
            using FontPtr = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
            static const auto fonts = [] {
                std::vector<FontPtr> result;
                for (const auto& data : EmbeddedFonts) {
                    auto* blob = hb_blob_create(reinterpret_cast<const char*>(data.bytes), data.length, HB_MEMORY_MODE_READONLY, nullptr, nullptr);
                    auto* fontFace = hb_face_create(blob, 0); hb_blob_destroy(blob);
                    result.emplace_back(hb_font_create(fontFace), hb_font_destroy); hb_face_destroy(fontFace);
                    hb_ot_font_set_funcs(result.back().get()); hb_font_make_immutable(result.back().get());
                }
                return result;
            }();
            (void)Font(id); return fonts[static_cast<std::size_t>(id)].get();
        }
        std::shared_ptr<const ShapedLine> Shape(const std::string& text, FontFace face)
        {
            // Size/phase-independent cache. Bounded to 256 short runs; long
            // strings bypass it. No shared lock on the rendering thread.
            struct Entry { std::string text; FontFace face{}; std::shared_ptr<const ShapedLine> line; std::uint64_t used = 0; };
            thread_local std::array<std::array<Entry, 4>, 64> cache;
            thread_local std::uint64_t serial = 0;
            auto& set = cache[(std::hash<std::string>{}(text) ^ std::size_t(face)) % cache.size()];
            Entry* oldest = &set.front();
            for (auto& entry : set) {
                if (entry.line && entry.text == text && entry.face == face) { entry.used = ++serial; return entry.line; }
                if (entry.used < oldest->used) oldest = &entry;
            }
            const auto primary = Primary(face);
            auto points = Decode(text);
            auto line = std::make_shared<ShapedLine>(); line->carets.resize(points.size() + 1);
            const auto baseEm = hb_face_get_upem(hb_font_get_face(ShapingFont(primary)));
            const auto ratioFor = [&](FontId id) { return static_cast<float>(baseEm) / hb_face_get_upem(hb_font_get_face(ShapingFont(id))); };
            const auto metrics = [&](FontId id) {
                int a = 0, d = 0, g = 0; stbtt_GetFontVMetrics(&Font(id), &a, &d, &g);
                const float ratio = ratioFor(id);
                line->ascent = std::max(line->ascent, a * ratio);
                line->descent = std::min(line->descent, d * ratio);
                line->gap = std::max(line->gap, g * ratio);
            };
            metrics(primary);
            const std::u32string scalars(points.begin(), points.end());
            const auto boundaries = GraphemeBreaks(scalars);
            struct Segment { std::size_t start, end; FontId font; hb_script_t script; bool rtl = false; };
            std::vector<Segment> segments;
            const auto strong = [](hb_script_t script) { return script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED && script != HB_SCRIPT_UNKNOWN; };
            for (std::size_t n = 1; n < boundaries.size(); ++n) {
                const auto start = boundaries[n - 1], end = boundaries[n];
                const auto supports = [&](FontId id) {
                    for (auto s = start; s < end; ++s)
                        if (!Ignorable(points[s]) && !stbtt_FindGlyphIndex(&Font(id), static_cast<int>(points[s]))) return false;
                    return true;
                };
                FontId selected = primary;
                if (face != FontFace::Icons && !supports(primary)) {
                    for (auto candidate : {face == FontFace::Bold ? FontId::DevanagariBold : FontId::Devanagari, FontId::Symbols,
                        face == FontFace::Bold ? FontId::ArabicBold : FontId::Arabic, face == FontFace::Bold ? FontId::HebrewBold : FontId::Hebrew})
                        if (supports(candidate)) { selected = candidate; break; }
                }
                hb_script_t script = HB_SCRIPT_COMMON;
                for (auto s = start; s < end; ++s) {
                    const auto next = hb_unicode_script(hb_unicode_funcs_get_default(), points[s]);
                    if (!strong(script) && strong(next)) script = next;
                    // No font covers the whole cluster: retain a single font,
                    // preserving supported scalars and replacing missing ones.
                    if (!Ignorable(points[s]) && !stbtt_FindGlyphIndex(&Font(selected), static_cast<int>(points[s]))) points[s] = 0xfffd;
                }
                segments.push_back({start, end, selected, script});
            }
            // Common/inherited clusters follow the preceding strong script;
            // leading neutrals use the following script. Font boundaries remain.
            hb_script_t script = HB_SCRIPT_COMMON;
            for (auto& segment : segments) { if (strong(segment.script)) script = segment.script; else segment.script = script; }
            script = HB_SCRIPT_COMMON;
            for (auto it = segments.rbegin(); it != segments.rend(); ++it) { if (strong(it->script)) script = it->script; else it->script = script; }
            std::vector<Segment> visual;
            for (const auto& bidi : VisualBidiRuns(scalars)) {
                std::vector<Segment> pieces;
                for (auto segment : segments) {
                    segment.start = std::max(segment.start, bidi.start); segment.end = std::min(segment.end, bidi.end); segment.rtl = bidi.rtl;
                    if (segment.start >= segment.end) continue;
                    if (!pieces.empty() && pieces.back().end == segment.start && pieces.back().font == segment.font && pieces.back().script == segment.script) pieces.back().end = segment.end;
                    else pieces.push_back(segment);
                }
                if (bidi.rtl) std::reverse(pieces.begin(), pieces.end());
                visual.insert(visual.end(), pieces.begin(), pieces.end());
            }
            std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
            float pen = 0;
            for (const auto& segment : visual) {
                metrics(segment.font); const float ratio = ratioFor(segment.font);
                hb_buffer_clear_contents(buffer.get());
                hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                hb_buffer_set_direction(buffer.get(), segment.rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_buffer_set_script(buffer.get(), segment.script);
                hb_buffer_set_language(buffer.get(), hb_language_from_string("und", -1));
                hb_buffer_add_codepoints(buffer.get(), points.data(), static_cast<int>(points.size()), static_cast<unsigned>(segment.start), static_cast<int>(segment.end - segment.start));
                auto* font = ShapingFont(segment.font); hb_shape(font, buffer.get(), nullptr, 0);
                if (!hb_buffer_allocation_successful(buffer.get())) throw std::bad_alloc();
                unsigned count = 0;
                const auto* info = hb_buffer_get_glyph_infos(buffer.get(), &count);
                const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
                std::vector<std::size_t> clusterEnds{segment.end};
                for (unsigned i = 0; i < count; ++i) clusterEnds.push_back(info[i].cluster);
                std::sort(clusterEnds.begin(), clusterEnds.end());
                clusterEnds.erase(std::unique(clusterEnds.begin(), clusterEnds.end()), clusterEnds.end());
                for (unsigned i = 0; i < count;) {
                    const unsigned begin = i; const auto start = info[i].cluster;
                    const float startX = pen;
                    do {
                        line->glyphs.push_back({static_cast<int>(info[i].codepoint), pen + positions[i].x_offset * ratio, -positions[i].y_offset * ratio, segment.font});
                        pen += positions[i].x_advance * ratio; ++i;
                    } while (i < count && info[i].cluster == start);
                    const std::size_t end = *std::upper_bound(clusterEnds.begin(), clusterEnds.end(), start);
                    std::vector<std::size_t> stops;
                    for (auto it = std::upper_bound(boundaries.begin(), boundaries.end(), start); it != boundaries.end() && *it < end; ++it) stops.push_back(*it);
                    // Use font-supplied GDEF ligature carets where available, otherwise
                    // evenly split the advance between whole graphemes (not scalars).
                    std::vector<hb_position_t> ligature(stops.size());
                    unsigned caretCount = static_cast<unsigned>(ligature.size());
                    if (!segment.rtl && i == begin + 1 && caretCount) hb_ot_layout_get_ligature_carets(font, HB_DIRECTION_LTR, info[begin].codepoint, 0, &caretCount, ligature.data());
                    else caretCount = 0;
                    std::size_t stop = 0; float caret = segment.rtl ? pen : startX;
                    for (std::size_t s = start; s < end; ++s) {
                        if (stop < stops.size() && s == stops[stop]) {
                            caret = caretCount == stops.size() ? startX + ligature[stop] * ratio : startX + (pen - startX) * static_cast<float>(stop + 1) / static_cast<float>(stops.size() + 1);
                            caret = std::clamp(caret, startX, std::max(startX, pen)); ++stop;
                            if (segment.rtl) caret = pen - (caret - startX);
                        }
                        line->carets[s] = caret;
                    }
                    line->carets[end] = segment.rtl ? startX : pen;
                    line->clusters.push_back({start, end, startX, pen, segment.rtl, {line->carets.begin() + start, line->carets.begin() + end + 1}});
                }
            }
            line->advance = std::max(0.f, pen);
            if (text.size() <= 512) *oldest = {text, face, line, ++serial};
            return line;
        }
        Size MeasureLine(const std::string& text, float pixelSize, FontFace face) {
            const auto& font = Font(face); const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixelSize);
            const auto line = Shape(text, face);
            return {line->advance * scale, (line->ascent - line->descent + line->gap) * scale};
        }
    }
    void ValidateText(const std::string& text)
    { if (text.size() > 4096) throw std::invalid_argument("UI single-line text is limited to 4096 UTF-8 bytes."); }
    std::vector<float> TextCaretPositions(const std::string& text, float fontSize)
    {
        ValidateFontSize(fontSize);
        const auto& font = Font(FontFace::Regular);
        const float scale = stbtt_ScaleForMappingEmToPixels(&font, fontSize);
        auto positions = Shape(text, FontFace::Regular)->carets;
        for (auto& position : positions) position *= scale;
        return positions;
    }
    bool HasGlyph(char32_t codepoint, FontFace face)
    { return codepoint <= 0x10ffff && stbtt_FindGlyphIndex(&Font(face), static_cast<int>(codepoint)) != 0; }
    std::vector<std::pair<float, float>> TextSelectionSpans(const std::string& text, float size, std::size_t start, std::size_t end) {
        ValidateFontSize(size);
        const auto line = Shape(text, FontFace::Regular);
        const float scale = stbtt_ScaleForMappingEmToPixels(&Font(FontFace::Regular), size);
        std::vector<std::pair<float, float>> result;
        for (const auto& cluster : line->clusters) {
            const auto a = std::max(start, cluster.start), b = std::min(end, cluster.end);
            if (a >= b) continue;
            const float l = std::min(cluster.carets[a - cluster.start], cluster.carets[b - cluster.start]);
            const float r = std::max(cluster.carets[a - cluster.start], cluster.carets[b - cluster.start]);
            result.emplace_back(l * scale, r * scale);
        }
        std::sort(result.begin(), result.end());
        std::vector<std::pair<float, float>> merged;
        for (auto span : result) { if (!merged.empty() && span.first <= merged.back().second + .01f) merged.back().second = std::max(merged.back().second, span.second); else merged.push_back(span); }
        return merged;
    }
    Size MeasureText(const std::string& text, float fontSize, FontFace face)
    {
        ValidateFontSize(fontSize);
        ValidateText(text); (void)Font(face);
        // No shared locks on the UI path. Embedded fonts are immutable, so an
        // exact metrics entry remains valid across frames, DPI and surfaces.
        thread_local TextMetricsCache cache;
        return cache.Resolve(text, fontSize, face, [&] { return MeasureLine(text, fontSize, face); });
    }
    TextBitmap RasterizeText(const std::string& text, float pixelSize, int maxWidth, Point subpixelOrigin, int maxHeight, FontFace face)
    {
        // Logical sizes are <=256dp and Context supports scale <=16. Do not
        // cap a physical size at 256px and upscale its bitmap on high-DPI screens.
        if (!std::isfinite(pixelSize) || pixelSize <= 0 || pixelSize > 4096)
            throw std::invalid_argument("UI physical font size must be in (0,4096].");
        for (float offset : {subpixelOrigin.x, subpixelOrigin.y})
            if (!std::isfinite(offset) || offset < 0 || offset >= 1)
                throw std::invalid_argument("UI subpixel origin must be in [0,1).");
        if (text.empty() || maxWidth <= 0 || maxHeight <= 0) return {};
        maxWidth = std::min(maxWidth, 4096);
        maxHeight = std::min(maxHeight, 4096);
        const auto run = LayoutGlyphs(text, pixelSize, subpixelOrigin, face);
        const int height = std::max(1, std::min(maxHeight, run.height));
        const int width = std::max(1, std::min(maxWidth, run.width));
        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width) * height);
        for (const auto& item : run.glyphs)
        {
            const auto& font = Font(item.font);
            const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixelSize);
            const int gw = item.width, gh = item.height;
            if (item.x >= width || item.x + gw <= 0 || item.y >= height || item.y + gh <= 0) continue;
            if (gw > 0 && gh > 0)
            {
                std::vector<unsigned char> glyph(static_cast<std::size_t>(gw) * gh);
                stbtt_MakeGlyphBitmapSubpixel(&font, glyph.data(), gw, gh, gw, scale, scale, item.phaseX / 256.f, item.phaseY / 256.f, item.index);
                for (int row = 0; row < gh; ++row) for (int column = 0; column < gw; ++column)
                {
                    const int dx = item.x + column, dy = item.y + row;
                    if (dx < 0 || dx >= width || dy < 0 || dy >= height) continue;
                    auto& dst = coverage[static_cast<std::size_t>(dy) * width + dx];
                    const unsigned src = glyph[static_cast<std::size_t>(row) * gw + column];
                    dst = static_cast<std::uint8_t>(src + (dst * (255 - src) + 127) / 255);
                }
            }
        }
        RHITextureDesc texture; texture.width = static_cast<unsigned>(width); texture.height = static_cast<unsigned>(height);
        // Coverage already contains antialiasing and the subpixel phase. The
        // Canvas maps texels 1:1 to physical pixels; no second filtering pass.
        texture.filter = RHIFilter::Nearest;
        texture.pixels.reserve(coverage.size() * 4);
        for (auto alpha : coverage) for (int channel = 0; channel < 4; ++channel) texture.pixels.push_back(alpha);
        return {RHI::CreateTexture(std::move(texture)), 0, 0, static_cast<float>(width), static_cast<float>(height)};
    }
    GlyphRun LayoutGlyphs(const std::string& text, float pixelSize, Point phase, FontFace face)
    {
        if (!std::isfinite(pixelSize) || pixelSize <= 0 || pixelSize > 4096) throw std::invalid_argument("Invalid glyph pixel size.");
        for (float p : {phase.x, phase.y}) if (!std::isfinite(p) || p < 0 || p >= 1) throw std::invalid_argument("Invalid glyph run phase.");
        const auto shaped = Shape(text, face); const auto& font = Font(face);
        const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixelSize);
        GlyphRun run;
        run.width = std::clamp(static_cast<int>(std::ceil(shaped->advance * scale + phase.x)) + 2, 1, 4096);
        run.height = std::clamp(static_cast<int>(std::ceil((shaped->ascent - shaped->descent + shaped->gap) * scale + phase.y)), 1, 4096);
        const float baseline = shaped->ascent * scale + phase.y;
        // Quantization is much finer than the line origin's 1/64 grid and avoids
        // distinct cache keys from insignificant floating-point pen differences.
        const auto split = [](float position) {
            const auto fixed = static_cast<int>(std::round(double(position) * 256));
            const int pixel = static_cast<int>(std::floor(double(fixed) / 256));
            return std::pair{pixel, static_cast<std::uint16_t>(fixed - pixel * 256)};
        };
        int previousRight = 0;
        for (const auto& item : shaped->glyphs)
        {
            const auto [px, fx] = split(phase.x + item.x * scale);
            const auto [by, fy] = split(baseline + item.y * scale);
            const int glyph = item.index;
            const auto& actualFont = Font(item.font);
            const float glyphScale = stbtt_ScaleForMappingEmToPixels(&actualFont, pixelSize);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel(&actualFont, glyph, glyphScale, glyphScale, fx / 256.f, fy / 256.f, &x0, &y0, &x1, &y1);
            if (x1 > x0 && y1 > y0)
            {
                PositionedGlyph g{glyph, px + x0, by + y0, x1 - x0, y1 - y0, fx, fy, item.font};
                // Conservatively use horizontal overlap, including combining marks.
                run.overlaps |= !run.glyphs.empty() && g.x < previousRight;
                previousRight = std::max(previousRight, g.x + g.width);
                run.oversized |= g.width > 1022 || g.height > 1022;
                run.glyphs.push_back(g);
            }
        }
        return run;
    }
    std::vector<std::uint8_t> RasterizeGlyph(const PositionedGlyph& glyph, float pixelSize)
    {
        if (!std::isfinite(pixelSize) || pixelSize <= 0 || pixelSize > 4096 || glyph.width <= 0 || glyph.height <= 0 ||
            glyph.width > 1022 || glyph.height > 1022 || glyph.phaseX >= 256 || glyph.phaseY >= 256)
            throw std::invalid_argument("Invalid atlas glyph.");
        const auto& font = Font(glyph.font);
        if (glyph.index < 0 || glyph.index >= font.numGlyphs) throw std::invalid_argument("Invalid glyph index.");
        const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixelSize);
        std::vector<std::uint8_t> coverage(std::size_t(glyph.width) * glyph.height);
        stbtt_MakeGlyphBitmapSubpixel(&font, coverage.data(), glyph.width, glyph.height, glyph.width,
            scale, scale, glyph.phaseX / 256.f, glyph.phaseY / 256.f, glyph.index);
        return coverage;
    }
}
