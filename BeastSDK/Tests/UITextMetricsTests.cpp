#include "UI/Private/Font.h"
#include "UI/Private/TextMetricsCache.h"
#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace Beast::UI;
namespace {
    void Check(bool ok) { if (!ok) throw std::runtime_error("Text metrics assertion failed"); }
    template<class F> void Reject(F f) { bool rejected = false; try { f(); } catch (const std::logic_error&) { rejected = true; } Check(rejected); }
    void CacheTests() {
        Detail::TextMetricsCache cache; int calls = 0;
        const auto compute = [&] { ++calls; return Size{12, 24}; };
        cache.Resolve("a", 14, FontFace::Regular, compute);
        cache.Resolve("a", 14, FontFace::Regular, compute); Check(calls == 1);
        cache.Resolve("b", 14, FontFace::Regular, compute);
        cache.Resolve("a", 14.00001f, FontFace::Regular, compute);
        cache.Resolve("a", 14, FontFace::Bold, compute); Check(calls == 4);
        const std::string large(513, 'a');
        cache.Resolve(large, 14, FontFace::Regular, compute); cache.Resolve(large, 14, FontFace::Regular, compute);
        Check(calls == 6 && cache.GetStats().bypasses == 2);
        for (int i = 0; i < 4096; ++i) {
            const auto text = std::to_string(i);
            const auto result = cache.Resolve(text, 14, FontFace::Regular, [i] { return Size{float(i), 20}; });
            Check(result.width == float(i));
        }
        const auto s = cache.Resolve("a", 14, FontFace::Regular, compute); Check(s.width == 12 && s.height == 24);
    }
    void MetricsTests() {
        for (auto face : {FontFace::Regular, FontFace::Bold}) {
            const auto ligature = Detail::LayoutGlyphs("ffi", 32, {}, face);
            Check(ligature.glyphs.size() < 3 && !ligature.glyphs.empty());
            for (float size : {12.f, 32.f, 83.5f}) {
                const auto composed = Detail::RasterizeText("é", size, 4096, {.375f, .25f}, 4096, face);
                const auto decomposed = Detail::RasterizeText("e\u0301", size, 4096, {.375f, .25f}, 4096, face);
                Check(composed.texture->GetDesc().pixels == decomposed.texture->GetDesc().pixels);
                Check(std::abs(Detail::MeasureText("q\u0301", size, face).width - Detail::MeasureText("q", size, face).width) < .001f);
            }
        }
        for (const std::string text : {"ffi", "office", "e\u0301", "q\u0301", "й\u0301", "AVATAR"}) {
            const auto carets = Detail::TextCaretPositions(text, 32);
            Check(std::is_sorted(carets.begin(), carets.end()));
            Check(std::abs(carets.back() - Detail::MeasureText(text, 32).width) < .001f);
        }
        const auto carets = Detail::TextCaretPositions("ffi", 32);
        Check(carets.size() == 4 && carets[0] < carets[1] && carets[1] < carets[2] && carets[2] < carets[3]);
        const auto accentCarets = Detail::TextCaretPositions("e\u0301", 32);
        Check(accentCarets.size() == 3 && accentCarets[0] == accentCarets[1] && accentCarets[2] > 0);
        std::puts("HarfBuzz LTR: ligatures, canonical accents, mark advances, grapheme/ligature carets and shared metrics passed.");
        for (auto face : {FontFace::Regular, FontFace::Bold, FontFace::Icons})
        for (float size : {1.f, 14.f, 14.00001f, 36.75f, 256.f})
        for (const std::string text : {"", "AVATAR To WA", "Переработка пластика", "a\xcc\x81", "\xff\xc0\x80", "\xee\x88\x97"}) {
            const auto first = Detail::MeasureText(text, size, face);
            for (int i = 0; i < 8; ++i) { const auto again = Detail::MeasureText(text, size, face); Check(first.width == again.width && first.height == again.height); }
            Size other; std::thread worker([&] { other = Detail::MeasureText(text, size, face); }); worker.join();
            Check(first.width == other.width && first.height == other.height);
        }
        Reject([] { Detail::MeasureText("a", std::numeric_limits<float>::quiet_NaN()); });
        Reject([] { Detail::MeasureText("a", 0); });
        Reject([] { Detail::MeasureText("a", 14, static_cast<FontFace>(99)); });
        Reject([] { Detail::MeasureText(std::string(4097, 'a'), 14); });
    }
}
int main() {
    try {
        CacheTests(); MetricsTests();
        std::vector<std::string> labels;
        for (int i = 0; i < 100; ++i) labels.push_back("Переработка бумаги и пластика " + std::to_string(i));
        for (const auto& label : labels) Detail::MeasureText(label, 14);
        double sum = 0; const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 120; ++frame) for (const auto& label : labels) sum += Detail::MeasureText(label, 14).width;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("Text metrics: 12000 repeated labels %.3f ms, checksum %.3f; exact keys, eviction, long text, invalid input and thread isolation passed.\n", ms, sum);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
