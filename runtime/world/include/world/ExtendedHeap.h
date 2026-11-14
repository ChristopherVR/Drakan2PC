// Extended guest heap ([general] heap_extend, terrain_page_pool, extended_heap_mb): the rules, pure (no guest memory,
// no host pages; the caller passes commit / decommit callbacks). Hooks: ps2xRuntime drakan_extended_heap.cpp.
//
// The game has one heap (newlib's dlmalloc: malloc 0x4ac628 / realloc 0x4acef0 / free 0x4acc40, PAL) that grows by
// sbrk (0x4bc7b8) up to the retail heap end (EndOfHeap, 0x1f00000, under the stack): about 25 MB. The runtime
// reserves guest RAM above the retail 32 MB (PS2_RAM_SIZE, ps2_memory.h):
// * heap_extend: sbrk past the retail end continues at the extended base (decideSbrk). dlmalloc takes a break that
//   is not its old end as a foreign sbrk: it fences the old top chunk, frees its rest and starts a new top there, so
//   the game's own allocator serves every malloc, realloc, memalign and free from both ranges;
// * terrain_page_pool (when heap_extend is off): every allocation of the terrain draw-block builder (FUN_00154060 and
//   the functions it calls, 0x154060 to 0x157de8) comes from a Pool instead. Converted Order of the Flame levels list
//   up to ten times the chunks of a disc level and their pages ran the heap out
//   (docs/journal/2026-11-14/ootf-terrain-heap.md).
// Both commit host pages only as far as they reach and give them back when they shrink.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace world::extended_heap {

// Return addresses inside the terrain draw-block builder (FUN_00154060 .. FUN_00157a08, PAL).
inline constexpr uint32_t kBuilderBegin = 0x154060u;
inline constexpr uint32_t kBuilderEnd = 0x157de8u;

inline bool fromBuilder(uint32_t returnAddress) {
    return returnAddress >= kBuilderBegin && returnAddress < kBuilderEnd;
}

// The start-up heap probe (static initialiser, PAL 0x4ac1f0..0x4ac2c0): it mallocs most of the heap, then three
// 32 MB blocks it expects to fail (each success adds 32 MB to a memory figure and is never freed), then frees the
// first block (its free returns to kProbeFreeReturn). While it runs, the heap must stay retail.
inline constexpr uint32_t kProbeBegin = 0x4ac1f0u;
inline constexpr uint32_t kProbeEnd = 0x4ac2c0u;
inline constexpr uint32_t kProbeFreeReturn = 0x4ac28cu;

inline bool fromProbe(uint32_t returnAddress) {
    return returnAddress >= kProbeBegin && returnAddress < kProbeEnd;
}

// One sbrk answer. `result` is the old break (the start of the new bytes) or kSbrkFail; `brk` the new break.
inline constexpr uint32_t kSbrkFail = 0xffffffffu;
struct SbrkAnswer {
    uint32_t result = kSbrkFail;
    uint32_t brk = 0;
    bool continued = false; // the heap moved to the extended range with this call
};

// sbrk(increment) at break `old`: below `lowEnd` (the retail heap end) as the game's own sbrk; a growth past it
// continues at `extBase` (unless `retailOnly`); in the extended range it grows up to `extLimit` and shrinks down to
// `extBase` at most. Anything else fails and leaves the break.
inline SbrkAnswer decideSbrk(uint32_t old, int32_t increment, uint32_t lowEnd, uint32_t extBase, uint32_t extLimit,
                             bool retailOnly) {
    SbrkAnswer a;
    a.brk = old;
    const int64_t want = int64_t(old) + increment;
    if (old < extBase) {
        if (want >= 0 && (want <= int64_t(lowEnd) || increment <= 0)) {
            a.result = old;
            a.brk = static_cast<uint32_t>(want);
        } else if (increment > 0 && !retailOnly && uint64_t(extBase) + uint64_t(increment) <= extLimit) {
            a.result = extBase;
            a.brk = extBase + static_cast<uint32_t>(increment);
            a.continued = true;
        }
    } else if (want >= int64_t(extBase) && want <= int64_t(extLimit)) {
        a.result = old;
        a.brk = static_cast<uint32_t>(want);
    }
    return a;
}

