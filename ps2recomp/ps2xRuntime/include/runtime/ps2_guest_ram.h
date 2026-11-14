// Host memory behind guest RAM: one contiguous address range reserved for all of PS2_RAM_SIZE, with host pages
// committed only as far as the guest uses them. The retail 32 MB is committed at start; the extended range above it
// (the runtime's own pools, drakan_extended_heap.h) is committed by its owner as it grows and decommitted when it
// shrinks, so idle extended RAM costs no host memory.
//
// Platforms (no platform headers here):
// * Windows: VirtualAlloc reserve, commit on demand. A stray guest access to a reserved but uncommitted page (a wild
//   pointer, a debug scan of all of guest RAM) commits that page instead of crashing, as the fully mapped RAM did
//   before; such pages are counted (strayCommits).
// * POSIX: one anonymous MAP_NORESERVE mapping, read/write; pages cost memory only once touched. commit is bookkeeping.
// * Anything else (WebAssembly): one zeroed allocation of the whole size. commit is bookkeeping.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class GuestRamReservation {
public:
    GuestRamReservation() = default;
    ~GuestRamReservation() { release(); }
    GuestRamReservation(const GuestRamReservation&) = delete;
    GuestRamReservation& operator=(const GuestRamReservation&) = delete;

    // Reserves `total` bytes (zeroed when committed) and commits the first `initialCommit`. False on failure.
    bool reserve(size_t total, size_t initialCommit);
    void release();

    uint8_t* base() const { return m_base; }
    size_t size() const { return m_size; }
    // End of the contiguous committed prefix [0, committedEnd()).
    size_t committedEnd() const { return m_committed.load(std::memory_order_acquire); }
    // True when commit really maps host pages (Windows); false when the whole range is backed lazily by the OS.
    bool explicitCommit() const { return m_explicit; }

    // Makes [0, end) usable (rounded up to the commit granularity). False when the host refuses (out of memory).
    bool commitTo(size_t end);
    // Gives the host pages of [end, committedEnd()) back (rounded up to the granularity); their bytes read 0 again.
    void decommitFrom(size_t end);

    static constexpr size_t kGranularity = 64u * 1024u;
    static uint64_t strayCommits();

private:
    uint8_t* m_base = nullptr;
    size_t m_size = 0;
    std::atomic<size_t> m_committed{0}; // read by other threads (scans bound by the RAM in use)
    bool m_explicit = false;
    bool m_ownsMapping = false;
};
