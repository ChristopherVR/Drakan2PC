// Drakan: Order of the Flame Magic Crystals (drakan_crystals.h). PAL addresses.
#include "drakan_crystals.h"
#include "drakan_crystals_policy.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

namespace drakan_crystals {
namespace {

// The mana power-up's message handler (class 0x100ab, event 2 of its handler 392978): (item instance, message); for the
// use message it adds the class's Mana Boost to the player's mana. The item's actor handle is item + 0xc.
constexpr uint32_t kUseHandler = 0x392aa0u;
// Public spawn (docs/journal/2026-10-31/actor-spawn.md): (engine, owner, transform*, extra*, extraLen, resIndex).
constexpr uint32_t kFactory = 0x181ad8u, kContextPtr = 0x5565d4u;
constexpr uint32_t kRynnClass = 0x10009u;
constexpr uint32_t kFrame = 0x60u; // the trampoline's stack frame (transform at +0x10)

std::mutex g_mutex;
std::map<uint32_t, Crystal> g_table; // crystal resource id -> effect
bool g_enabled = false, g_log = true, g_trace = false;
PS2Runtime::RecompiledFunction g_origUse = nullptr;
uint32_t g_slot1 = 0, g_slot2 = 0;

std::deque<uint32_t> g_pending; // effects (resource ids) to spawn at Rynn, one per use, guarded by g_mutex
std::atomic<int> g_logBudget{64};

struct Flight {
    float xform[12] = {};
    uint32_t idx = 0, owner = 0, savedSp = 0, savedRa = 0;
} g_flight;
std::atomic<uint32_t> g_result{0}, g_serial{0};
bool g_inFlight = false;
uint32_t g_inflightSerial = 0, g_inflightEffect = 0;
unsigned g_ticks = 0, g_postTick = 0;

void logf(const char* fmt, ...) {
    if (!g_log || g_logBudget.fetch_sub(1) <= 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[crystals] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

bool ramPtr(uint32_t p, uint32_t n = 4) {
    const uint32_t a = p & 0x1fffffffu;
    return a >= 0x100000u && a + n <= PS2_RAM_SIZE;
}
uint32_t rd32(const uint8_t* ram, uint32_t a) {
    if (!ramPtr(a))
        return 0;
    uint32_t v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 4);
    return v;
}
uint16_t rd16(const uint8_t* ram, uint32_t a) {
    if (!ramPtr(a, 2))
        return 0;
    uint16_t v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 2);
    return v;
}

// The resource record of an actor handle (world-model.md: handle +0x10 = u16 record index into ctx+0x320).
uint32_t recordOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t ctx = rd32(ram, kContextPtr);
    const uint32_t res = ramPtr(ctx) ? rd32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(handle, 0x14) || !ramPtr(res, 0x14))
        return 0;
    const uint16_t idx = rd16(ram, handle + 0x10u);
    if (idx == 0xffffu || idx > rd32(ram, res + 0x10u))
        return 0;
    const uint32_t rec = rd32(ram, res + 8u) + idx * 0x24u;
    return ramPtr(rec, 0x24) ? rec : 0;
}

uint32_t findResourceIndex(const uint8_t* ram, uint32_t resId) {
    const uint32_t ctx = rd32(ram, kContextPtr);
    const uint32_t res = ramPtr(ctx) ? rd32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(res, 0x14))
        return 0xffffffffu;
    const uint32_t table = rd32(ram, res + 8u), count = rd32(ram, res + 0x10u);
    if (!ramPtr(table) || count > 0x20000u)
        return 0xffffffffu;
    for (uint32_t i = 0; i <= count; ++i)
        if (rd32(ram, table + i * 0x24u) == resId)
            return i;
    return 0xffffffffu;
}

void hookUse(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kUseHandler && g_enabled) {
        const uint32_t item = getRegU32(c, 4), msg = getRegU32(c, 5);
        const uint32_t idPtr = rd32(ram, msg + 4u);
        if (g_trace) {
            const uint32_t handle = rd32(ram, item + 0xcu), rec = recordOf(ram, handle);
            logf("trace: item %x handle %x resource %x message %x id %x user %x", item, handle,
                 rec ? rd32(ram, rec) : 0, msg, rd16(ram, idPtr), rd32(ram, msg));
        }
        if (ramPtr(item, 0x10) && ramPtr(msg, 8) && isUseMessage(rd16(ram, idPtr), rd32(ram, msg))) {
            const uint32_t handle = rd32(ram, item + 0xcu), rec = recordOf(ram, handle);
            const uint32_t resId = rec ? rd32(ram, rec) : 0;
            std::lock_guard<std::mutex> lock(g_mutex);
            const auto it = g_table.find(resId);
            if (it != g_table.end()) {
                if (g_pending.size() < 16)
                    g_pending.push_back(it->second.effect);
                logf("%s (resource %x) used: %s effect %x queued", it->second.name.c_str(), resId,
                     it->second.kind.c_str(), it->second.effect);
            }
        }
    }
    g_origUse(ram, c, rt);
}