// The heap's size word at address - 4: chunk size with flag bits 0..1 (1 = previous in use, 2 = mapped); the usable
// bytes of a used chunk are its size - 4 (calloc clears exactly that many).
inline uint32_t usableFromHeader(uint32_t word) {
    const uint32_t size = word & ~3u;
    return size >= 4u ? size - 4u : 0u;
}
inline uint32_t headerFor(uint32_t usable) {
    return (usable + 4u) | 1u;
}

// Each block starts with at least this many bytes in front of the address handed out (room for the size word at
// address - 4), and addresses are 16-byte aligned like a qword.
inline constexpr uint32_t kHeader = 16u;
inline constexpr uint32_t kAlign = 16u;

// Best-fit allocator over [base, limit) with coalescing of neighbouring free blocks, growing its committed top in
// `step` units. Not thread safe (the caller locks).
class Pool {
public:
    // commit(newTop): make [base, newTop) usable, false when the host refuses. decommit(newTop): [newTop, old top) is
    // no longer used. Either may be empty (then growth always succeeds and nothing is given back).
    using Commit = std::function<bool(uint32_t)>;
    using Decommit = std::function<void(uint32_t)>;

    Pool() = default;
    Pool(uint32_t base, uint32_t limit, uint32_t step = 4u << 20, Commit commit = {}, Decommit decommit = {}) {
        reset(base, limit, step, std::move(commit), std::move(decommit));
    }

    void reset(uint32_t base, uint32_t limit, uint32_t step = 4u << 20, Commit commit = {}, Decommit decommit = {}) {
        base_ = (base + kAlign - 1u) & ~(kAlign - 1u);
        limit_ = limit & ~(kAlign - 1u);
        if (limit_ < base_)
            limit_ = base_;
        step_ = step < 0x10000u ? 0x10000u : step;
        top_ = base_;
        commit_ = std::move(commit);
        decommit_ = std::move(decommit);
        free_.clear();
        bySize_.clear();
        live_.clear();
        used_ = peak_ = peakTop_ = 0;
        grows_ = shrinks_ = 0;
    }

    bool contains(uint32_t address) const { return address >= base_ && address < limit_; }
    uint32_t base() const { return base_; }
    uint32_t limit() const { return limit_; }
    uint32_t top() const { return top_; }               // end of the committed part
    uint32_t committed() const { return top_ - base_; } // host bytes in use for the pool
    uint32_t peakCommitted() const { return peakTop_ > base_ ? peakTop_ - base_ : 0u; }
    uint32_t used() const { return used_; } // bytes of live blocks, headers included
    uint32_t peak() const { return peak_; }
    size_t blocks() const { return live_.size(); }
    uint64_t grows() const { return grows_; }
    uint64_t shrinks() const { return shrinks_; }
    uint32_t largestFree() const { return bySize_.empty() ? 0u : bySize_.rbegin()->first; }

    // Usable bytes of the live block handed out at `address`, 0 when there is none.
    uint32_t sizeOf(uint32_t address) const {
        const auto it = live_.find(address);
        return it == live_.end() ? 0u : it->second.start + it->second.size - address;
    }

    // Address of `bytes` usable bytes aligned to `align` (a power of two, at least 16), 0 when the pool cannot grow
    // far enough (limit reached or the host refused).
    uint32_t allocate(uint32_t bytes, uint32_t align = kAlign) {
        if (align < kAlign || (align & (align - 1u)))
            align = kAlign;
        const uint64_t want64 = ((uint64_t(bytes) + kAlign - 1u) & ~uint64_t(kAlign - 1u)) + kHeader + (align - kAlign);
        if (bytes == 0u || want64 > uint64_t(limit_ - base_))
            return 0u;
        const uint32_t want = static_cast<uint32_t>(want64);
        auto fit = bySize_.lower_bound(want);
        if (fit == bySize_.end()) {
            if (!grow(want))
                return 0u;
            fit = bySize_.lower_bound(want);
            if (fit == bySize_.end())
                return 0u;
        }
        const uint32_t start = fit->second, size = fit->first;
        eraseFree(start, size);
        const uint32_t handed = (start + kHeader + align - 1u) & ~(align - 1u);
        const uint32_t needed = handed - start + ((bytes + kAlign - 1u) & ~(kAlign - 1u));
        const uint32_t taken = size - needed >= kHeader + kAlign ? needed : size;
        if (taken < size)
            insertFree(start + taken, size - taken);
        live_.emplace(handed, Block{start, taken});
        used_ += taken;
        if (used_ > peak_)
            peak_ = used_;
        return handed;
    }

