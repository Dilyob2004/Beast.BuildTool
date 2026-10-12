// Vendored shaping engine, compiled by the same toolchain as the SDK.
// Only immutable, trusted embedded fonts are passed to it.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define HB_NO_SUBSET
#include "../../../ThirdParty/HarfBuzz/src/harfbuzz.cc"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
