// Native texture cache budget for campaign packs and pack levels (PAL). See drakan_texture_budget.h.
#include "drakan_texture_budget.h"
#include "drakan_config.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "drakan_texture_budget_policy.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace drakan_texture_budget {
namespace {
// Texture resource manager (static object, vtable 0x51d138): +4 budget, +8 bytes in use. The engine sets the budget
// itself (7 MiB at boot, 0x566666 in a level). 0x1bf3e0 is its vtable +0x54 entry, reached only when
// budget < used + request + 200: it may then only recycle an evictable texture buffer of exactly the requested size,
// among the 64 (96 for requests over 32 KiB) least recently used ones. When none fits, the texture is not loaded and
// whatever waits for it (a cut scene's start, see the header) waits until a buffer happens to free up.
constexpr uint32_t kManager = 0x4db280, kOverBudget = 0x1bf3e0;
PS2Runtime::RecompiledFunction g_original = nullptr;
std::string g_setting;
uint32_t g_lastLogged = 0;
std::mutex g_levelsMutex;
std::set<std::string> g_packLevels;
std::atomic<bool> g_packLevel{false};
// Over-budget calls since the last level load: each one is a texture the cache could only take by recycling a buffer
// of the same size (or not at all), i.e. a texture that leaves memory and is read and uploaded again later.
uint64_t g_overBudgetCalls = 0, g_overBudgetRaised = 0;
std::string g_level;

bool isPackLevel(const std::string& name) {
    std::lock_guard<std::mutex> l(g_levelsMutex);
    for (const std::string& p : g_packLevels)
        if (drakan_cfg::compareNoCase(p.c_str(), name.c_str()) == 0)
            return true;
    return false;
}

void overBudget(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc == kOverBudget && GPR_U32(ctx, 4) == kManager) {
        const bool raise = drakan_game::current() != drakan_game::Game::Drakan || g_packLevel.load();
        uint32_t budget = 0, used = 0;
        std::memcpy(&budget, ram + kManager + 4, 4);
        std::memcpy(&used, ram + kManager + 8, 4);
        ++g_overBudgetCalls;
        if (const uint32_t raised = raisedBudget(budget, budgetFor(raise, g_setting.c_str()))) {
            std::memcpy(ram + kManager + 4, &raised, 4);
            ++g_overBudgetRaised;
            if (budget != g_lastLogged) { // once per engine budget value (boot, then the level's)
                g_lastLogged = budget;
                std::fprintf(stderr, "[texbudget] texture cache budget %u -> %u bytes (in use %u, request %u)\n",
                             budget, raised, used, GPR_U32(ctx, 6));
            }
            // With the raised budget the request may fit now: the native call then returns without recycling.
        }
    }
    g_original(ram, ctx, rt);
}

void beforeLevelLoad(const drakan_lifecycle::Event& e) {
    if (!g_level.empty() || g_overBudgetCalls)
        std::fprintf(stderr, "[texbudget] level '%s': %llu over-budget texture loads (%llu after a raise)\n",
                     g_level.c_str(), (unsigned long long)g_overBudgetCalls, (unsigned long long)g_overBudgetRaised);
    g_overBudgetCalls = g_overBudgetRaised = 0;
    g_level = e.level;
    const bool pack = packLevel(
        e.level.c_str(), [](const char* n) { return isPackLevel(n); }, std::getenv("DRAKAN_START_LEVEL"),
        std::getenv("DRAKAN_START_LEVEL_FROM"));
    g_packLevel.store(pack);
    const uint32_t target = budgetFor(pack || drakan_game::current() != drakan_game::Game::Drakan, g_setting.c_str());
    std::fprintf(stderr, "[texbudget] level '%s'%s: texture cache budget %s\n", e.level.c_str(),
                 pack ? " (from a pack)" : "", target ? (std::to_string(target / 1024) + " KiB").c_str() : "native");
}
} // namespace

void setPackLevels(const std::vector<std::string>& levels) {
    std::lock_guard<std::mutex> l(g_levelsMutex);
    g_packLevels.clear();
    g_packLevels.insert(levels.begin(), levels.end());
}

void install(PS2Runtime& rt) {
    if constexpr (!drakan_region::build().identityAddresses)
        return;
    if (!rt.hasFunction(kOverBudget))
        return;
    g_setting = drakan_cfg::getString("video", "game_texture_cache_kb", "DRAKAN_TEX_CACHE_KB", "auto");
    g_original = rt.lookupFunction(kOverBudget);
    rt.replaceFunction(kOverBudget, overBudget);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, beforeLevelLoad);
}
} // namespace drakan_texture_budget
