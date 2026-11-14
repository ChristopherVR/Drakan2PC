// Point lights per model (PAL). See drakan_model_lights.h.
#include "drakan_model_lights.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/ModelLights.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace drakan_model_lights {
namespace {

namespace ml = world::model_lights;

constexpr uint32_t kBuilder = 0x1c4148u;    // (a0 set, a1 limit, a2 sun, a3 light list, t0 mode, t1 sphere, t2 box,
                                            //  t3 transform; stack +0 full search, +8 cached light indices)
constexpr uint32_t kCandidates = 0x56b6c0u; // {entries, capacity, count}; entry = {distance^2, light, list index}
constexpr uint32_t kCount = 0x20u, kSlotBase = 0x24u, kSlotSize = 0x34u;
constexpr uint32_t kModelLimit = 4u;

std::atomic<bool> g_installed{false};
int g_setting = ml::kOriginal;
bool g_log = false;
PS2Runtime::RecompiledFunction g_original = nullptr;

struct Cached { // the lights the last full search of an object cut off (list indices)
    uint32_t list = 0;
    uint8_t count = 0;
    uint8_t index[ml::kMax]{};
};
std::unordered_map<uint32_t, Cached> g_cached; // key: the object's cached index bytes

struct Stats {
    uint64_t calls = 0, full = 0, candidates = 0, accepted = 0, wouldAccept = 0, cut = 0, callsCut = 0, mismatch = 0,
             suspended = 0;
    uint64_t added[5]{}; // by ml::Added
    uint32_t maxCandidates = 0, maxWould = 0;
    uint64_t histogram[ml::kMax + 1]{}; // lights it would accept (sun included), per call
    uint64_t cutDirectional = 0, cutPoint = 0, cutDark = 0, callsCutDirectionalOnly = 0;
    // the call that would have accepted the most lights since the last report (where to look)
    uint32_t topWould = 0, topAccepted = 0;
    ml::Vec3 topCentre, topOrigin;
    float topRadius = 0;
    bool topTransform = false, topDirectionalOnly = false;
};
Stats g_stats;
std::chrono::steady_clock::time_point g_nextReport;

bool inRam(uint32_t a, uint32_t bytes) {
    return a != 0 && a <= PS2_RAM_SIZE && bytes <= PS2_RAM_SIZE - a;
}
uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
ml::Vec3 vec(const uint8_t* ram, uint32_t a) {
    return {f32(ram, a), f32(ram, a + 4), f32(ram, a + 8)};
}
void put(uint8_t* ram, uint32_t a, uint32_t v) {
    std::memcpy(ram + a, &v, 4);
}
void put(uint8_t* ram, uint32_t a, float v) {
    std::memcpy(ram + a, &v, 4);
}
void put(uint8_t* ram, uint32_t a, ml::Vec3 v) {
    put(ram, a, v.x), put(ram, a + 4, v.y), put(ram, a + 8, v.z);
}

bool readLight(const uint8_t* ram, uint32_t list, uint32_t index, ml::Light& out) {
    if (!inRam(list, 12))
        return false;
    const uint32_t entries = u32(ram, list) & 0x1fffffffu, count = u32(ram, list + 8);
    if (index >= count || count > 4096 || !inRam(entries, count * 4))
        return false;
    const uint32_t light = u32(ram, entries + index * 4) & 0x1fffffffu;
    if (!inRam(light, 0x1c))
        return false;
    out.colour = vec(ram, light);
    out.radius = f32(ram, light + 0xc);
    out.position = vec(ram, light + 0x10);
    return true;
}

void readSet(const uint8_t* ram, uint32_t set, ml::Set& s) {
    s.specular = u32(ram, set);
    s.eye = vec(ram, set + 4);
    s.hasPoint = u32(ram, set + 0x10);
    s.ambient = vec(ram, set + 0x14);
    s.count = static_cast<int>(u32(ram, set + kCount));
    for (int i = 0; i < ml::kSlots; ++i) {
        const uint32_t a = set + kSlotBase + uint32_t(i) * kSlotSize;
        ml::Slot& slot = s.slots[i];
        slot.type = u32(ram, a);
        slot.vector = vec(ram, a + 4);
        slot.colour = vec(ram, a + 0x10);
        slot.radiusSquared = f32(ram, a + 0x1c);
        slot.inverseRadiusSquared = f32(ram, a + 0x20);
        slot.half = vec(ram, a + 0x24);
        slot.specular = u32(ram, a + 0x30);
    }
}

void writeSet(uint8_t* ram, uint32_t set, const ml::Set& s, int fromSlot) {
    put(ram, set + 0x10, s.hasPoint);
    put(ram, set + 0x14, s.ambient);
    put(ram, set + kCount, static_cast<uint32_t>(s.count));
    for (int i = fromSlot; i < std::min(s.count, ml::kSlots); ++i) {
        const uint32_t a = set + kSlotBase + uint32_t(i) * kSlotSize;
        const ml::Slot& slot = s.slots[i];
        put(ram, a, slot.type);
        put(ram, a + 4, slot.vector);
        put(ram, a + 0x10, slot.colour);
        put(ram, a + 0x1c, slot.radiusSquared);
        put(ram, a + 0x20, slot.inverseRadiusSquared);
        put(ram, a + 0x24, slot.half);
        put(ram, a + 0x30, slot.specular);
    }
}

void report() {
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextReport)
        return;
    g_nextReport = now + std::chrono::seconds(5);
    const Stats& s = g_stats;
    if (!s.calls)
        return;
    char histogram[160];
    int n = 0;
    for (int i = 1; i <= ml::kMax && n < int(sizeof(histogram)) - 16; ++i)
        if (s.histogram[i])
            n += std::snprintf(histogram + n, sizeof(histogram) - n, " %d:%llu", i,
                               static_cast<unsigned long long>(s.histogram[i]));
    histogram[n] = 0;
    std::fprintf(stderr,
                 "[model-lights] calls %llu (full search %llu): candidates %.2f (max %u), accepted %.2f, without the "
                 "limit %.2f (max %u); %llu calls cut %llu lights; added: %llu none, %llu free slot, %llu folded "
                 "directional, %llu folded point, %llu ambient; mismatch %llu, "
                 "suspended %llu; lights per call:%s\n",
                 static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.full),
                 double(s.candidates) / double(s.calls), s.maxCandidates, double(s.accepted) / double(s.calls),
                 double(s.wouldAccept) / double(s.calls), s.maxWould, static_cast<unsigned long long>(s.callsCut),
                 static_cast<unsigned long long>(s.cut), static_cast<unsigned long long>(s.added[0]),
                 static_cast<unsigned long long>(s.added[1]), static_cast<unsigned long long>(s.added[2]),
                 static_cast<unsigned long long>(s.added[3]), static_cast<unsigned long long>(s.added[4]),
                 static_cast<unsigned long long>(s.mismatch), static_cast<unsigned long long>(s.suspended), histogram);
    std::fprintf(stderr,
                 "[model-lights] cut lights: %llu directional, %llu point (%llu dark at the object); calls of "
                 "directional-only objects cut %llu; most lit: %u lights (%u kept), sphere %.0f %.0f %.0f r %.0f, "
                 "transform %d at %.0f %.0f %.0f, directional only %d\n",
                 static_cast<unsigned long long>(s.cutDirectional), static_cast<unsigned long long>(s.cutPoint),
                 static_cast<unsigned long long>(s.cutDark), static_cast<unsigned long long>(s.callsCutDirectionalOnly),
                 s.topWould, s.topAccepted, s.topCentre.x, s.topCentre.y, s.topCentre.z, s.topRadius,
                 int(s.topTransform), s.topOrigin.x, s.topOrigin.y, s.topOrigin.z, int(s.topDirectionalOnly));
    g_stats.topWould = 0;
}

