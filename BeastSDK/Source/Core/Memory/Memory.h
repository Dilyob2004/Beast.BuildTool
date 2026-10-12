#pragma once
#include <cstddef>

namespace Beast::Memory
{
    // Allocates uninitialized raw storage; no constructors are called.
    // alignment must be a nonzero power of two. Small alignments are supported.
    // Returns nullptr for size == 0, invalid alignment, overflow or allocation failure.
    // Release only through Memory::Free, never delete or std::free.
    [[nodiscard]] void* Allocate(std::size_t size,
        std::size_t alignment = alignof(std::max_align_t)) noexcept;

    // Accepts nullptr or a live pointer returned by Allocate. No destructors are called.
    void Free(void* pointer) noexcept;
}
