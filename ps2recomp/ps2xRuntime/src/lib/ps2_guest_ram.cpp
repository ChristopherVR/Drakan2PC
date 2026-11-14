#include "runtime/ps2_guest_ram.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#if !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#define PS2_GUEST_RAM_MMAP 1
#endif
#endif

namespace {

size_t roundUp(size_t value) {
    return (value + GuestRamReservation::kGranularity - 1u) & ~(GuestRamReservation::kGranularity - 1u);
}

std::atomic<uint64_t> g_strayCommits{0};

#if defined(_WIN32)
// The one reservation the fault handler serves (the process's guest RAM). Several may exist in tests; the handler
// checks each registered range.
constexpr int kMaxRanges = 4;
std::atomic<uintptr_t> g_rangeBase[kMaxRanges];
std::atomic<size_t> g_rangeSize[kMaxRanges];
std::atomic<bool> g_handlerInstalled{false};

LONG CALLBACK commitOnFault(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t address = static_cast<uintptr_t>(record->ExceptionInformation[1]);
    for (int i = 0; i < kMaxRanges; ++i) {
        const uintptr_t base = g_rangeBase[i].load(std::memory_order_acquire);
        const size_t size = g_rangeSize[i].load(std::memory_order_acquire);
        if (!base || address < base || address - base >= size)
            continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof mbi) || mbi.State != MEM_RESERVE)
            return EXCEPTION_CONTINUE_SEARCH; // committed already: a real protection fault (debug watch), not ours
        const uintptr_t page = address & ~static_cast<uintptr_t>(GuestRamReservation::kGranularity - 1u);
        if (!VirtualAlloc(reinterpret_cast<void*>(page), GuestRamReservation::kGranularity, MEM_COMMIT, PAGE_READWRITE))
            return EXCEPTION_CONTINUE_SEARCH;
        g_strayCommits.fetch_add(1, std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void registerRange(uint8_t* base, size_t size) {
    if (!g_handlerInstalled.exchange(true))
        AddVectoredExceptionHandler(1, commitOnFault);
    for (int i = 0; i < kMaxRanges; ++i) {
        uintptr_t expected = 0;
        if (g_rangeBase[i].load() == 0) {
            g_rangeSize[i].store(size, std::memory_order_release);
            if (g_rangeBase[i].compare_exchange_strong(expected, reinterpret_cast<uintptr_t>(base)))
                return;
        }
    }
}

void unregisterRange(uint8_t* base) {
    for (int i = 0; i < kMaxRanges; ++i)
        if (g_rangeBase[i].load() == reinterpret_cast<uintptr_t>(base)) {
            g_rangeBase[i].store(0, std::memory_order_release);
            g_rangeSize[i].store(0, std::memory_order_release);
        }
}
#endif

} // namespace

uint64_t GuestRamReservation::strayCommits() {
    return g_strayCommits.load(std::memory_order_relaxed);
}

bool GuestRamReservation::reserve(size_t total, size_t initialCommit) {
    release();
    total = roundUp(total);
#if defined(_WIN32)
    void* p = VirtualAlloc(nullptr, total, MEM_RESERVE, PAGE_NOACCESS);
    if (!p)
        return false;
    m_base = static_cast<uint8_t*>(p);
    m_size = total;
    m_explicit = true;
    m_ownsMapping = true;
    m_committed = 0;
    registerRange(m_base, m_size);
#elif defined(PS2_GUEST_RAM_MMAP)
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_NORESERVE)
    flags |= MAP_NORESERVE;
#endif
    void* p = mmap(nullptr, total, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED)
        return false;
    m_base = static_cast<uint8_t*>(p);
    m_size = total;
    m_explicit = false;
    m_ownsMapping = true;
    m_committed = 0;
#else
    void* p = std::calloc(total, 1);
    if (!p)
        return false;
    m_base = static_cast<uint8_t*>(p);
    m_size = total;
    m_explicit = false;
    m_ownsMapping = false;
    m_committed = 0;
#endif
    if (!commitTo(initialCommit)) {
        release();
        return false;
    }
    return true;
}

void GuestRamReservation::release() {
    if (!m_base)
        return;
#if defined(_WIN32)
    unregisterRange(m_base);
    VirtualFree(m_base, 0, MEM_RELEASE);
#elif defined(PS2_GUEST_RAM_MMAP)
    munmap(m_base, m_size);
#else
    std::free(m_base);
#endif
    m_base = nullptr;
    m_size = m_committed = 0;
    m_explicit = m_ownsMapping = false;
}

bool GuestRamReservation::commitTo(size_t end) {
    if (!m_base)
        return false;
    end = std::min(roundUp(end), m_size);
    if (end <= m_committed)
        return true;
#if defined(_WIN32)
    if (!VirtualAlloc(m_base + m_committed, end - m_committed, MEM_COMMIT, PAGE_READWRITE))
        return false;
#endif
    m_committed = end;
    return true;
}

void GuestRamReservation::decommitFrom(size_t end) {
    if (!m_base)
        return;
    end = std::min(roundUp(end), m_size);
    if (end >= m_committed)
        return;
#if defined(_WIN32)
    VirtualFree(m_base + end, m_committed - end, MEM_DECOMMIT);
#elif defined(PS2_GUEST_RAM_MMAP) && defined(MADV_DONTNEED)
    madvise(m_base + end, m_committed - end, MADV_DONTNEED); // private anonymous: reads 0 afterwards
#else
    std::memset(m_base + end, 0, m_committed - end);
#endif
    m_committed = end;
}
