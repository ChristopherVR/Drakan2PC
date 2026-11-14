// Extended guest heap: the game's heap may grow past the retail 32 MB (PAL only; the addresses are PAL values).
//
// * [general] heap_extend (env DRAKAN_HEAP_EXTEND): the guest's sbrk (0x4bc7b8) is answered by the runtime. Below the
//   retail heap end (EndOfHeap, 0x1f00000) it behaves as before; a request past it continues the heap at 0x2000000,
//   above the stack and the RPC pool, up to 0x2000000 + [general] extended_heap_mb (default and most 224 MB: guest RAM
//   ends at 0x10000000, ps2_memory.h). The game's allocator (newlib's dlmalloc) takes a discontiguous sbrk answer as
//   designed: it fences the old top chunk and carries on from the new range, so every malloc, realloc, memalign and
//   free of the game keeps working on its own data structures, only on more memory. sbrk(negative), the allocator's
//   trim, gives host pages back. The heap probe that runs at start (0x4ac1f0, mallocs of 32 MB expected to fail) keeps
//   seeing the retail heap. Host pages are committed only as far as the heap reaches (runtime/ps2_guest_ram.h).
// * [general] terrain_page_pool (env DRAKAN_TERRAIN_PAGE_POOL), used when heap_extend is off: the allocations of the
//   terrain draw-block builder come from a pool over the same range instead (the 14 November fix,
//   docs/journal/2026-11-14/ootf-terrain-heap.md); with heap_extend on, the game heap itself has the room.
// Rules and allocator: runtime/world/include/world/ExtendedHeap.h (world_extended_heap_test).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_extended_heap {

// Installs the hooks when the disc profile has PAL addresses and a setting is on. Call after every other hook of the
// guest free (the object motion tracker). Safe to call more than once.
void install(PS2Runtime& runtime);
bool installed();
// True when the settings and the disc let the game heap grow past the retail RAM (what install will do); readable
// before install (the engine cache budgets follow it).
bool heapWillExtend();

} // namespace drakan_extended_heap
