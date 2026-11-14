#include "drakan_model_detail.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "world/ModelDetail.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace drakan_model_detail {
namespace {

namespace rules = world::model_detail;

constexpr uint32_t kLodSelect = 0x102c90u;      // a0 = skeleton controller, a1 = model resource (or 0), f12 = d^2
constexpr uint32_t kFarModelSelect = 0x18b760u; // a0 = object, a1 = model resource, f12 = d^2
constexpr uint32_t kDistanceTest = 0x18ba28u;   // a0 = object, a1 = bounds; v0 = 1 when beyond the draw distance
constexpr uint32_t kRenderObject = 0x189b38u;   // a0 = object
constexpr uint32_t kObjectVtable = 0x51b8b0u;
constexpr uint32_t kSkinnedModelVtable = 0x519a00u;
constexpr uint32_t kRamMask = 0x1fffffffu;
constexpr uint32_t kRamSize = PS2_RAM_SIZE;
constexpr uint32_t kDrawDistance = 0xf8u;           // object: f32 draw distance
constexpr uint32_t kDrawDistanceMode = 0x30000000u; // object +4: 0 = no own draw distance

std::atomic<bool> g_installed{false};
std::atomic<float> g_lodFactor{rules::kOff};
std::atomic<float> g_drawFactor{rules::kOff};
PS2Runtime::RecompiledFunction g_origLod = nullptr;
PS2Runtime::RecompiledFunction g_origFarModel = nullptr;
PS2Runtime::RecompiledFunction g_origDistance = nullptr;
PS2Runtime::RecompiledFunction g_origRender = nullptr;

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
}
inline uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}

// ---- look-changing LODs
// A resident skinned model: LOD count at +0xc4, table at +0xbc (16 bytes per LOD: f32 squared switch distance, pointer
// to a descriptor with the first polygon at +16 and the polygon count at +20), polygons at +0x28 (12 bytes: four u16
// vertex indices, u16 material, u16 flags), polygon count at +0x20.
struct LookRule {
    uint64_t signature = 0;
    float limitSquared = 0; // squared switch distance of the first look-changing LOD
    bool limited = false;
};
std::mutex g_rulesMutex;
std::unordered_map<uint32_t, LookRule> g_lookRules; // by model address

uint64_t modelSignature(const uint8_t* ram, uint32_t m) {
    // Identity of what the rule was computed from: resource id, polygon count and array, LOD table and count.
    uint64_t h = 1469598103934665603ull;
    for (const uint32_t off : {0x0u, 0x1cu, 0x20u, 0x28u, 0x38u, 0xbcu, 0xc4u}) {
        h ^= u32(ram, m + off);
        h *= 1099511628211ull;
    }
    return h;
}

LookRule computeLookRule(const uint8_t* ram, uint32_t m) {
    LookRule rule;
    rule.signature = modelSignature(ram, m);
    const uint32_t count = u32(ram, m + 0xc4), table = u32(ram, m + 0xbc) & kRamMask;
    const uint32_t polygons = u32(ram, m + 0x28) & kRamMask, polygonCount = u32(ram, m + 0x20);
    if (count < 2 || count > 16 || !inRam(table, count * 16) || polygonCount > 100000 ||
        !inRam(polygons, polygonCount * 12))
        return rule;
    std::vector<std::vector<uint16_t>> materials(count);
    for (uint32_t lod = 0; lod < count; ++lod) {
        const uint32_t descriptor = u32(ram, table + lod * 16 + 4) & kRamMask;
        if (!inRam(descriptor, 24))
            return rule;
        const uint32_t first = u32(ram, descriptor + 16), faces = u32(ram, descriptor + 20);
        if (first > polygonCount || faces > polygonCount - first)
            return rule;
        bool seen[64] = {};
        for (uint32_t f = first; f < first + faces; ++f) {
            uint16_t material;
            std::memcpy(&material, ram + polygons + f * 12 + 8, 2);
            if (material < 64 && seen[material])
                continue;
            if (material < 64)
                seen[material] = true;
            materials[lod].push_back(material);
        }
    }
    if (const unsigned lod = rules::firstLookChangingLod(materials)) {
        rule.limitSquared = f32(ram, table + lod * 16);
        rule.limited = true;
    }
    return rule;
}

