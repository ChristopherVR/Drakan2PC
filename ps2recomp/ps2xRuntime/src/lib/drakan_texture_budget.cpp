// Native texture cache budget for campaign packs and pack levels (PAL). See drakan_texture_budget.h.
#include "drakan_texture_budget.h"
#include "drakan_config.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "drakan_texture_budget_policy.h"
#include "drakan_view_distance.h"
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
// itself (7 MiB at boot, 0x566666 in a level). 0x1bf3e0 is its vtable +0x54 entry. When budget < used + request + 200
// it may only recycle an evictable texture buffer of exactly the requested size, among the 64 (96 for requests over
// 32 KiB) least recently used ones. When none fits, the texture is not loaded and whatever waits for it (a cut scene's
// start, see the header) waits until a buffer happens to free up. It is also called when the request fits (seen with
// 0 bytes in use at boot), so the counters below test the condition themselves.
constexpr uint32_t kManager = 0x4db280, kOverBudget = 0x1bf3e0;
PS2Runtime::RecompiledFunction g_original = nullptr;
std::string g_setting;
uint32_t g_lastLogged = 0;
std::mutex g_levelsMutex;
std::set<std::string> g_packLevels;
std::atomic<bool> g_packLevel{false};
// Since the last level load: calls of 0x1bf3e0, and the calls whose request did not fit the budget in force (after any
// raise): each of those is a texture the cache could only take by recycling a buffer of the same size, or not at all.
uint64_t g_calls = 0, g_full = 0;
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
        const uint32_t request = GPR_U32(ctx, 6);
        uint32_t inForce = budget;
        ++g_calls;
        if (const uint32_t raised =
                raisedBudget(budget, budgetFor(raise, g_setting.c_str(), drakan_view_distance::sceneryWidens()))) {
            std::memcpy(ram + kManager + 4, &raised, 4);
            inForce = raised;
            if (budget != g_lastLogged) { // once per engine budget value (boot, then the level's)
                g_lastLogged = budget;
                std::fprintf(stderr, "[texbudget] texture cache budget %u -> %u bytes (in use %u, request %u)\n",
                             budget, raised, used, request);
            }
            // With the raised budget the request may fit now: the native call then returns without recycling.
        }
        if (uint64_t(inForce) < uint64_t(used) + request + 200) {
            ++g_full;
            if (g_full == 1 || g_full == 10 || g_full == 100 || g_full % 1000 == 0)
                std::fprintf(stderr,
                             "[texbudget] level '%s': %llu texture loads did not fit so far (budget %u, in use %u)\n",
                             g_level.c_str(), (unsigned long long)g_full, inForce, used);
        }
    }
    g_original(ram, ctx, rt);
}

void beforeLevelLoad(const drakan_lifecycle::Event& e) {
    if (!g_level.empty() || g_calls)
        std::fprintf(stderr, "[texbudget] level '%s': %llu texture loads did not fit the budget (%llu calls)\n",
                     g_level.c_str(), (unsigned long long)g_full, (unsigned long long)g_calls);
    g_calls = g_full = 0;
    g_level = e.level;
    const bool pack = packLevel(
        e.level.c_str(), [](const char* n) { return isPackLevel(n); }, std::getenv("DRAKAN_START_LEVEL"),
        std::getenv("DRAKAN_START_LEVEL_FROM"));
    g_packLevel.store(pack);
    const uint32_t target = budgetFor(pack || drakan_game::current() != drakan_game::Game::Drakan, g_setting.c_str(),
                                      drakan_view_distance::sceneryWidens());
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
    std::atexit([] {
        std::fprintf(stderr, "[texbudget] exit, level '%s': %llu texture loads did not fit the budget (%llu calls)\n",
                     g_level.c_str(), (unsigned long long)g_full, (unsigned long long)g_calls);
    });
}
} // namespace drakan_texture_budget
