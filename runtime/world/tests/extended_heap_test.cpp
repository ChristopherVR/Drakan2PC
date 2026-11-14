// Extended guest heap (world/ExtendedHeap.h): allocator, growth / give-back of the committed top, routing rules.
// Synthetic addresses only.
#include <cstdio>
#include <random>
#include <vector>

#include "world/ExtendedHeap.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::extended_heap;

constexpr uint32_t kBase = 0x02000000u, kLimit = 0x10000000u, kStep = 1u << 20;

} // namespace

int main() {
    // routing: terrain builder return addresses; the start-up heap probe
    CHECK(fromBuilder(0x157cd8u) && fromBuilder(0x154060u) && fromBuilder(0x157de4u));
    CHECK(!fromBuilder(0x15405cu) && !fromBuilder(0x157de8u) && !fromBuilder(0x15ff40u) && !fromBuilder(0));
    CHECK(fromProbe(0x4ac258u) && fromProbe(0x4ac268u) && !fromProbe(0x4ac2c0u) && !fromProbe(0x4ac1ecu));
    CHECK(fromProbe(kProbeFreeReturn) && !fromProbe(0x4acf2cu));

    // sbrk: retail below the heap end, continue above 32 MB when it is full, grow / shrink there, limits
    constexpr uint32_t kLow = 0x01f00000u, kExt = 0x02000000u, kExtEnd = 0x10000000u;
    SbrkAnswer s = decideSbrk(0x005c0000u, 0x1000, kLow, kExt, kExtEnd, false);
    CHECK(s.result == 0x005c0000u && s.brk == 0x005c1000u && !s.continued);
    s = decideSbrk(0x01ef0000u, 0x10000, kLow, kExt, kExtEnd, false); // exactly to the end: retail
    CHECK(s.result == 0x01ef0000u && s.brk == kLow && !s.continued);
    s = decideSbrk(0x01ef0000u, 0x20000, kLow, kExt, kExtEnd, false); // past the end: continues at 32 MB
    CHECK(s.result == kExt && s.brk == kExt + 0x20000u && s.continued);
    s = decideSbrk(0x01ef0000u, 0x20000, kLow, kExt, kExtEnd, true); // the probe: retail answer (fail)
    CHECK(s.result == kSbrkFail && s.brk == 0x01ef0000u);
    s = decideSbrk(0x01ef0000u, 0x20000, kLow, kExt, kExt + 0x10000u, false); // no room up there either
    CHECK(s.result == kSbrkFail);
    s = decideSbrk(kExt + 0x20000u, 0x100000, kLow, kExt, kExtEnd, false);
    CHECK(s.result == kExt + 0x20000u && s.brk == kExt + 0x120000u && !s.continued);
    s = decideSbrk(kExt + 0x120000u, 0, kLow, kExt, kExtEnd, false); // sbrk(0) reports the break
    CHECK(s.result == kExt + 0x120000u && s.brk == kExt + 0x120000u);
    s = decideSbrk(kExt + 0x120000u, -0x100000, kLow, kExt, kExtEnd, false); // trim
    CHECK(s.result == kExt + 0x120000u && s.brk == kExt + 0x20000u);
    s = decideSbrk(kExt + 0x20000u, -0x30000, kLow, kExt, kExtEnd, false); // never below the extended base
    CHECK(s.result == kSbrkFail && s.brk == kExt + 0x20000u);
    s = decideSbrk(kExtEnd - 0x1000u, 0x2000, kLow, kExt, kExtEnd, false); // the limit
    CHECK(s.result == kSbrkFail);
    s = decideSbrk(0x01000000u, -0x1000, 0x00800000u, kExt, kExtEnd, false); // trim above a lowered end: allowed
    CHECK(s.result == 0x01000000u && s.brk == 0x00fff000u);
    s = decideSbrk(0x005c0000u, -0x00600000, kLow, kExt, kExtEnd, false); // below 0: fails
    CHECK(s.result == kSbrkFail);
    // size word: calloc clears exactly the usable bytes
    CHECK(usableFromHeader(headerFor(0x400)) == 0x400 && (headerFor(0x400) & 2u) == 0 && (headerFor(16) & 1u));
    CHECK(usableFromHeader(0x411u) == 0x40cu && usableFromHeader(0) == 0);

    // growth on demand: nothing committed until the first block, then whole steps
    std::vector<uint32_t> commits, decommits;
    Pool pool(
        kBase, kLimit, kStep,
        [&](uint32_t top) {
            commits.push_back(top);
            return true;
        },
        [&](uint32_t top) { decommits.push_back(top); });
    CHECK(pool.committed() == 0 && pool.used() == 0 && pool.blocks() == 0 && commits.empty());
    const uint32_t a = pool.allocate(0x400), b = pool.allocate(1), c = pool.allocate(0x1234);
    CHECK(a && b && c && commits.size() == 1 && commits[0] == kBase + kStep && pool.committed() == kStep);
    CHECK(a % kAlign == 0 && b % kAlign == 0 && c % kAlign == 0);
    CHECK(pool.contains(a) && pool.contains(c) && !pool.contains(0x01eff000u) && !pool.contains(kLimit));
    CHECK(pool.sizeOf(a) == 0x400 && pool.sizeOf(b) == kAlign && pool.sizeOf(c) == 0x1240);
    CHECK(b >= a + 0x400 + kHeader || a >= b + kAlign + kHeader);
    CHECK(pool.sizeOf(a + 16) == 0 && pool.sizeOf(0) == 0);

    // a block larger than a step grows the top far enough in one commit
    const uint32_t big = pool.allocate(5u << 20);
    CHECK(big && pool.sizeOf(big) == (5u << 20) && commits.size() == 2 && pool.top() >= big + (5u << 20));
    CHECK(pool.top() % kStep == 0 && pool.committed() <= 7u * kStep);

    // aligned blocks (memalign)
    const uint32_t al = pool.allocate(100, 256);
    CHECK(al && al % 256 == 0 && pool.sizeOf(al) >= 100 && pool.release(al));

    // frees: only live blocks, once
    CHECK(!pool.release(a + 16) && !pool.release(0x100000u) && !pool.release(0));
    CHECK(pool.release(b) && !pool.release(b));
    // freeing the big top block gives whole steps back, keeping one spare step
    CHECK(pool.release(big) && !decommits.empty() && pool.committed() <= 2u * kStep);
    CHECK(pool.release(a) && pool.release(c));
    CHECK(pool.used() == 0 && pool.blocks() == 0 && pool.committed() == kStep);
    CHECK(pool.peakCommitted() >= 6u * kStep && pool.grows() == 2 && pool.shrinks() >= 1);
    // everything coalesced back into one free block
    CHECK(pool.largestFree() == kStep);

    // the limit: blocks up to it, then 0 (the caller falls back to the game heap), pool still usable
    Pool small(kBase, kBase + 0x3000, 0x10000);
    const uint32_t s1 = small.allocate(0x3000 - kHeader);
    CHECK(s1 == kBase + kHeader && small.allocate(1) == 0 && small.top() == kBase + 0x3000);
    CHECK(small.allocate(0xffffffffu) == 0 && small.allocate(0) == 0);
    CHECK(small.release(s1) && small.allocate(0x800) != 0);

    // the host refusing a commit: 0, nothing changes
    bool allow = false;
    Pool refused(kBase, kLimit, kStep, [&](uint32_t) { return allow; });
    CHECK(refused.allocate(64) == 0 && refused.committed() == 0);
    allow = true;
    CHECK(refused.allocate(64) != 0 && refused.committed() == kStep);

    // random churn like layers coming into and out of view: no overlap, full recovery, committed top follows use
    Pool churn(kBase, kLimit, 0x10000);
    std::mt19937 rng(7);
    std::vector<std::pair<uint32_t, uint32_t>> live;
    for (int i = 0; i < 40000; ++i) {
        if (live.empty() || rng() % 3 != 0) {
            const uint32_t n = 0x40u + rng() % 0x1400u;
            const uint32_t p = churn.allocate(n, rng() % 8 == 0 ? 64u : kAlign);
            CHECK(p != 0 && p + n <= churn.top());
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
    CHECK(churn.committed() <= churn.peak() + churn.peak() / 2 + 0x20000u); // the top follows use
    for (const auto& [p, n] : live)
        CHECK(churn.release(p));
    CHECK(churn.used() == 0 && churn.committed() == 0x10000u && churn.largestFree() == 0x10000u);

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("extended_heap_test: all checks passed\n");
    return g_failed ? 1 : 0;
}