// The rule of the model at `m`, recomputed when another model was streamed in at that address.
LookRule lookRule(const uint8_t* ram, uint32_t m) {
    const uint64_t signature = modelSignature(ram, m);
    std::lock_guard<std::mutex> lock(g_rulesMutex);
    auto it = g_lookRules.find(m);
    if (it != g_lookRules.end() && it->second.signature == signature)
        return it->second;
    if (g_lookRules.size() > 4096)
        g_lookRules.clear();
    return g_lookRules[m] = computeLookRule(ram, m);
}

// ---- DRAKAN_MODEL_DETAIL_LOG
// Counts per report window: which LOD each skinned model seen by the selector ended on and how many polygons that is,
// and how many objects the distance test hid.
bool g_log = false;
struct LogState {
    std::mutex mutex;
    struct Actor {
        uint32_t slot = 0, polygons = 0, vertices = 0, fullPolygons = 0;
        bool limited = false;
    };
    std::unordered_map<uint32_t, Actor> actors; // by controller
    std::unordered_map<uint32_t, bool> tested;  // object -> hidden by distance
    uint64_t selectorCalls = 0, renderCalls = 0, scaledObjects = 0;
    std::chrono::steady_clock::time_point next{};
} g_logState;

void logSelector(const uint8_t* ram, uint32_t controller, uint32_t m, bool limited) {
    LogState::Actor actor;
    actor.limited = limited;
    actor.slot = u32(ram, controller + 0x10);
    if (m && inRam(m, 0xc8) && u32(ram, m + 0x18) == kSkinnedModelVtable) {
        const uint32_t count = u32(ram, m + 0xc4), table = u32(ram, m + 0xbc) & kRamMask;
        if (count && count <= 16 && inRam(table, count * 16)) {
            auto read = [&](uint32_t slot, uint32_t& polygons, uint32_t& vertices) {
                const uint32_t descriptor = u32(ram, table + slot * 16 + 4) & kRamMask;
                if (inRam(descriptor, 24)) {
                    vertices = u32(ram, descriptor + 12);
                    polygons = u32(ram, descriptor + 20);
                }
            };
            uint32_t unused = 0;
            read(actor.slot < count ? actor.slot : 0, actor.polygons, actor.vertices);
            read(0, actor.fullPolygons, unused);
        }
    }
    std::lock_guard<std::mutex> lock(g_logState.mutex);
    ++g_logState.selectorCalls;
    g_logState.actors[controller] = actor;
}

void logReport() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_logState.mutex);
    if (now < g_logState.next)
        return;
    g_logState.next = now + std::chrono::seconds(5);
    uint32_t slots[3] = {}, polygons = 0, vertices = 0, full = 0, limited = 0;
    for (const auto& [controller, actor] : g_logState.actors) {
        ++slots[actor.slot < 2 ? actor.slot : 2];
        polygons += actor.polygons;
        vertices += actor.vertices;
        full += actor.fullPolygons;
        limited += actor.limited;
    }
    uint32_t hidden = 0;
    for (const auto& [object, isHidden] : g_logState.tested)
        hidden += isHidden;
    std::fprintf(stderr,
                 "[model-detail] lod x%.2f draw x%.2f | characters %zu: LOD0 %u LOD1 %u LOD2+ %u, polygons %u "
                 "(all on LOD0: %u), vertices %u, look rule on %u | distance test: objects %zu, hidden %u, own "
                 "distance scaled %llu | selector calls %llu, render calls %llu\n",
                 g_lodFactor.load(), g_drawFactor.load(), g_logState.actors.size(), slots[0], slots[1], slots[2],
                 polygons, full, vertices, limited, g_logState.tested.size(), hidden,
                 static_cast<unsigned long long>(g_logState.scaledObjects),
                 static_cast<unsigned long long>(g_logState.selectorCalls),
                 static_cast<unsigned long long>(g_logState.renderCalls));
    g_logState.actors.clear();
    g_logState.tested.clear();
    g_logState.selectorCalls = g_logState.renderCalls = g_logState.scaledObjects = 0;
}

