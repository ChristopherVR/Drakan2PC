// runtime/ps2_chain_buffers.h: the block a DMA chain copy travels in. Small blocks are plain allocations, large ones
// are reused after release(), the contents are always exactly the bytes given, and the pool is bounded.
#include "runtime/ps2_chain_buffers.h"

#include <cstdio>
#include <cstring>
#include <thread>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
std::vector<uint8_t> pattern(size_t size, uint8_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<uint8_t>(seed + i * 7u);
    return bytes;
}
} // namespace

int main() {
    using namespace ps2x_chain_buffers;
    Pool& p = pool();
    p.enabled = true; // whatever PS2X_DMA_CHAIN_POOL says in the test environment

    // Small blocks: exact contents, never pooled.
    const auto small = pattern(1000, 3);
    auto a = copyOf(small.data(), small.size());
    check(a == small, "small block has the bytes given");
    release(a);
    check(a == small && p.free.empty() && p.reused.load() == 0, "a small block is not taken by release");

    // Large blocks: handed back, then reused for a block that fits, with the new contents only.
    const auto large = pattern(kMinPooled + 5000, 11);
    auto b = copyOf(large.data(), large.size());
    check(b == large, "large block has the bytes given");
    const uint8_t* storage = b.data();
    release(b);
    check(b.empty() && b.capacity() == 0 && p.free.size() == 1, "a large block is taken by release");
    const auto next = pattern(kMinPooled + 100, 99);
    auto c = copyOf(next.data(), next.size());
    check(c == next && c.data() == storage && p.reused.load() == 1 && p.free.empty(), "the released block is reused");
    // A block that does not fit stays in the pool and a new one is allocated.
    release(c);
    const auto bigger = pattern(4 * kMinPooled, 42);
    auto d = copyOf(bigger.data(), bigger.size());
    check(d == bigger && p.free.size() == 1 && p.reused.load() == 1, "a block that is too small is not reused");
    // The most recently returned block that fits is taken.
    release(d);
    auto e = copyOf(next.data(), next.size());
    check(e == next && e.capacity() >= 4 * kMinPooled && p.free.size() == 1, "the latest fitting block is taken first");
    release(e);

    // Bounded: more blocks than kMaxBuffers are left to their owners.
    std::vector<std::vector<uint8_t>> many;
    for (size_t i = 0; i < kMaxBuffers + 8; ++i)
        many.emplace_back(kMinPooled, static_cast<uint8_t>(i));
    for (auto& block : many)
        release(block);
    check(p.free.size() == kMaxBuffers, "the pool holds at most kMaxBuffers blocks");
    size_t kept = 0;
    for (const auto& block : many)
        kept += block.capacity() != 0;
    check(kept == 10, "blocks the pool refused stay with the caller"); // 2 were in the pool already

    // Off: nothing is pooled, contents unchanged.
    p.enabled = false;
    auto f = copyOf(large.data(), large.size());
    const uint64_t reusedBefore = p.reused.load();
    release(f);
    check(f == large && p.reused.load() == reusedBefore, "a disabled pool neither reuses nor takes blocks");
    p.enabled = true;

    // Two threads producing and returning blocks: contents stay intact (run under a race detector when available).
    const uint64_t chainsBefore = p.chains.load();
    auto worker = [&](uint8_t seed) {
        for (unsigned i = 0; i < 2000; ++i) {
            const auto source = pattern(kMinPooled + (i % 7) * 1000, static_cast<uint8_t>(seed + i));
            auto block = copyOf(source.data(), source.size());
            if (block != source)
                ++failures;
            release(block);
        }
    };
    std::thread first(worker, uint8_t(1)), second(worker, uint8_t(101));
    first.join();
    second.join();
    check(p.chains.load() == chainsBefore + 4000, "every copy is counted");
    check(p.free.size() <= kMaxBuffers && p.bytes <= kMaxBytes, "the pool stays within its limits");

    if (failures == 0)
        std::printf("chain_buffers: all checks passed (%llu blocks reused)\n",
                    static_cast<unsigned long long>(p.reused.load()));
    return failures ? 1 : 0;
}
