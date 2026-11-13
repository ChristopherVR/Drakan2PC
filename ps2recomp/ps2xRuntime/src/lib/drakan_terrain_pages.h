// Terrain draw page pool ([general] terrain_page_pool, env DRAKAN_TERRAIN_PAGE_POOL, on by default): the allocations
// of the engine's terrain draw-block builder (FUN_00154060 .. FUN_00157a08) are served from guest RAM above the
// retail 32 MB instead of the game's heap, so levels with far more terrain chunks than any disc level (converted
// Order of the Flame terrain) no longer run the heap out while building the visible layers.
//
// How: the guest's malloc (0x4ac628), realloc (0x4acef0) and free (0x4acc40) are wrapped. A malloc or realloc whose
// return address lies in the builder takes a block of the pool; a free or realloc of a pool address goes back to the
// pool; everything else runs the original allocator untouched. When the pool has no room the original malloc runs as
// before. The engine still builds, budgets and evicts the same pages; only their addresses change, so nothing drawn
// changes. Rules and allocator: runtime/world/include/world/TerrainPagePool.h (world_terrain_page_pool_test).
// PAL only (the addresses are PAL values).
#pragma once

class PS2Runtime;

namespace drakan_terrain_pages {

// Installs the wrappers when the disc profile has PAL addresses and the setting is on. Call after every other hook
// of the guest free (the object motion tracker) so pool addresses never reach them. Safe to call more than once.
void install(PS2Runtime& runtime);
bool installed();

} // namespace drakan_terrain_pages