// ---- hooks
// A wrapped function is entered at its first instruction for a call and at a later instruction when the guest
// scheduler resumes a call it had unwound (ps2_runtime.h, PS2_JAL). Arguments are only valid, and only changed, on a
// true entry.
void hookLodSelect(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kLodSelect;
    const float factor = g_lodFactor.load(std::memory_order_relaxed);
    const uint32_t controller = GPR_U32(c, 4) & kRamMask, m = GPR_U32(c, 5) & kRamMask;
    const uint32_t ra = GPR_U32(c, 31);
    bool limited = false;
    if (entry && factor > rules::kOff) {
        float d2 = rules::lodDistanceSquared(c->f[12], factor);
        if (m && inRam(m, 0xc8) && u32(ram, m + 0x18) == kSkinnedModelVtable) {
            const LookRule rule = lookRule(ram, m);
            if (rule.limited) {
                d2 = rules::belowLod(d2, rule.limitSquared);
                limited = true;
            }
        }
        c->f[12] = d2;
    }
    g_origLod(ram, c, rt);
    if (g_log && entry && c->pc == ra && inRam(controller, 0x14)) {
        logSelector(ram, controller, m, limited);
        logReport();
    }
}

void hookFarModelSelect(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const float factor = g_lodFactor.load(std::memory_order_relaxed);
    if (c->pc == kFarModelSelect && factor > rules::kOff)
        c->f[12] = rules::lodDistanceSquared(c->f[12], factor);
    g_origFarModel(ram, c, rt);
}

// One object whose draw distance is multiplied for the length of a guest call. The stored value goes back when that
// call returns to its caller, also when the scheduler unwound and resumed it in between.
struct DrawScope {
    int thread;
    uint32_t function, returnPc, object;
    float stored, scaled;
};
std::mutex g_scopeMutex;
std::vector<DrawScope> g_scopes;

void restoreScope(uint8_t* ram, const DrawScope& scope) {
    // Not when the call itself wrote a new value (the engine recomputes the radius-based distance in places) or the
    // object is gone.
    if (inRam(scope.object, 0x120) && u32(ram, scope.object) == kObjectVtable &&
        std::memcmp(ram + scope.object + kDrawDistance, &scope.scaled, 4) == 0)
        std::memcpy(ram + scope.object + kDrawDistance, &scope.stored, 4);
}

// Returns true when the object's draw distance was changed for this call.
bool enterDrawScope(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t function) {
    const float factor = g_drawFactor.load(std::memory_order_relaxed);
    const uint32_t object = GPR_U32(c, 4) & kRamMask;
    if (c->pc != function || !(factor > rules::kOff) || !inRam(object, 0x120) || u32(ram, object) != kObjectVtable ||
        !(u32(ram, object + 4) & kDrawDistanceMode))
        return false;
    const float stored = f32(ram, object + kDrawDistance);
    const float scaled = rules::drawDistance(stored, factor);
    if (std::memcmp(&stored, &scaled, 4) == 0)
        return false;
    std::lock_guard<std::mutex> lock(g_scopeMutex);
    for (const DrawScope& scope : g_scopes)
        if (scope.object == object)
            return false;        // already multiplied by an enclosing or suspended call
    if (g_scopes.size() >= 64) { // calls that never returned (a thread that ended inside one)
        restoreScope(ram, g_scopes.front());
        g_scopes.erase(g_scopes.begin());
    }
    DrawScope scope{};
    scope.thread = rt->eeScheduler().currentThreadId();
    scope.function = function;
    scope.returnPc = GPR_U32(c, 31);
    scope.object = object;
    scope.stored = stored;
    scope.scaled = scaled;
    g_scopes.push_back(scope);
    std::memcpy(ram + object + kDrawDistance, &scaled, 4);
    return true;
}

