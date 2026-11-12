#pragma once
// The copy of a source-chain DMA transfer that travels to the VU1 worker.
//
// PS2Memory::writeIORegister gathers the data of a chain into one contiguous block at the CHCR write; the VU1 worker
// (or the caller) frees it when the transfer has been processed. A world frame has several hundred such transfers of
// about ten kilobytes each, a few megabytes in all. Appending the pieces to a new vector grew it about twenty times
// per transfer (an allocation, a copy and a free each time). Now the chain is gathered in a scratch vector the caller
// keeps (it never shrinks), and copyOf() makes the block that travels in one allocation of the exact size. Blocks of
// kMinPooled bytes and more go round instead of being allocated: whoever finishes with one hands it back (release).
//
// PS2X_DMA_CHAIN_POOL=0 restores the first behaviour for comparisons: PS2Memory appends to a new vector and nothing is
// pooled. Any thread may call any function.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

namespace ps2x_chain_buffers {
inline constexpr size_t kMinPooled = 32u * 1024u;
inline constexpr size_t kMaxBuffers = 64;
inline constexpr size_t kMaxBytes = 192u * 1024u * 1024u;

struct Pool {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> free;
    size_t bytes = 0;
    bool enabled = true;
    // statistics (PS2X_PERF line): chains copied, their bytes, and how many went into a pooled block
    std::atomic<uint64_t> chains{0}, chainBytes{0}, reused{0};
    Pool() {
        const char* value = std::getenv("PS2X_DMA_CHAIN_POOL");
        enabled = !(value && value[0] == '0');
        free.reserve(kMaxBuffers); // release() never allocates
    }
};
inline Pool& pool() {
    static Pool* instance = new Pool; // never destroyed: worker threads may still hand blocks back at exit
    return *instance;
}
inline bool enabled() {
    return pool().enabled;
}

// The block that travels: `size` bytes of `data` in a vector of its own (a pooled one when the size calls for it and
// one that holds it has been handed back, the most recently returned first).
inline std::vector<uint8_t> copyOf(const uint8_t* data, size_t size) {
    Pool& p = pool();
    p.chains.fetch_add(1, std::memory_order_relaxed);
    p.chainBytes.fetch_add(size, std::memory_order_relaxed);
    if (size >= kMinPooled && p.enabled) {
        std::vector<uint8_t> block;
        {
            std::lock_guard<std::mutex> lock(p.mutex);
            for (size_t i = p.free.size(); i-- > 0;)
                if (p.free[i].capacity() >= size) {
                    block = std::move(p.free[i]);
                    p.free.erase(p.free.begin() + static_cast<std::ptrdiff_t>(i));
                    p.bytes -= block.capacity();
                    break;
                }
        }
        if (block.capacity() >= size) {
            block.assign(data, data + size);
            p.reused.fetch_add(1, std::memory_order_relaxed);
            return block;
        }
    }
    return std::vector<uint8_t>(data, data + size);
}

// Hands a finished block back. Blocks below kMinPooled, and any block while the pool is full, stay with the caller
// (and are freed with it); a block that was taken leaves the vector empty.
inline void release(std::vector<uint8_t>& block) noexcept {
    const size_t capacity = block.capacity();
    if (capacity < kMinPooled)
        return;
    Pool& p = pool();
    if (!p.enabled)
        return;
    block.clear();
    std::lock_guard<std::mutex> lock(p.mutex);
    if (p.free.size() >= kMaxBuffers || p.bytes + capacity > kMaxBytes)
        return;
    p.bytes += capacity;
    p.free.push_back(std::move(block));
    block = {};
}

// Counts a chain that was gathered without copyOf (PS2X_DMA_CHAIN_POOL=0).
inline void noteChain(size_t bytes) noexcept {
    Pool& p = pool();
    p.chains.fetch_add(1, std::memory_order_relaxed);
    p.chainBytes.fetch_add(bytes, std::memory_order_relaxed);
}
} // namespace ps2x_chain_buffers
