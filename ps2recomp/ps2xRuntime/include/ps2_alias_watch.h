#pragma once

// Diagnostic address alias (off unless a module sets the hook): guest addresses 0xE0000000 + a, a below the RAM size,
// take the slow memory path (KSEG3 is a special address), are reported to the hook with the access and then served
// from RAM address a. A module can hand such aliased pointers to the guest for one kind of object and so learn every
// guest routine that touches it (drakan_terrain_chunks.cpp, DRAKAN_TERRAIN_CHUNK_WATCH).

#include <cstdint>

struct R5900Context;

using Ps2AliasWatchFn = void (*)(R5900Context* ctx, uint32_t address, uint32_t bytes, bool write);

inline constexpr uint32_t kPs2AliasBase = 0xE0000000u;
inline constexpr uint32_t kPs2AliasMask = 0xF0000000u;

// The hook (nullptr = off). Set once at start-up, before the guest runs.
extern Ps2AliasWatchFn g_ps2AliasWatch;

inline bool ps2IsAlias(uint32_t address) {
    return (address & kPs2AliasMask) == kPs2AliasBase;
}
