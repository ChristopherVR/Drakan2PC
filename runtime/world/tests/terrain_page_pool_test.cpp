// Terrain draw page pool (world/TerrainPagePool.h): allocator and routing rule, synthetic addresses only.
#include <cstdio>
#include <random>
#include <vector>

#include "world/TerrainPagePool.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::terrain_pages;

constexpr uint32_t kBase = 0x02000000u, kEnd = 0x04000000u;

} // namespace

int main() {
    // routing: only return addresses inside the draw-block builder
    CHECK(fromBuilder(0x157cd8u) && fromBuilder(0x154060u) && fromBuilder(0x157de4u));
    CHECK(!fromBuilder(0x15405cu) && !fromBuilder(0x157de8u) && !fromBuilder(0x15ff40u) && !fromBuilder(0));

    Pool pool(kBase, kEnd);
    CHECK(pool.used() == 0 && pool.blocks() == 0 && pool.largestFree() == kEnd - kBase);

    // aligned, inside, sized, distinct
    const uint32_t a = pool.allocate(0x400), b = pool.allocate(1), c = pool.allocate(0x1234);
    CHECK(a && b && c);
    CHECK(a % kAlign == 0 && b % kAlign == 0 && c % kAlign == 0);
    CHECK(pool.contains(a) && pool.contains(c) && !pool.contains(0x01eff000u) && !pool.contains(kEnd));
    CHECK(pool.sizeOf(a) == 0x400 && pool.sizeOf(b) == kAlign && pool.sizeOf(c) == 0x1240);
    CHECK(b >= a + 0x400 + kHeader || a >= b + kAlign + kHeader);
    CHECK(pool.sizeOf(a + 16) == 0 && pool.sizeOf(0) == 0);

    // frees: only live blocks, once
    CHECK(!pool.release(a + 16) && !pool.release(0x100000u) && !pool.release(0));
    CHECK(pool.release(b) && !pool.release(b));
    CHECK(pool.release(a) && pool.release(c));
    CHECK(pool.used() == 0 && pool.blocks() == 0);
    // everything coalesced back into one block
    CHECK(pool.largestFree() == kEnd - kBase);
    CHECK(pool.peak() >= 0x400 + 0x1240 + 3 * kHeader);

    // exhaustion returns 0 and leaves the pool usable
    Pool small(kBase, kBase + 0x1000);
    const uint32_t big = small.allocate(0x1000 - kHeader);
    CHECK(big == kBase + kHeader && small.allocate(1) == 0);
    CHECK(small.allocate(0xffffffffu) == 0);
    CHECK(small.release(big) && small.allocate(0x800) != 0);

    // random churn like layers coming into and out of view: no overlap, full recovery
    Pool churn(kBase, kBase + 0x400000u);
    std::mt19937 rng(7);
    std::vector<std::pair<uint32_t, uint32_t>> live;
    for (int i = 0; i < 20000; ++i) {
        if (live.empty() || rng() % 3 != 0) {
            const uint32_t n = 0x40u + rng() % 0x1400u;
            const uint32_t p = churn.allocate(n);
            if (p) {
                for (const auto& [q, m] : live)
                    if (p < q + m && q < p + n) {
                        CHECK(!"overlap");
                        break;
                    }
                live.emplace_back(p, n);
            }
        } else {
            const size_t k = rng() % live.size();
            CHECK(churn.release(live[k].first));
            live[k] = live.back();
            live.pop_back();
        }
    }
    for (const auto& [p, n] : live)
        CHECK(churn.release(p));
    CHECK(churn.used() == 0 && churn.largestFree() == 0x400000u);

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("terrain_page_pool_test: all checks passed\n");
    return g_failed ? 1 : 0;
}
