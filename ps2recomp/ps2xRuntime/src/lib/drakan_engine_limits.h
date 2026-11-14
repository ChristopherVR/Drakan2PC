// PS2-era engine budgets the PC lifts (PAL). Rules: runtime/world/include/world/EngineLimits.h.
//
//   * [video] shadow_distance (env DRAKAN_SHADOW_DISTANCE), 1 .. 4, default 3, 1 = original: the projected character
//     shadows start to fade and end that many times further from the camera. 0x1431e8 stores the level's three squared
//     shadow distances (0x4e0adc start, 0x4e0ad4 end, 0x4e0ad8 cutoff); the wrapper runs it and multiplies the three by
//     the factor squared. Nothing else reads them. Applied at the next level load.
//   * [general] model_cache_mb / animation_cache_mb (env DRAKAN_MODEL_CACHE_MB / DRAKAN_ANIMATION_CACHE_MB), 0 = the
//     level's own budget (5.4 MiB / 2 MiB on the disc): the budget of the engine's model cache (0x4dae20) and animation
//     cache (0x4db050). 0x12c850 drops least recently used unreferenced entries until the cache is under its budget;
//     the wrapper raises the budget word (+4) before it runs and counts what it drops (bytes in use at +8). The level
//     sets its budget again at every load, so the raise is applied at each call.
//   * [general] ai_run_max (env DRAKAN_AI_RUN_MAX), default 8, 0 = the level's own ("AI Run Max", 4 on the disc): how
//     many creatures under the AI limiter run their AI at once (a farther one waits for a free slot). 0x2dc718 stores
//     the level's value at 0x5130bc when a level loads; the wrappers of it and of the limiter 0x48a4a0 raise it, never
//     below the level's value, and log the peak number of slots in use above the level's value. 0 installs nothing.
// DRAKAN_ENGINE_LIMITS_LOG=1 logs every call of the two wrappers; without it the cache wrapper logs each further MiB
// that a cache drops.
#pragma once

class PS2Runtime;

namespace drakan_engine_limits {

// Installs the wrappers when the disc profile has PAL addresses and the functions exist. Safe to call more than once.
void install(PS2Runtime& runtime);

} // namespace drakan_engine_limits
