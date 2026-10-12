# HarfBuzz 12.3.0

Unmodified `src` and `COPYING` (stored as `LICENSE`) from the official release:
https://github.com/harfbuzz/harfbuzz/releases/tag/12.3.0

Archive SHA-256:
`8660EBD3C27D9407FC8433B5D172BAFBA5F0317CB0BB4339F28E5370C93D42B7`.

Maintainer import: `BeastSDK/Tools/FetchHarfBuzz.ps1`. Normal builds are offline.
The SDK compiles the upstream amalgamation through `Source/UI/HarfBuzz.cpp`.
No FreeType, ICU or platform shaping dependency is needed: HarfBuzz's OpenType
font functions read the trusted embedded fonts, and stb rasterizes glyph IDs.

Integration uses script/font/directional runs with whole-grapheme fallback.
SheenBidi resolves each displayed line; paragraph context across soft wrapping
and complete bidi caret affinity remain follow-up work. See Source/UI/TextFeatures.md.
