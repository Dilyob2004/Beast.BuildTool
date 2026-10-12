#include "RHI/Private/LayerTargetPool.h"
#include <cstdio>
#include <stdexcept>
#include <source_location>
#include <set>
#include <string>
using namespace Beast;
using namespace Beast::Detail;
namespace {
    void Check(bool value, std::source_location at = std::source_location::current())
    { if (!value) throw std::runtime_error("Pool assertion line " + std::to_string(at.line())); }
    struct Target { std::uint32_t width, height; std::uint64_t bytes; int id; };
}
int main() {
    try {
        LayerTargetPool<Target> pool; RHILayerStats stats; std::set<int> destroyed;
        auto destroy = [&](Target& t) { Check(destroyed.insert(t.id).second); };
        Target result{};
        pool.Put({10, 20, 800, 1}, 2400, stats, destroy);
        pool.Put({20, 10, 800, 2}, 2400, stats, destroy);
        Check(!pool.Take(10, 10, result)); Check(pool.Take(10, 20, result) && result.id == 1);
        pool.Put(result, 2400, stats, destroy); // Most recently recycled now.
        pool.Put({30, 20, 2400, 3}, 2400, stats, destroy); // Evict 2 then 1.
        pool.ReadStats(stats); Check(destroyed == std::set<int>({1, 2}) && stats.pooledBytes == 2400 && stats.pooledTargets == 1);
        pool.Put({40, 20, 3200, 4}, 2400, stats, destroy); // Oversized target bypasses pool, doesn't evict useful entries.
        Check(destroyed.contains(4) && pool.Take(30, 20, result) && result.id == 3);
        pool.Put(result, 0, stats, destroy); Check(destroyed.contains(3));
        for (int i = 5; i < 25; ++i) pool.Put({1, 1, 4, i}, 100000, stats, destroy);
        pool.ReadStats(stats); Check(stats.pooledTargets == 16 && stats.pooledBytes == 64);
        pool.Clear(destroy); pool.ReadStats(stats); Check(stats.pooledTargets == 0 && stats.pooledBytes == 0 && destroyed.size() == 24);
        pool.Put({1, 1, LayerTargetPool<Target>::MaxBytes + 1, 25}, UINT64_MAX, stats, destroy);
        Check(destroyed.contains(25));
        pool.Put({2, 2, 16, 26}, 2400, stats, destroy); pool.Abandon(); // A lost context must not delete names in its replacement.
        Check(!destroyed.contains(26)); pool.ReadStats(stats); Check(stats.pooledBytes == 0);
        std::puts("Layer pool: exact sizes, byte/count caps, oldest eviction, oversized/disabled bypass, clear and abandon passed.");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
