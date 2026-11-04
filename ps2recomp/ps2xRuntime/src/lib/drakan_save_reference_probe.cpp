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
constexpr uint32_t kLoad = 0x16adb8, kRecord = 0x16c590, kResolve = 0x16d4f0, kRead = 0x4a1650;
Fn g_load = nullptr, g_record = nullptr, g_resolve = nullptr, g_read = nullptr;
using Owner = std::pair<int, size_t>;
struct Scope {
    uint32_t context, slot, ra, stream;
    uint32_t lastOffset = 0xffffffff, lastSize = 0, lastValue = 0;
    bool valid = true;
};
struct PendingRead {
    Owner record;
    uint32_t slot, offset, requested, sizePointer, destination, ra;
    bool compressed;
};
std::map<Owner, PendingRead> g_reads;
std::map<Owner, uint32_t> g_loadReturns;
std::map<Owner, Scope> g_scopes;
std::set<std::tuple<int, uint32_t, uint32_t, uint32_t, uint32_t>> g_seen;
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
uint32_t compressedOffset(uint8_t* ram, uint32_t stream) {
    const uint32_t state = read(ram, stream + 0x24);
    if (state < 0x100000 || state > PS2_RAM_SIZE - 0x6c)
        return 0xffffffff;
    // Native z_stream has 64-bit total_out at +0x18. 4a3060 fills the output buffer;
    // 4a2ed0 advances +0x50 within its +0x54 available bytes. Include all readers/skips.
    const uint32_t total = read(ram, state + 0x18), cursor = read(ram, state + 0x50), size = read(ram, state + 0x54);
    if (read(ram, state + 0x1c) || total > PS2_RAM_SIZE || cursor > size || size - cursor > total)
        return 0xffffffff;
    return total - (size - cursor);
}
void load(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kLoad;
    const Owner who = owner(rt);
    if (entry) {
        if (g_loadReturns.size() < 16 || g_loadReturns.count(who))
            g_loadReturns[who] = getRegU32(c, 31);
        g_scopes.clear();
        g_reads.clear();
        g_seen.clear();
        g_calls = g_staticCalls = g_recordStaticCalls = g_records = g_lines = g_externalLines = 0;
        g_active = true;
        std::fprintf(stderr, "[save-ref] load begin thread=%d context=%08x maxLines=%u\n", owner(rt).first,
                     getRegU32(c, 4), g_limit);
    }
    const auto returning = g_loadReturns.find(who);
    const uint32_t ra = returning == g_loadReturns.end() ? 0 : returning->second;
    g_load(ram, c, rt);
    if (returning != g_loadReturns.end() && c->pc == ra) {
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
            g_scopes[who] = {getRegU32(c, 4), getRegU32(c, 7), getRegU32(c, 31), getRegU32(c, 5)};
    }
    const auto scope = g_scopes.find(who);
    const uint32_t ra = scope == g_scopes.end() ? 0 : scope->second.ra;
    g_record(ram, c, rt);
    if (scope != g_scopes.end() && c->pc == ra)
        g_scopes.erase(who);
}
// Observe successful reads at the native decompressor cursor. Summing this API's reads is insufficient:
// string and skip routines can consume bytes through 4a2ed0 directly. Uncompressed offsets remain unknown.
void chunkRead(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const Owner who = owner(rt);
    if (g_active && c->pc == kRead) {
        const uint32_t stream = getRegU32(c, 4);
        auto active = g_scopes.end();
        for (auto it = g_scopes.begin(); it != g_scopes.end(); ++it)
            if (it->first.first == who.first && it->first.second <= who.second && it->second.stream == stream &&
                (active == g_scopes.end() || it->first.second >= active->first.second))
                active = it;
        if (active != g_scopes.end() && (g_reads.size() < 128 || g_reads.count(who))) {
            const uint32_t sizePointer = getRegU32(c, 6), requested = read(ram, sizePointer);
            g_reads[who] = {active->first,   active->second.slot, compressedOffset(ram, stream), requested, sizePointer,
                            getRegU32(c, 5), getRegU32(c, 31),    read(ram, stream + 0x24) != 0};
        }
    }
    const auto pending = g_reads.find(who);
    const bool tracked = pending != g_reads.end();
    const PendingRead saved = tracked ? pending->second : PendingRead{};
    g_read(ram, c, rt);
    if (tracked && c->pc == saved.ra) {
        g_reads.erase(who);
        const auto scope = g_scopes.find(saved.record);
        if (scope != g_scopes.end() && scope->second.slot == saved.slot) {
            Scope& record = scope->second;
            const uint32_t actual = saved.compressed ? saved.requested : read(ram, saved.sizePointer);
            if (getRegU32(c, 2) != 0 || actual > saved.requested || actual > PS2_RAM_SIZE) {
                record.valid = false;
            } else {
                record.lastOffset = saved.offset;
                record.lastSize = actual;
                record.lastValue = actual == 4 ? read(ram, saved.destination) : 0;
            }
        }
    }
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
            uint32_t payloadOffset = 0xffffffff;
            for (const auto& [key, scope] : g_scopes)
                if (key.first == who.first && key.second <= who.second && key.second >= depth &&
                    scope.context == context) {
                    source = scope.slot;
                    depth = key.second;
                    payloadOffset =
                        scope.valid && scope.lastSize == 4 && scope.lastValue == slot ? scope.lastOffset : 0xffffffff;
                }
            if (source != 0xffffffff)
                ++g_recordStaticCalls;
            const bool budget = source != 0xffffffff ? g_lines < g_limit : g_externalLines < 32;
            if (budget && g_seen.size() < 4096 && g_seen.emplace(who.first, ra, slot, source, payloadOffset).second) {
                if (source != 0xffffffff)
                    ++g_lines;
                else
                    ++g_externalLines;
                const uint32_t sp = getRegU32(c, 29);
                const uint32_t wrapperCaller = ra == 0x16dee8 ? read(ram, sp) : ra;
                const uint32_t serializerCaller = wrapperCaller == 0x189aac ? read(ram, sp + 0x60) : wrapperCaller;
                std::fprintf(stderr,
                             "[save-ref] static thread=%d depth=%zu caller=%08x slot=%u first=%u total=%u "
                             "recordSlot=%u recordId=%u wrapperCaller=%08x serializerCaller=%08x payloadOffset=%08x\n",
                             who.first, who.second, ra, slot, first, total, source,
                             source == 0xffffffff ? 0 : source + 1, wrapperCaller, serializerCaller, payloadOffset);
                if (source != 0xffffffff && g_lines == g_limit)
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
    if (!rt.hasFunction(kLoad) || !rt.hasFunction(kRecord) || !rt.hasFunction(kResolve) || !rt.hasFunction(kRead)) {
        std::fprintf(stderr, "[save-ref] required functions missing; no hooks installed\n");
        return;
    }
    if (const char* limit = std::getenv("DRAKAN_SAVE_REF_PROBE_LINES"))
        g_limit = std::clamp(std::atoi(limit), 1, 4096);
    g_load = rt.lookupFunction(kLoad);
    g_record = rt.lookupFunction(kRecord);
    g_resolve = rt.lookupFunction(kResolve);
    g_read = rt.lookupFunction(kRead);
    rt.replaceFunction(kLoad, load);
    rt.replaceFunction(kRecord, record);
    rt.replaceFunction(kResolve, resolve);
    rt.replaceFunction(kRead, chunkRead);
    std::fprintf(stderr, "[save-ref] installed maxLines=%u (entry observations; yielded calls are not returns)\n",
                 g_limit);
}
} // namespace drakan_save_reference_probe
