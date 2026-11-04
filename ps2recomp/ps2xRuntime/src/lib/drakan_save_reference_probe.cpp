#include "drakan_save_reference_probe.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace drakan_save_reference_probe {
namespace {
using Fn = PS2Runtime::RecompiledFunction;
constexpr uint32_t kLoad = 0x16adb8, kRecord = 0x16c590, kResolve = 0x16d4f0;
Fn g_load = nullptr, g_record = nullptr, g_resolve = nullptr;
using Owner = std::pair<int, size_t>;
struct Scope {
    uint32_t context, slot, ra;
};
std::map<Owner, uint32_t> g_loadReturns;
std::map<Owner, Scope> g_scopes;
std::set<std::tuple<int, uint32_t, uint32_t, uint32_t>> g_seen;
unsigned g_lines = 0, g_externalLines = 0, g_limit = 512, g_calls = 0, g_staticCalls = 0, g_recordStaticCalls = 0,
         g_records = 0;
bool g_active = false;
Owner owner(PS2Runtime* rt) {
    const auto* thread = rt->eeScheduler().currentThread();
    return {rt->eeScheduler().currentThreadId(), thread ? thread->invocations.size() : 0};
}
uint32_t read(uint8_t* ram, uint32_t address) {
    address &= 0x1fffffff;
    uint32_t value = 0;
    if (address >= 0x100000 && address <= PS2_RAM_SIZE - 4)
        std::memcpy(&value, ram + address, 4);
    return value;
}
void load(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kLoad;
    const Owner who = owner(rt);
    if (entry) {
        if (g_loadReturns.size() < 16 || g_loadReturns.count(who))
            g_loadReturns[who] = getRegU32(c, 31);
        g_scopes.clear();
        g_seen.clear();
        g_calls = g_staticCalls = g_recordStaticCalls = g_records = g_lines = g_externalLines = 0;
        g_active = true;
        std::fprintf(stderr, "[save-ref] load begin thread=%d context=%08x maxLines=%u\n", owner(rt).first,
                     getRegU32(c, 4), g_limit);
    }
    const auto returning = g_loadReturns.find(who);
    const uint32_t ra = returning == g_loadReturns.end() ? 0 : returning->second;
    g_load(ram, c, rt);
    if (ra && c->pc == ra) {
        g_loadReturns.erase(who);
        g_active = false;
        std::fprintf(stderr,
                     "[save-ref] load returned references=%u staticReferences=%u recordStaticReferences=%u records=%u "
                     "result=%08x\n",
                     g_calls, g_staticCalls, g_recordStaticCalls, g_records, getRegU32(c, 2));
    }
}
void record(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const Owner who = owner(rt);
    if (c->pc == kRecord) {
        ++g_records;
        if (g_scopes.size() < 128 || g_scopes.count(who))
            g_scopes[who] = {getRegU32(c, 4), getRegU32(c, 7), getRegU32(c, 31)};
    }
    const auto scope = g_scopes.find(who);
    const uint32_t ra = scope == g_scopes.end() ? 0 : scope->second.ra;
    g_record(ram, c, rt);
    if (ra && c->pc == ra)
        g_scopes.erase(who);
}
void resolve(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (g_active && c->pc == kResolve) {
        ++g_calls;
        const uint32_t table = getRegU32(c, 4), slot = getRegU32(c, 5), ra = getRegU32(c, 31);
        const uint32_t context = table >= 0x58 ? table - 0x58 : 0;
        const uint32_t total = read(ram, context + 0x68), statics = read(ram, 0x569688);
        const uint32_t first = total >= statics ? total - statics : 0;
        // Only the actual world-context resolver has this table layout. No arbitrary pointer dereferences.
        if (context == read(ram, 0x5565d4) && total < 0x100000 && statics <= total && slot >= first && slot < total) {
            ++g_staticCalls;
            const Owner who = owner(rt);
            uint32_t source = 0xffffffff;
            size_t depth = 0;
            for (const auto& [key, scope] : g_scopes)
                if (key.first == who.first && key.second <= who.second && key.second >= depth &&
                    scope.context == context) {
                    source = scope.slot;
                    depth = key.second;
                }
            if (source != 0xffffffff)
                ++g_recordStaticCalls;
            const bool budget = source != 0xffffffff ? g_lines < g_limit : g_externalLines < 32;
            if (budget && g_seen.size() < 4096 && g_seen.emplace(who.first, ra, slot, source).second) {
                if (source != 0xffffffff)
                    ++g_lines;
                else
                    ++g_externalLines;
                std::fprintf(stderr,
                             "[save-ref] static thread=%d depth=%zu caller=%08x slot=%u first=%u total=%u "
                             "recordSlot=%u recordId=%u\n",
                             who.first, who.second, ra, slot, first, total, source,
                             source == 0xffffffff ? 0 : source + 1);
                if (g_lines == g_limit)
                    std::fprintf(stderr, "[save-ref] detail limit reached; counters continue\n");
            }
        }
    }
    g_resolve(ram, c, rt);
}
} // namespace
void install(PS2Runtime& rt) {
    const char* enabled = std::getenv("DRAKAN_SAVE_REF_PROBE");
    if (!enabled || std::strcmp(enabled, "1") != 0)
        return;
    if (!drakan_region::build().identityAddresses) {
        std::fprintf(stderr, "[save-ref] PAL-only observer not installed\n");
        return;
    }
    if (g_load)
        return;
    if (!rt.hasFunction(kLoad) || !rt.hasFunction(kRecord) || !rt.hasFunction(kResolve)) {
        std::fprintf(stderr, "[save-ref] required functions missing; no hooks installed\n");
        return;
    }
    if (const char* limit = std::getenv("DRAKAN_SAVE_REF_PROBE_LINES"))
        g_limit = std::clamp(std::atoi(limit), 1, 4096);
    g_load = rt.lookupFunction(kLoad);
    g_record = rt.lookupFunction(kRecord);
    g_resolve = rt.lookupFunction(kResolve);
    rt.replaceFunction(kLoad, load);
    rt.replaceFunction(kRecord, record);
    rt.replaceFunction(kResolve, resolve);
    std::fprintf(stderr, "[save-ref] installed maxLines=%u (entry observations; yielded calls are not returns)\n",
                 g_limit);
}
} // namespace drakan_save_reference_probe
