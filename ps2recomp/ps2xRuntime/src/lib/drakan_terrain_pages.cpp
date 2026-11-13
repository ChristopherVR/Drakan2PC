#include "drakan_terrain_pages.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/TerrainPagePool.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace drakan_terrain_pages {
namespace {

namespace pool = world::terrain_pages;

constexpr uint32_t kMalloc = 0x4ac628u;  // a0 = size
constexpr uint32_t kRealloc = 0x4acef0u; // a0 = pointer, a1 = size
constexpr uint32_t kFree = 0x4acc40u;    // a0 = pointer

static_assert(PS2_RAM_SIZE > PS2_RETAIL_RAM_SIZE, "the pool lives above the retail RAM");

std::atomic<bool> g_installed{false};
PS2Runtime::RecompiledFunction g_origMalloc = nullptr, g_origRealloc = nullptr, g_origFree = nullptr;
std::mutex g_mutex;
pool::Pool g_pool;
uint64_t g_served = 0, g_fallbacks = 0;
uint32_t g_reportedPeak = 0;
bool g_log = false;

void giveBack(R5900Context* c, uint32_t value) {
    setReturnU32(c, value);
    c->pc = GPR_U32(c, 31);
}

// Heap-style size word in front of the block, so a reader of the chunk header sees a used chunk of the right size.
void stampHeader(uint8_t* ram, uint32_t address) {
    const uint32_t word = (g_pool.sizeOf(address) + pool::kHeader) | 1u;
    std::memcpy(ram + address - 4u, &word, 4);
}

void noteLocked(const char* what) {
    constexpr uint32_t kStep = 4u << 20;
    if (g_pool.peak() >= g_reportedPeak + kStep || g_log) {
        g_reportedPeak = g_pool.peak() / kStep * kStep;
        std::fprintf(stderr, "[terrain-pages] %s: in use %.1f MB in %zu blocks, peak %.1f MB of %.1f MB\n", what,
                     g_pool.used() / 1048576.0, g_pool.blocks(), g_pool.peak() / 1048576.0,
                     (g_pool.end() - g_pool.base()) / 1048576.0);
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
    if (c->pc == kMalloc && pool::fromBuilder(GPR_U32(c, 31))) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (const uint32_t address = allocateLocked(ram, GPR_U32(c, 4))) {
            giveBack(c, address);
            return;
        }
    }
    g_origMalloc(ram, c, rt);
}

void hookRealloc(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kRealloc) {
        const uint32_t old = GPR_U32(c, 4) & 0x1FFFFFFFu, bytes = GPR_U32(c, 5);
        std::unique_lock<std::mutex> lock(g_mutex);
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
        if (old == 0 && pool::fromBuilder(GPR_U32(c, 31))) {
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
        const uint32_t address = GPR_U32(c, 4) & 0x1FFFFFFFu;
        std::unique_lock<std::mutex> lock(g_mutex);
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

} // namespace

bool installed() {
    return g_installed.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kMalloc) || !rt.hasFunction(kRealloc) ||
        !rt.hasFunction(kFree))
        return;
    if (!drakan_cfg::getBool("general", "terrain_page_pool", "DRAKAN_TERRAIN_PAGE_POOL", true)) {
        std::fprintf(stderr, "[terrain-pages] off: terrain draw pages come from the game heap\n");
        return;
    }
    g_log = std::getenv("DRAKAN_TERRAIN_PAGES_LOG") != nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pool.reset(PS2_RETAIL_RAM_SIZE, PS2_RAM_SIZE);
    }
    g_origMalloc = rt.lookupFunction(kMalloc);
    g_origRealloc = rt.lookupFunction(kRealloc);
    g_origFree = rt.lookupFunction(kFree);
    rt.replaceFunction(kMalloc, &hookMalloc);
    rt.replaceFunction(kRealloc, &hookRealloc);
    rt.replaceFunction(kFree, &hookFree);
    g_installed = true;
    std::fprintf(stderr, "[terrain-pages] installed: terrain draw pages in guest RAM 0x%x..0x%x\n", PS2_RETAIL_RAM_SIZE,
                 PS2_RAM_SIZE);
}

} // namespace drakan_terrain_pages
