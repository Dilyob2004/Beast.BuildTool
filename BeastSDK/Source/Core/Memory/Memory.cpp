#include "Core/Memory/Memory.h"
#include <limits>
#if defined(_WIN32)
#include <malloc.h>
#elif defined(__ANDROID__)
#include <cstdlib>
#else
#error Beast memory backend is not implemented for this platform.
#endif

namespace Beast::Memory
{
    void* Allocate(std::size_t size, std::size_t alignment) noexcept
    {
        // Validate before calling the CRT: invalid _aligned_malloc parameters
        // can invoke Windows' invalid-parameter handler rather than return null.
        if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return nullptr;
        if (alignment < sizeof(void*)) alignment = sizeof(void*);
        // Reserve headroom for allocator alignment padding and bookkeeping.
        if (size > std::numeric_limits<std::size_t>::max() - alignment - sizeof(void*))
            return nullptr;
#if defined(_WIN32)
        return _aligned_malloc(size, alignment);
#elif defined(__ANDROID__)
        void* pointer = nullptr;
        return ::posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
#endif
    }

    void Free(void* pointer) noexcept
    {
#if defined(_WIN32)
        _aligned_free(pointer);
#elif defined(__ANDROID__)
        std::free(pointer);
#endif
    }
}
