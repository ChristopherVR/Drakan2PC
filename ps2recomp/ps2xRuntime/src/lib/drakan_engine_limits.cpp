// PS2-era engine budgets the PC lifts (PAL). See drakan_engine_limits.h.
#include "drakan_engine_limits.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/EngineLimits.h"

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace drakan_engine_limits {
namespace {

namespace sd = world::shadow_distance;
namespace rc = world::resource_cache;
namespace ai = world::ai_run_max;

constexpr uint32_t kShadowSetter = 0x1431e8u; // (a0 this, a1 start, a2 end, a3, t0): squared distances x 4194.304
constexpr uint32_t kShadowEnd = 0x4e0ad4u, kShadowCutoff = 0x4e0ad8u, kShadowStart = 0x4e0adcu;
constexpr uint32_t kCacheTrim = 0x12c850u; // (a0 cache, a1 bytes to make room for, 0 = down to the budget)
constexpr uint32_t kModelCache = 0x4dae20u, kAnimationCache = 0x4db050u;
constexpr uint32_t kBudget = 4u, kInUse = 8u;
// The AI limiter: 0x5130b0 {+0 slot list, +4 capacity, +8 slots in use, +12 run max}. 0x2dc718 (level load) stores the
// level's "AI Run Max" at +12 and empties the list; 0x48a4a0 (per limited creature) takes a slot while fewer than +12
// are in use (the list grows in steps of 8), else gives the farthest creature's slot to a nearer one.
constexpr uint32_t kAiLoad = 0x2dc718u, kAiLoadEnd = 0x2dc7b0u, kAiLimiter = 0x48a4a0u;
constexpr uint32_t kAiInUse = 0x5130b8u, kAiRunMax = 0x5130bcu;

std::atomic<bool> g_installed{false};
float g_shadowFactor = sd::kOff;
uint32_t g_modelBudget = 0, g_animationBudget = 0;
bool g_log = false;
PS2Runtime::RecompiledFunction g_shadowOriginal = nullptr, g_trimOriginal = nullptr;
PS2Runtime::RecompiledFunction g_aiLoadOriginal = nullptr, g_aiLimiterOriginal = nullptr;
int g_aiOption = 0;              // [general] ai_run_max, 0 = the level's own
int32_t g_aiWritten = INT32_MIN; // the run max last left in RAM (ours or the level's)
int32_t g_aiLevel = 0, g_aiPeak = 0;

struct CacheStats {
    const char* name;
    uint64_t calls = 0, dropped = 0, nextReport = 1u << 20;
    uint32_t loggedBudget = 0;
};
std::mutex g_mutex;
CacheStats g_model{"model"}, g_animation{"animation"};

uint32_t read32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}

void scaleShadow(uint8_t* ram, uint32_t address) {
    float stored;
    std::memcpy(&stored, ram + address, 4);
    const float scaled = sd::scaledSquared(stored, g_shadowFactor);
    std::memcpy(ram + address, &scaled, 4);
}

void shadowSetter(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const bool ours = ctx->pc == kShadowSetter;
    const uint32_t start = GPR_U32(ctx, 5), end = GPR_U32(ctx, 6);
    g_shadowOriginal(ram, ctx, rt); // a leaf: it has stored the three values when it returns
    if (!ours)
        return;
    if (g_shadowFactor > sd::kOff) {
        scaleShadow(ram, kShadowStart);
        scaleShadow(ram, kShadowEnd);
        scaleShadow(ram, kShadowCutoff);
    }
    if (g_log || g_shadowFactor > sd::kOff) {
        float s, e;
        std::memcpy(&s, ram + kShadowStart, 4);
        std::memcpy(&e, ram + kShadowEnd, 4);
        std::fprintf(stderr, "[engine-limits] shadow distance: level %d / %d, squared %.0f / %.0f (x%.2f)\n",
                     static_cast<int>(start), static_cast<int>(end), s, e, g_shadowFactor);
    }
}

void cacheTrim(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t cache = GPR_U32(ctx, 4);
    CacheStats* stats = nullptr;
    uint32_t target = 0;
    if (ctx->pc == kCacheTrim) {
        if (cache == kModelCache)
            stats = &g_model, target = g_modelBudget;
        else if (cache == kAnimationCache)
            stats = &g_animation, target = g_animationBudget;
    }
    if (!stats) {
        g_trimOriginal(ram, ctx, rt);
        return;
    }
    const uint32_t budget = read32(ram, cache + kBudget);
    if (const uint32_t raised = rc::raisedBudget(budget, target)) {
        std::memcpy(ram + cache + kBudget, &raised, 4);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (stats->loggedBudget != budget) { // once per level budget value
            stats->loggedBudget = budget;
            std::fprintf(stderr, "[engine-limits] %s cache budget %u -> %u bytes (in use %u)\n", stats->name, budget,
                         raised, read32(ram, cache + kInUse));
        }
    }
    const uint32_t returnPc = GPR_U32(ctx, 31), request = GPR_U32(ctx, 5);
    const uint32_t before = read32(ram, cache + kInUse);
    g_trimOriginal(ram, ctx, rt);
    if (ctx->pc != returnPc) // suspended inside (it waits on the cache's semaphore): nothing to count yet
        return;
    const uint32_t after = read32(ram, cache + kInUse);
    std::lock_guard<std::mutex> lock(g_mutex);
    ++stats->calls;
    if (after < before)
        stats->dropped += before - after;
    if (g_log)
        std::fprintf(stderr, "[engine-limits] %s cache trim: request %u, budget %u, in use %u -> %u\n", stats->name,
                     request, read32(ram, cache + kBudget), before, after);
    if (stats->dropped >= stats->nextReport) {
        std::fprintf(stderr, "[engine-limits] %s cache dropped %.1f MiB so far (%llu trims, budget %u)\n", stats->name,
                     stats->dropped / 1048576.0, static_cast<unsigned long long>(stats->calls),
                     read32(ram, cache + kBudget));
        while (stats->nextReport <= stats->dropped)
            stats->nextReport += 1u << 20;
    }
}

// Raises the run max when the level has stored a new value (a value other than the one we left there).
void applyAiRunMax(uint8_t* ram) {
    const int32_t stored = static_cast<int32_t>(read32(ram, kAiRunMax));
    if (stored == g_aiWritten)
        return;
    const int32_t want = ai::raised(stored, g_aiOption);
    if (want != stored)
        std::memcpy(ram + kAiRunMax, &want, 4);
    g_aiWritten = want;
    g_aiLevel = stored;
    g_aiPeak = 0;
    std::fprintf(stderr, "[engine-limits] AI run max: level %d -> %d\n", stored, want);
}

void aiLoad(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    g_aiLoadOriginal(ram, ctx, rt);
    if (ctx->pc < kAiLoad || ctx->pc >= kAiLoadEnd) // returned (not suspended inside): the level's value is stored
        applyAiRunMax(ram);
}

void aiLimiter(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc == kAiLimiter)
        applyAiRunMax(ram);
    g_aiLimiterOriginal(ram, ctx, rt);
    const int32_t inUse = static_cast<int32_t>(read32(ram, kAiInUse));
    if (inUse > g_aiPeak) {
        g_aiPeak = inUse;
        if (g_log || inUse > g_aiLevel)
            std::fprintf(stderr, "[engine-limits] AI run slots in use: %d (level max %d, run max %d)\n", inUse,
                         g_aiLevel, static_cast<int32_t>(read32(ram, kAiRunMax)));
    }
}

} // namespace

