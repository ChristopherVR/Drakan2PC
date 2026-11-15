#pragma once
class PS2Runtime;
namespace drakan_ponytail {
// The engine's vertex cloth (Rynn's ponytail, 0x11d368): see drakan_ponytail_policy.h and
// docs/journal/2026-11-16/ponytail-lift.md. DRAKAN_PONYTAIL=0 runs the engine's step unchanged;
// DRAKAN_PONYTAIL_LOG=1 logs one line per 200 cloth steps (2 = every step, bounded).
void install(PS2Runtime& runtime);
} // namespace drakan_ponytail
