# Unicode segmentation data

Pinned UCD version: **17.0.0**. License: [LICENSE](LICENSE).

Source: https://www.unicode.org/Public/17.0.0/ucd/

- `auxiliary/GraphemeBreakProperty.txt`
- `auxiliary/WordBreakProperty.txt`
- `DerivedCoreProperties.txt` (Indic_Conjunct_Break)
- `emoji/emoji-data.txt` (Extended_Pictographic)
- `auxiliary/GraphemeBreakTest.txt`
- `auxiliary/WordBreakTest.txt`

The original data and test files are retained in `17.0.0`. Generate the compact
C++ range tables with `BeastSDK/Tools/GenerateUnicode.ps1` from the repository
root. `-Download` explicitly refreshes the pinned source files from Unicode;
normal builds and tests are offline. Commit generated `Source/UI/Private/UnicodeData.h`
alongside inputs. The runtime contains only tables, not the original text files.

`BeastSDK/Tests/Run-UnicodeTests.ps1` checks all official grapheme and word test
cases, including controls not allowed by the single-line editing model. The
algorithm follows UAX #29 revision 47; word selection additionally suppresses
boundaries inside extended graphemes. It does not implement dictionaries,
locale tailoring, shaping, bidi, normalization or font fallback.
