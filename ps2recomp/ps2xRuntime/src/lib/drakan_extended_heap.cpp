#include "drakan_extended_heap.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/ExtendedHeap.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace drakan_extended_heap {
namespace {

namespace rules = world::extended_heap;

constexpr uint32_t kMalloc = 0x4ac628u;  // a0 = size
constexpr uint32_t kRealloc = 0x4acef0u; // a0 = pointer, a1 = size
constexpr uint32_t kFree = 0x4acc40u;    // a0 = pointer
constexpr uint32_t kSbrk = 0x4bc7b8u;    // a0 = signed increment; v0 = old break or -1
constexpr uint32_t kBreak = 0x517084u;   // sbrk's current break
constexpr uint32_t kExtBase = PS2_RETAIL_RAM_SIZE;
constexpr uint32_t kMaxExtendedMb = (PS2_RAM_SIZE - PS2_RETAIL_RAM_SIZE) >> 20;
constexpr uint32_t kReportStep = 1u << 20; // log each new MB of peak

static_assert(PS2_RAM_SIZE > PS2_RETAIL_RAM_SIZE, "the extended heap lives above the retail RAM");

std::atomic<bool> g_installed{false};
bool g_log = false;
uint32_t g_extLimit = kExtBase; // end of the usable extended range

// ---- heap extension (sbrk)
PS2Runtime::RecompiledFunction g_origSbrk = nullptr;
std::mutex g_heapMutex;
uint32_t g_lowEnd = 0; // retail heap end override (DRAKAN_HEAP_EXTEND_AT, tests only); 0 = EndOfHeap
uint32_t g_extPeak = 0, g_extReported = 0, g_retailPeak = 0, g_retailReported = 0;
uint64_t g_extFailures = 0, g_trims = 0;
std::atomic<int> g_probeDepth{0}; // 1 while the start-up heap probe runs

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
void wr32(uint8_t* ram, uint32_t a, uint32_t v) {
    std::memcpy(ram + a, &v, 4);
}

void giveBack(R5900Context* c, uint32_t value) {
    setReturnU32(c, value);
    c->pc = GPR_U32(c, 31);
}

GuestRamReservation& hostRam(PS2Runtime* rt) {
    return rt->memory().guestRam();
}

void reportHeapLocked(PS2Runtime* rt, const char* what, uint32_t brk) {
    const uint32_t above = brk > kExtBase ? brk - kExtBase : 0u;
    if (above > g_extPeak)
        g_extPeak = above;
    if (g_extPeak >= g_extReported + kReportStep || g_log || std::strcmp(what, "grew") != 0) {
        if (std::strcmp(what, "grew") == 0)
            g_extReported = g_extPeak / kReportStep * kReportStep;
        std::fprintf(stderr,
                     "[extended-heap] %s: game heap %.1f MB above the retail RAM (peak %.1f MB of %u MB), host "
                     "committed %.1f MB above 32 MB (+%llu stray 64 KB pages); retail break peak 0x%x\n",
                     what, above / 1048576.0, g_extPeak / 1048576.0, (g_extLimit - kExtBase) >> 20,
                     hostRam(rt).committedEnd() > kExtBase ? (hostRam(rt).committedEnd() - kExtBase) / 1048576.0 : 0.0,
                     static_cast<unsigned long long>(GuestRamReservation::strayCommits()), g_retailPeak);
    }
}

// The retail part of the heap: its break peak, logged at each further MB (the heap high-water below 32 MB).
void noteRetailLocked(uint32_t brk) {
    if (brk >= kExtBase || brk <= g_retailPeak)
        return;
    g_retailPeak = brk;
    if (brk >= g_retailReported + kReportStep) {
        g_retailReported = brk / kReportStep * kReportStep;
        std::fprintf(stderr, "[extended-heap] retail heap break 0x%x (%.1f MB)\n", brk, brk / 1048576.0);
    }
}

void hookSbrk(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != kSbrk) { // resumed mid-function (this hook never yields; kept for safety)
        g_origSbrk(ram, c, rt);
        return;
    }
    const int32_t increment = static_cast<int32_t>(GPR_U32(c, 4));
    std::lock_guard<std::mutex> lock(g_heapMutex);
    const uint32_t old = rd32(ram, kBreak);
    const uint32_t lowEnd = g_lowEnd ? g_lowEnd : rt->guestHeapLimit();
    rules::SbrkAnswer a = rules::decideSbrk(old, increment, lowEnd, kExtBase, g_extLimit, g_probeDepth.load() != 0);
    if (a.result != rules::kSbrkFail && a.brk > old && a.brk > kExtBase && !hostRam(rt).commitTo(a.brk))
        a = rules::SbrkAnswer{}; // the host is out of memory: the allocator sees a full heap as before
    if (a.result != rules::kSbrkFail) {
        wr32(ram, kBreak, a.brk);
        noteRetailLocked(a.brk);
        if (a.continued)
            reportHeapLocked(rt, "continued", a.brk);
        else if (a.brk < old && old > kExtBase) {
            hostRam(rt).decommitFrom(std::max(a.brk, kExtBase)); // the allocator trimmed its top
            if (++g_trims <= 16 || g_log)
                reportHeapLocked(rt, "trimmed", a.brk);
        } else if (a.brk > old && a.brk > kExtBase)
            reportHeapLocked(rt, "grew", a.brk);
    } else if (increment > 0 && !g_probeDepth.load() && g_extFailures++ < 8)
        std::fprintf(stderr, "[extended-heap] sbrk of %d bytes refused at break 0x%x (limit 0x%x)\n", increment, old,
                     g_extLimit);
    giveBack(c, a.result);
}

