// Native texture cache budget for campaign packs (PAL). See drakan_texture_budget.h.
#include "drakan_texture_budget.h"
#include "drakan_game.h"
#include "drakan_region.h"
#include "drakan_texture_budget_policy.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_texture_budget {
namespace {
// Texture resource manager (static object, vtable 0x51d138): +4 budget, +8 bytes in use. The engine sets the budget
// itself (7 MiB at boot, 0x566666 in a level). 0x1bf3e0 is its vtable +0x54 entry, reached only when
// budget < used + request + 200: it may then only recycle an evictable texture buffer of exactly the requested size,
// among the 64 (96 for requests over 32 KiB) least recently used ones. When none fits, the texture is not loaded and
// whatever waits for it (a cut scene's start, see the header) waits until a buffer happens to free up.
constexpr uint32_t kManager = 0x4db280, kOverBudget = 0x1bf3e0;
PS2Runtime::RecompiledFunction g_original = nullptr;
const char* g_env = nullptr;
uint32_t g_lastLogged = 0;

void overBudget(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc == kOverBudget && GPR_U32(ctx, 4) == kManager) {
        const bool campaign = drakan_game::current() != drakan_game::Game::Drakan;
        uint32_t budget = 0, used = 0;
        std::memcpy(&budget, ram + kManager + 4, 4);
        std::memcpy(&used, ram + kManager + 8, 4);
        if (const uint32_t raised = raisedBudget(budget, budgetFor(campaign, g_env))) {
            std::memcpy(ram + kManager + 4, &raised, 4);
            if (budget != g_lastLogged) { // once per engine budget value (boot, then the level's)
                g_lastLogged = budget;
                std::fprintf(stderr, "[texbudget] texture cache budget %u -> %u bytes (in use %u, request %u)\n",
                             budget, raised, used, GPR_U32(ctx, 6));
            }
        }
    }
    g_original(ram, ctx, rt);
}
} // namespace

void install(PS2Runtime& rt) {
    if constexpr (!drakan_region::build().identityAddresses)
        return;
    if (!rt.hasFunction(kOverBudget))
        return;
    g_env = std::getenv("DRAKAN_TEX_CACHE_KB");
    g_original = rt.lookupFunction(kOverBudget);
    rt.replaceFunction(kOverBudget, overBudget);
}
} // namespace drakan_texture_budget