void builder(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const bool ours = ctx->pc == kBuilder;
    const uint32_t limit = GPR_U32(ctx, 5);
    if (!ours || limit != kModelLimit || (!g_log && g_setting <= ml::kOriginal)) {
        g_original(ram, ctx, rt);
        return;
    }
    const uint32_t set = GPR_U32(ctx, 4), list = GPR_U32(ctx, 7), sphere = GPR_U32(ctx, 9),
                   transformAt = GPR_U32(ctx, 11), sp = GPR_U32(ctx, 29), returnPc = GPR_U32(ctx, 31);
    const bool directionalOnly = GPR_U32(ctx, 8) != 0;
    if (!inRam(set, kSlotBase + ml::kMax * kSlotSize) || !inRam(sphere, 16) || !inRam(sp, 12)) {
        g_original(ram, ctx, rt);
        return;
    }
    const bool full = u32(ram, sp) != 0;
    const uint32_t cache = u32(ram, sp + 8);
    const uint32_t before = u32(ram, set + kCount);
    g_original(ram, ctx, rt);
    if (ctx->pc != returnPc) { // suspended inside (the candidate list grows under a semaphore): leave this call
        ++g_stats.suspended;
        return;
    }

    ml::Object object;
    object.centre = vec(ram, sphere);
    object.radius = f32(ram, sphere + 12);
    object.directionalOnly = directionalOnly;
    ml::Transform transform;
    if (inRam(transformAt, 0x3c)) {
        transform.present = true;
        transform.origin = vec(ram, transformAt);
        for (int i = 0; i < 9; ++i)
            transform.rotation[i] = f32(ram, transformAt + 0xc + uint32_t(i) * 4);
        transform.scale = vec(ram, transformAt + 0x30);
    }

    const uint32_t after = u32(ram, set + kCount);
    const uint32_t start = before < uint32_t(ml::kMax) ? before + 1 : uint32_t(ml::kMax); // the sun's slot
    uint32_t simulated = start, would = start;
    ml::Evaluated extras[ml::kMax];
    int extraCount = 0;
    uint32_t candidateCount = 0;
    Cached cut;
    cut.list = list;
    if (full) {
        const uint32_t entries = u32(ram, kCandidates) & 0x1fffffffu;
        candidateCount = u32(ram, kCandidates + 8);
        if (candidateCount > 4096 || !inRam(entries, candidateCount * 12))
            candidateCount = 0;
        for (uint32_t i = 0; i < candidateCount && would < uint32_t(ml::kMax); ++i) {
            const uint32_t e = entries + i * 12, light = u32(ram, e + 4) & 0x1fffffffu;
            if (!inRam(light, 0x1c))
                continue;
            ml::Light l{vec(ram, light), f32(ram, light + 0xc), vec(ram, light + 0x10)};
            const ml::Evaluated v = ml::evaluate(l, transform, object);
            if (!ml::accepted(v.kind))
                continue;
            ++would;
            if (simulated < limit) {
                ++simulated;
            } else {
                if (cut.count < ml::kMax)
                    cut.index[cut.count++] = static_cast<uint8_t>(u32(ram, e + 8));
                if (extraCount < ml::kMax)
                    extras[extraCount++] = v;
            }
        }
        if (cache) {
            if (cut.count)
                g_cached[cache] = cut;
            else
                g_cached.erase(cache);
            if (g_cached.size() > 65536)
                g_cached.clear();
        }
        ++g_stats.full;
        if (simulated != after) { // the replay disagrees with the engine: its cut list may hold a light the set has
            ++g_stats.mismatch;
            extraCount = 0;
            if (cache)
                g_cached.erase(cache);
        }
    } else if (cache) {
        candidateCount = u32(ram, kCandidates + 8);
        const auto it = g_cached.find(cache);
        if (it != g_cached.end() && it->second.list == list)
            for (int i = 0; i < it->second.count && would < uint32_t(ml::kMax); ++i) {
                ml::Light l;
                if (!readLight(ram, list, it->second.index[i], l))
                    continue;
                const ml::Evaluated v = ml::evaluate(l, transform, object);
                if (!ml::accepted(v.kind))
                    continue;
                ++would;
                extras[extraCount++] = v;
            }
        would += after > start ? after - start : 0; // the cached lights the engine accepted itself
    }

    Stats& s = g_stats;
    ++s.calls;
    s.candidates += candidateCount;
    s.maxCandidates = std::max(s.maxCandidates, candidateCount);
    const uint32_t accepted = after > before ? after - before : 0;
    s.accepted += accepted;
    s.wouldAccept += would - before;
    s.maxWould = std::max(s.maxWould, would - before);
    ++s.histogram[std::min<uint32_t>(would - before, ml::kMax)];
    if (extraCount) {
        ++s.callsCut;
        s.cut += uint64_t(extraCount);
        if (directionalOnly)
            ++s.callsCutDirectionalOnly;
        for (int i = 0; i < extraCount; ++i) {
            if (extras[i].kind == ml::Kind::Point) {
                ++s.cutPoint;
                if (!(extras[i].attenuation > 0))
                    ++s.cutDark;
            } else {
                ++s.cutDirectional;
            }
        }
    }
    if (would - before > s.topWould) {
        s.topWould = would - before;
        s.topAccepted = accepted;
        s.topCentre = object.centre;
        s.topRadius = object.radius;
        s.topTransform = transform.present;
        s.topOrigin = transform.origin;
        s.topDirectionalOnly = directionalOnly;
    }

    const int allowed = g_setting - ml::kOriginal;
    if (allowed > 0 && extraCount > 0 && after >= 1 && after <= uint32_t(ml::kSlots)) {
        ml::Set lights;
        readSet(ram, set, lights);
        const int firstChanged = lights.count;
        int changedFrom = firstChanged;
        for (int i = 0; i < std::min(extraCount, allowed); ++i) {
            const ml::Added how = ml::add(lights, extras[i]);
            ++s.added[how];
            if (how != ml::kNothing && how != ml::kFreeSlot)
                changedFrom = 1;
        }
        writeSet(ram, set, lights, std::min(changedFrom, firstChanged));
    }
    if (g_log)
        report();
}

} // namespace

void install(PS2Runtime& rt) {
    if constexpr (!drakan_region::build().identityAddresses)
        return;
    if (g_installed.exchange(true))
        return;
    g_log = std::getenv("DRAKAN_MODEL_LIGHTS_LOG") != nullptr;
    g_setting = ml::clampSetting(drakan_cfg::getInt("video", "model_lights", "DRAKAN_MODEL_LIGHTS", ml::kOriginal));
    g_nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (rt.hasFunction(kBuilder)) {
        g_original = rt.lookupFunction(kBuilder);
        rt.replaceFunction(kBuilder, &builder);
    }
    std::fprintf(stderr, "[model-lights] installed (lights per model %d%s)\n", g_setting,
                 g_setting == ml::kOriginal ? ", original" : "");
}

} // namespace drakan_model_lights