void install(PS2Runtime& rt) {
    if constexpr (!drakan_region::build().identityAddresses)
        return;
    if (g_installed.exchange(true))
        return;
    g_log = std::getenv("DRAKAN_ENGINE_LIMITS_LOG") != nullptr;
    g_shadowFactor = sd::clampFactor(
        static_cast<float>(drakan_cfg::getFloat("video", "shadow_distance", "DRAKAN_SHADOW_DISTANCE", sd::kDefault)));
    g_modelBudget = rc::budgetBytes(drakan_cfg::getInt("general", "model_cache_mb", "DRAKAN_MODEL_CACHE_MB", 0));
    g_animationBudget =
        rc::budgetBytes(drakan_cfg::getInt("general", "animation_cache_mb", "DRAKAN_ANIMATION_CACHE_MB", 0));
    g_aiOption = drakan_cfg::getInt("general", "ai_run_max", "DRAKAN_AI_RUN_MAX", ai::kDefault);
    if (g_aiOption > 0 && rt.hasFunction(kAiLoad) && rt.hasFunction(kAiLimiter)) {
        g_aiLoadOriginal = rt.lookupFunction(kAiLoad);
        rt.replaceFunction(kAiLoad, &aiLoad);
        g_aiLimiterOriginal = rt.lookupFunction(kAiLimiter);
        rt.replaceFunction(kAiLimiter, &aiLimiter);
    }
    if (rt.hasFunction(kShadowSetter)) {
        g_shadowOriginal = rt.lookupFunction(kShadowSetter);
        rt.replaceFunction(kShadowSetter, &shadowSetter);
    }
    if (rt.hasFunction(kCacheTrim)) {
        g_trimOriginal = rt.lookupFunction(kCacheTrim);
        rt.replaceFunction(kCacheTrim, &cacheTrim);
    }
    std::fprintf(
        stderr,
        "[engine-limits] installed (shadow distance x%.2f, model cache %u, animation cache %u bytes, AI run max %d)\n",
        g_shadowFactor, g_modelBudget, g_animationBudget, g_aiOption);
}

} // namespace drakan_engine_limits