    // Frees the block handed out at `address`; false when it is not a live block of this pool (nothing changes).
    bool release(uint32_t address) {
        const auto it = live_.find(address);
        if (it == live_.end())
            return false;
        uint32_t start = it->second.start, size = it->second.size;
        live_.erase(it);
        used_ -= size;
        auto next = free_.lower_bound(start);
        if (next != free_.end() && next->first == start + size) {
            const uint32_t s = next->first, n = next->second;
            eraseFree(s, n);
            size += n;
        }
        auto prev = free_.lower_bound(start);
        if (prev != free_.begin()) {
            --prev;
            if (prev->first + prev->second == start) {
                const uint32_t s = prev->first, n = prev->second;
                eraseFree(s, n);
                start = s;
                size += n;
            }
        }
        insertFree(start, size);
        shrink();
        return true;
    }

private:
    struct Block {
        uint32_t start, size; // header included
    };

    // Extends the committed top so a free block of `want` bytes exists at the top.
    bool grow(uint32_t want) {
        uint32_t topFree = 0, topStart = top_;
        if (!free_.empty()) {
            const auto last = std::prev(free_.end());
            if (last->first + last->second == top_) {
                topFree = last->second;
                topStart = last->first;
            }
        }
        const uint64_t need = uint64_t(topStart) + want;
        uint64_t newTop = uint64_t(top_) + step_;
        if (newTop < need)
            newTop = need;
        newTop = (newTop - base_ + step_ - 1u) / step_ * step_ + base_;
        if (newTop > limit_)
            newTop = limit_;
        if (newTop < need)
            return false;
        if (commit_ && !commit_(static_cast<uint32_t>(newTop)))
            return false;
        if (topFree)
            eraseFree(topStart, topFree);
        insertFree(topStart, static_cast<uint32_t>(newTop) - topStart);
        top_ = static_cast<uint32_t>(newTop);
        if (top_ > peakTop_)
            peakTop_ = top_;
        ++grows_;
        return true;
    }

    // Gives back whole steps of a free top beyond one spare step (hysteresis against grow / shrink churn).
    void shrink() {
        if (free_.empty())
            return;
        const auto last = std::prev(free_.end());
        if (last->first + last->second != top_)
            return;
        const uint32_t start = last->first;
        const uint64_t keep = (uint64_t(start) - base_ + step_ - 1u) / step_ * step_ + base_ + step_;
        if (keep + step_ > top_)
            return; // less than two spare steps
        const uint32_t newTop = static_cast<uint32_t>(keep);
        eraseFree(start, top_ - start);
        insertFree(start, newTop - start);
        if (decommit_)
            decommit_(newTop);
        top_ = newTop;
        ++shrinks_;
    }

    void insertFree(uint32_t start, uint32_t size) {
        free_.emplace(start, size);
        bySize_.emplace(size, start);
    }
    void eraseFree(uint32_t start, uint32_t size) {
        free_.erase(start);
        for (auto [lo, hi] = bySize_.equal_range(size); lo != hi; ++lo)
            if (lo->second == start) {
                bySize_.erase(lo);
                break;
            }
    }

    uint32_t base_ = 0, limit_ = 0, step_ = 4u << 20, top_ = 0, used_ = 0, peak_ = 0, peakTop_ = 0;
    uint64_t grows_ = 0, shrinks_ = 0;
    Commit commit_;
    Decommit decommit_;
    std::map<uint32_t, uint32_t> free_;        // start -> size
    std::multimap<uint32_t, uint32_t> bySize_; // size -> start
    std::map<uint32_t, Block> live_;           // handed-out address -> block
};

} // namespace world::extended_heap