// The spawn takes six arguments and a posted guest call four, so the post lands on this host trampoline (the arena's
// way): stage 1 puts the transform on the dispatcher thread's stack and calls the spawn returning to stage 2.
void stage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    g_flight.savedSp = getRegU32(c, 29);
    g_flight.savedRa = getRegU32(c, 31);
    const uint32_t sp = g_flight.savedSp - kFrame, xf = sp + 0x10u;
    std::memcpy(rdram + (xf & PS2_RAM_MASK), g_flight.xform, sizeof g_flight.xform);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, rd32(rdram, kContextPtr));
    SET_GPR_U32(c, 5, g_flight.owner);
    SET_GPR_U32(c, 6, xf);
    SET_GPR_U32(c, 7, 0u);
    SET_GPR_U32(c, 8, 0u);
    SET_GPR_U32(c, 9, g_flight.idx);
    SET_GPR_U32(c, 31, g_slot2);
    c->pc = kFactory;
}

void stage2(uint8_t*, R5900Context* c, PS2Runtime*) {
    g_result.store(getRegU32(c, 2));
    g_serial.fetch_add(1);
    SET_GPR_U32(c, 29, g_flight.savedSp);
    SET_GPR_U32(c, 31, g_flight.savedRa);
    c->pc = g_flight.savedRa;
}

} // namespace

void setTables(const std::vector<std::pair<std::string, std::string>>& files) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_table.clear();
    for (const auto& f : files) {
        const size_t n = parseCrystalTable(f.second, g_table);
        std::fprintf(stderr, "[crystals] %s: %zu crystal(s)\n", f.first.c_str(), n);
    }
}

void install(PS2Runtime& rt) {
    if (const char* v = std::getenv("DRAKAN_CRYSTALS"); v && v[0] == '0') {
        std::fprintf(stderr, "[crystals] off (DRAKAN_CRYSTALS=0): crystals keep the native use\n");
        return;
    }
    if (const char* v = std::getenv("DRAKAN_CRYSTALS_LOG"))
        g_log = v[0] != '0';
    g_trace = std::getenv("DRAKAN_CRYSTALS_TRACE") != nullptr; // test aid: every call of the use handler
    if constexpr (!drakan_region::build().identityAddresses) {
        std::fprintf(stderr, "[crystals] off: PAL addresses only\n");
        return;
    } else {
        g_origUse = rt.lookupFunction(kUseHandler);
        if (!g_origUse) {
            std::fprintf(stderr, "[crystals] off: %x missing\n", kUseHandler);
            return;
        }
        for (uint32_t a = 0x100500u; a < 0x100600u && !(g_slot1 && g_slot2); a += 4) {
            if (rt.hasFunction(a))
                continue;
            if (!g_slot1 && rt.registerFunction(a, &stage1))
                g_slot1 = a;
            else if (g_slot1 && !g_slot2 && rt.registerFunction(a, &stage2))
                g_slot2 = a;
        }
        if (!g_slot1 || !g_slot2) {
            std::fprintf(stderr, "[crystals] off: no free trampoline slots\n");
            return;
        }
        rt.replaceFunction(kUseHandler, &hookUse);
        g_enabled = true;
    }
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram, bool gameplay) {
    if (!g_enabled)
        return;
    ++g_ticks;
    if (g_inFlight && g_serial.load() != g_inflightSerial) {
        g_inFlight = false;
        const uint32_t h = g_result.load();
        logf("effect %x spawned at Rynn (tick %u): actor %x (%s)", g_inflightEffect, g_ticks, h, h ? "ok" : "refused");
    }
    if (g_inFlight && !guest.postBusy() && g_ticks > g_postTick + 4)
        g_inFlight = false;
    if (!gameplay || g_inFlight || guest.postBusy() || drakan_lifecycle::transitionActive())
        return;
    uint32_t effect = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_pending.empty())
            return;
        effect = g_pending.front();
        g_pending.pop_front();
    }
    const uint32_t ctx = rd32(rdram, kContextPtr);
    const uint32_t player = ramPtr(ctx) ? rd32(rdram, ctx + 0x4b0u) : 0;
    const uint32_t rec = recordOf(rdram, player);
    if (!rec || rd32(rdram, rec + 8u) != kRynnClass) {
        logf("effect %x dropped: the controlled actor is not Rynn", effect);
        return;
    }
    const uint32_t idx = findResourceIndex(rdram, effect);
    if (idx == 0xffffffffu) {
        logf("effect %x dropped: no such resource in this level", effect);
        return;
    }
    std::memcpy(g_flight.xform, rdram + ((player + 0x14u) & PS2_RAM_MASK), sizeof g_flight.xform);
    g_flight.idx = idx;
    g_flight.owner = player;
    g_inflightSerial = g_serial.load();
    g_inflightEffect = effect;
    guest.post(drakan_input::GuestCall(g_slot1));
    g_inFlight = true;
    g_postTick = g_ticks;
}

} // namespace drakan_crystals