// The start-up probe must see the retail heap: its 32 MB mallocs are expected to fail and are never freed. Its mallocs
// (return addresses inside it) close the extension until its closing free (return address kProbeFreeReturn). Only call
// entries are seen (a call the scheduler resumes enters the original function), which is all this needs.
PS2Runtime::RecompiledFunction g_heapOrigMalloc = nullptr, g_heapOrigFree = nullptr;

void hookHeapMalloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kMalloc) {
        const uint32_t ra = GPR_U32(c, 31);
        if (rules::fromProbe(ra) && g_probeDepth.exchange(1) == 0 && g_log)
            std::fprintf(stderr, "[extended-heap] start-up heap probe: retail heap only\n");
    }
    g_heapOrigMalloc(ram, c, rt);
}

void hookHeapFree(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const PS2Runtime::RecompiledFunction original = g_heapOrigFree;
    if (c->pc == kFree && GPR_U32(c, 31) == rules::kProbeFreeReturn && g_probeDepth.exchange(0)) {
        // The probe runs once, at start: malloc and free get their original entries back (no wrapper from here on).
        rt->replaceFunction(kMalloc, g_heapOrigMalloc);
        rt->replaceFunction(kFree, g_heapOrigFree);
        std::fprintf(stderr, "[extended-heap] start-up heap probe done (retail answer); allocator hooks removed\n");
    }
    original(ram, c, rt);
}

// ---- terrain page pool (heap_extend off)
PS2Runtime::RecompiledFunction g_origMalloc = nullptr, g_origRealloc = nullptr, g_origFree = nullptr;
std::mutex g_poolMutex;
rules::Pool g_pool;
uint64_t g_served = 0, g_fallbacks = 0;
uint32_t g_reportedPeak = 0;

// Heap-style size word in front of the block, so a reader of the chunk header sees a used chunk of the right size.
void stampHeader(uint8_t* ram, uint32_t address) {
    const uint32_t word = rules::headerFor(g_pool.sizeOf(address));
    std::memcpy(ram + address - 4u, &word, 4);
}

void noteLocked(const char* what) {
    if (g_pool.peak() >= g_reportedPeak + 4 * kReportStep || g_log) {
        g_reportedPeak = g_pool.peak() / (4 * kReportStep) * (4 * kReportStep);
        std::fprintf(stderr,
                     "[terrain-pages] %s: in use %.1f MB in %zu blocks, peak %.1f MB, host committed %.1f MB "
                     "(peak %.1f MB) of %.1f MB\n",
                     what, g_pool.used() / 1048576.0, g_pool.blocks(), g_pool.peak() / 1048576.0,
                     g_pool.committed() / 1048576.0, g_pool.peakCommitted() / 1048576.0,
                     (g_pool.limit() - g_pool.base()) / 1048576.0);
    }
}

uint32_t allocateLocked(uint8_t* ram, uint32_t bytes) {
    const uint32_t address = g_pool.allocate(std::max(bytes, 1u));
    if (address) {
        stampHeader(ram, address);
        ++g_served;
        noteLocked("grew");
    } else if (g_fallbacks++ < 8) {
        std::fprintf(stderr,
                     "[terrain-pages] pool full (%.1f MB in use, largest free %u bytes): %u bytes from the "
                     "game heap\n",
                     g_pool.used() / 1048576.0, g_pool.largestFree(), bytes);
    }
    return address;
}

void hookMalloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kMalloc && rules::fromBuilder(GPR_U32(c, 31))) {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        if (const uint32_t address = allocateLocked(ram, GPR_U32(c, 4))) {
            giveBack(c, address);
            return;
        }
    }
    g_origMalloc(ram, c, rt);
}

void hookRealloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kRealloc) {
        const uint32_t old = GPR_U32(c, 4) & PS2_RAM_MASK, bytes = GPR_U32(c, 5);
        std::unique_lock<std::mutex> lock(g_poolMutex);
        if (g_pool.contains(old)) {
            // newlib keeps the block when it still fits; a new block otherwise (0 and the old one kept when full)
            const uint32_t have = g_pool.sizeOf(old);
            if (have && bytes <= have) {
                giveBack(c, old);
                return;
            }
            const uint32_t address = allocateLocked(ram, bytes);
            if (address) {
                std::memmove(ram + address, ram + old, std::min(have, bytes));
                g_pool.release(old);
            } else
                std::fprintf(stderr, "[terrain-pages] realloc of a pool block to %u bytes failed\n", bytes);
            giveBack(c, address);
            return;
        }
        if (old == 0 && rules::fromBuilder(GPR_U32(c, 31))) {
            if (const uint32_t address = allocateLocked(ram, bytes)) {
                giveBack(c, address);
                return;
            }
        }
    }
    g_origRealloc(ram, c, rt);
}

