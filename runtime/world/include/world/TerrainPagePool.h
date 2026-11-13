// Terrain draw page pool ([general] terrain_page_pool): the allocator and the routing rule, pure (no guest memory).
//
// The engine builds a draw block for every terrain layer that comes into view (FUN_00154060 and the functions it calls,
// 0x154060 to 0x157de8): the chunk records, the per-chunk group arrays and the draw pages (0x400 bytes or more each)
// that the VIF1 DMA later reads. All of it comes from the game's one heap, which ends at 0x1f00000 under the stack.
// The pages of the layers drawn in one frame must all fit at once (the engine only evicts layers not drawn this frame,
// see docs/journal/2026-11-07/ootf-quickload-lru.md). The disc levels list at most 524 chunks in one visible list;
// converted Order of the Flame levels list up to 5,656, so their pages run the heap out, malloc returns 0 and the
// builder writes its groups from address 0 ("[memset] bad size ... ra=0x157d7c", then a jump to 0 or a hang).
//
// The runtime maps guest RAM above the retail 32 MB (PS2_RAM_SIZE); the pool hands out that extra space for the
// allocations the builder makes. The engine still frees, re-allocates and budgets them as before, so what is drawn does
// not change; only where the bytes live does.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

namespace world::terrain_pages {

// Return addresses inside the terrain draw-block builder (FUN_00154060 .. FUN_00157a08, PAL).
inline constexpr uint32_t kBuilderBegin = 0x154060u;
inline constexpr uint32_t kBuilderEnd = 0x157de8u;

inline bool fromBuilder(uint32_t returnAddress) {
    return returnAddress >= kBuilderBegin && returnAddress < kBuilderEnd;
}

// Each block starts with this many bytes in front of the address handed out (16-byte aligned like a qword, room for a
// heap-style size word at address - 4).
inline constexpr uint32_t kHeader = 16u;
inline constexpr uint32_t kAlign = 16u;

// Best-fit allocator over [base, end) with coalescing of neighbouring free blocks. Not thread safe (the caller locks).
class Pool {
public:
    Pool() = default;
    Pool(uint32_t base, uint32_t end) { reset(base, end); }

    void reset(uint32_t base, uint32_t end) {
        base_ = (base + kAlign - 1u) & ~(kAlign - 1u);
        end_ = end & ~(kAlign - 1u);
        free_.clear();
        bySize_.clear();
        live_.clear();
        used_ = peak_ = 0;
        if (end_ > base_)
            insertFree(base_, end_ - base_);
    }

    bool contains(uint32_t address) const { return address >= base_ && address < end_; }
    uint32_t base() const { return base_; }
    uint32_t end() const { return end_; }
    uint32_t used() const { return used_; } // bytes of live blocks, headers included
    uint32_t peak() const { return peak_; }
    size_t blocks() const { return live_.size(); }
    uint32_t largestFree() const { return bySize_.empty() ? 0u : bySize_.rbegin()->first; }

    // Usable bytes of the live block at `address` (as handed out), 0 when there is none.
    uint32_t sizeOf(uint32_t address) const {
        const auto it = live_.find(address - kHeader);
        return it == live_.end() || address < kHeader ? 0u : it->second - kHeader;
    }

    // Address of `bytes` usable bytes (16-byte aligned), 0 when the pool has no room.
    uint32_t allocate(uint32_t bytes) {
        if (bytes > end_ - base_)
            return 0u;
        const uint64_t want64 = ((uint64_t(bytes) + kAlign - 1u) & ~uint64_t(kAlign - 1u)) + kHeader;
        const uint32_t want = static_cast<uint32_t>(want64);
        const auto fit = bySize_.lower_bound(want);
        if (fit == bySize_.end())
            return 0u;
        const uint32_t start = fit->second, size = fit->first;
        eraseFree(start, size);
        if (size - want >= kHeader + kAlign)
            insertFree(start + want, size - want);
        const uint32_t taken = size - want >= kHeader + kAlign ? want : size;
        live_.emplace(start, taken);
        used_ += taken;
        if (used_ > peak_)
            peak_ = used_;
        return start + kHeader;
    }

    // Frees the block handed out at `address`; false when it is not a live block of this pool (nothing changes).
    bool release(uint32_t address) {
        if (address < kHeader)
            return false;
        const auto it = live_.find(address - kHeader);
        if (it == live_.end())
            return false;
        uint32_t start = it->first, size = it->second;
        live_.erase(it);
        used_ -= size;
        // join the free neighbours
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
        return true;
    }

private:
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

    uint32_t base_ = 0, end_ = 0, used_ = 0, peak_ = 0;
    std::map<uint32_t, uint32_t> free_;        // start -> size
    std::multimap<uint32_t, uint32_t> bySize_; // size -> start
    std::map<uint32_t, uint32_t> live_;        // block start -> size (header included)
};

} // namespace world::terrain_pages