void leaveDrawScope(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t function) {
    std::lock_guard<std::mutex> lock(g_scopeMutex);
    if (g_scopes.empty())
        return;
    const int thread = rt->eeScheduler().currentThreadId();
    for (size_t i = g_scopes.size(); i-- > 0;) {
        const DrawScope& scope = g_scopes[i];
        if (scope.thread == thread && scope.function == function && scope.returnPc == c->pc) {
            restoreScope(ram, scope);
            g_scopes.erase(g_scopes.begin() + static_cast<std::ptrdiff_t>(i));
            return; // the innermost call returned; an enclosing one returns through its own hook
        }
    }
}

void hookDistanceTest(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kDistanceTest;
    const uint32_t object = GPR_U32(c, 4) & kRamMask, ra = GPR_U32(c, 31);
    const bool scaled = enterDrawScope(ram, c, rt, kDistanceTest);
    g_origDistance(ram, c, rt);
    leaveDrawScope(ram, c, rt, kDistanceTest);
    if (g_log && entry && c->pc == ra) {
        std::lock_guard<std::mutex> lock(g_logState.mutex);
        g_logState.tested[object] = GPR_U32(c, 2) != 0;
        g_logState.scaledObjects += scaled;
    }
}

void hookRenderObject(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kRenderObject;
    enterDrawScope(ram, c, rt, kRenderObject);
    g_origRender(ram, c, rt);
    leaveDrawScope(ram, c, rt, kRenderObject);
    if (g_log && entry) {
        {
            std::lock_guard<std::mutex> lock(g_logState.mutex);
            ++g_logState.renderCalls;
        }
        logReport();
    }
}

} // namespace

void configure(float lod, float draw) {
    g_lodFactor.store(rules::clampLodFactor(lod), std::memory_order_relaxed);
    g_drawFactor.store(rules::clampDrawFactor(draw), std::memory_order_relaxed);
}

float lodFactor() {
    return g_lodFactor.load(std::memory_order_relaxed);
}

float drawFactor() {
    return g_drawFactor.load(std::memory_order_relaxed);
}

bool available() {
    return g_installed.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses)
        return;
    for (const uint32_t address : {kLodSelect, kFarModelSelect, kDistanceTest, kRenderObject})
        if (!rt.hasFunction(address))
            return;
    configure(static_cast<float>(
                  drakan_cfg::getFloat("video", "lod_distance", "DRAKAN_LOD_DISTANCE", rules::kDefaultLodFactor)),
              static_cast<float>(
                  drakan_cfg::getFloat("video", "draw_distance", "DRAKAN_DRAW_DISTANCE", rules::kDefaultDrawFactor)));
    g_log = std::getenv("DRAKAN_MODEL_DETAIL_LOG") != nullptr;
    g_origLod = rt.lookupFunction(kLodSelect);
    g_origFarModel = rt.lookupFunction(kFarModelSelect);
    g_origDistance = rt.lookupFunction(kDistanceTest);
    g_origRender = rt.lookupFunction(kRenderObject);
    rt.replaceFunction(kLodSelect, &hookLodSelect);
    rt.replaceFunction(kFarModelSelect, &hookFarModelSelect);
    rt.replaceFunction(kDistanceTest, &hookDistanceTest);
    rt.replaceFunction(kRenderObject, &hookRenderObject);
    g_installed = true;
    std::fprintf(stderr, "[model-detail] installed (LOD distance x%.2f, draw distance x%.2f)\n", g_lodFactor.load(),
                 g_drawFactor.load());
}

} // namespace drakan_model_detail