void hookFree(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kFree) {
        const uint32_t address = GPR_U32(c, 4) & PS2_RAM_MASK;
        std::unique_lock<std::mutex> lock(g_poolMutex);
        if (g_pool.contains(address)) {
            if (!g_pool.release(address))
                std::fprintf(stderr, "[terrain-pages] free of 0x%x, not a live pool block (ignored)\n", address);
            else if (g_log)
                noteLocked("freed");
            c->pc = GPR_U32(c, 31);
            return;
        }
    }
    g_origFree(ram, c, rt);
}

void installPool(PS2Runtime& rt) {
    if (!rt.hasFunction(kMalloc) || !rt.hasFunction(kRealloc) || !rt.hasFunction(kFree))
        return;
    GuestRamReservation* ram = &rt.memory().guestRam();
    {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        g_pool.reset(
            kExtBase, g_extLimit, 2u << 20, [ram](uint32_t top) { return ram->commitTo(top); },
            [ram](uint32_t top) { ram->decommitFrom(top); });
    }
    g_origMalloc = rt.lookupFunction(kMalloc);
    g_origRealloc = rt.lookupFunction(kRealloc);
    g_origFree = rt.lookupFunction(kFree);
    rt.replaceFunction(kMalloc, &hookMalloc);
    rt.replaceFunction(kRealloc, &hookRealloc);
    rt.replaceFunction(kFree, &hookFree);
    std::fprintf(stderr, "[terrain-pages] installed: terrain draw pages in guest RAM 0x%x..0x%x, committed as used\n",
                 kExtBase, g_extLimit);
}

bool installHeap(PS2Runtime& rt) {
    if (!rt.hasFunction(kSbrk) || !rt.hasFunction(kMalloc) || !rt.hasFunction(kFree)) {
        std::fprintf(stderr, "[extended-heap] sbrk or the allocator is not a known function: heap stays retail\n");
        return false;
    }
    if (const char* at = std::getenv("DRAKAN_HEAP_EXTEND_AT")) // tests: move most of the heap above 32 MB
        g_lowEnd = static_cast<uint32_t>(std::strtoul(at, nullptr, 0)) & ~0xfffu;
    g_origSbrk = rt.lookupFunction(kSbrk);
    g_heapOrigMalloc = rt.lookupFunction(kMalloc);
    g_heapOrigFree = rt.lookupFunction(kFree);
    rt.replaceFunction(kSbrk, &hookSbrk);
    rt.replaceFunction(kMalloc, &hookHeapMalloc);
    rt.replaceFunction(kFree, &hookHeapFree);
    if (g_lowEnd)
        std::fprintf(stderr, "[extended-heap] test mode: the retail heap ends at 0x%x instead of EndOfHeap\n",
                     g_lowEnd);
    std::fprintf(stderr,
                 "[extended-heap] installed: the game heap continues at 0x%x up to 0x%x (%u MB) when the retail "
                 "heap is full; host pages committed as used\n",
                 kExtBase, g_extLimit, (g_extLimit - kExtBase) >> 20);
    return true;
}

uint32_t extendedMb() {
    const int mb =
        drakan_cfg::getInt("general", "extended_heap_mb", "DRAKAN_EXTENDED_HEAP_MB", static_cast<int>(kMaxExtendedMb));
    return static_cast<uint32_t>(std::clamp(mb, 0, static_cast<int>(kMaxExtendedMb)));
}

} // namespace

bool installed() {
    return g_installed.load();
}

bool heapWillExtend() {
    if (!drakan_region::build().identityAddresses)
        return false;
    return extendedMb() > 0 && drakan_cfg::getBool("general", "heap_extend", "DRAKAN_HEAP_EXTEND", true);
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses)
        return;
    g_log = std::getenv("DRAKAN_EXTENDED_HEAP_LOG") != nullptr || std::getenv("DRAKAN_TERRAIN_PAGES_LOG") != nullptr;
    const uint32_t clamped = extendedMb();
    g_extLimit = kExtBase + (clamped << 20);
    if (!clamped) {
        std::fprintf(stderr, "[extended-heap] off (extended_heap_mb = 0): the game heap and its terrain pages stay in "
                             "the retail RAM\n");
        return;
    }
    const bool extend = drakan_cfg::getBool("general", "heap_extend", "DRAKAN_HEAP_EXTEND", true);
    const bool pages = drakan_cfg::getBool("general", "terrain_page_pool", "DRAKAN_TERRAIN_PAGE_POOL", true);
    if (extend && installHeap(rt)) {
        if (pages && g_log)
            std::fprintf(stderr, "[terrain-pages] not needed: the terrain draw pages come from the extended heap\n");
        g_installed = true;
        return;
    }
    if (pages) {
        installPool(rt);
        g_installed = true;
        return;
    }
    std::fprintf(stderr, "[extended-heap] off: the game heap and its terrain pages stay in the retail RAM\n");
}

} // namespace drakan_extended_heap
