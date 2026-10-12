#include "Core/Memory/Memory.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <thread>
#include <vector>

namespace
{
    void Check(bool condition)
    {
        if (!condition) { std::fputs("Beast memory test failed\n", stderr); std::abort(); }
    }

    void CheckBlock(std::size_t size, std::size_t alignment)
    {
        void* pointer = Beast::Memory::Allocate(size, alignment);
        Check(pointer != nullptr);
        Check(reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0);
        std::memset(pointer, 0xA5, size);
        const auto* bytes = static_cast<const unsigned char*>(pointer);
        for (std::size_t index = 0; index < size; ++index) Check(bytes[index] == 0xA5);
        Beast::Memory::Free(pointer);
    }
}

int main()
{
    using namespace Beast::Memory;
    Free(nullptr);
    Check(Allocate(0) == nullptr);
    Check(Allocate(16, 0) == nullptr);
    Check(Allocate(16, 3) == nullptr);
    Check(Allocate(16, 24) == nullptr);
    Check(Allocate(std::numeric_limits<std::size_t>::max(), 16) == nullptr);
    Check(Allocate(16, std::numeric_limits<std::size_t>::max()) == nullptr);
    const auto extremeAlignment = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
    Check(Allocate(extremeAlignment, extremeAlignment) == nullptr);
    void* defaultBlock = Allocate(37);
    Check(defaultBlock != nullptr);
    Check(reinterpret_cast<std::uintptr_t>(defaultBlock) % alignof(std::max_align_t) == 0);
    Free(defaultBlock);
    for (std::size_t alignment = 1; alignment <= 4096; alignment *= 2)
        for (std::size_t size : {1u, 3u, 17u, 64u, 257u, 4097u})
            CheckBlock(size, alignment);

    // Independent allocations may be used concurrently without shared wrapper state.
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.emplace_back([] { for (int i = 0; i < 500; ++i) CheckBlock(137, 64); });
    for (auto& worker : workers) worker.join();
    std::puts("Beast memory tests passed.");
}
