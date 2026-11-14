// Wide terrain chunks ([general] terrain_wide_chunks, env DRAKAN_TERRAIN_WIDE_CHUNKS): terrain chunk records with up
// to 32 texture references instead of the disc engine's 9, so a converted Order of the Flame layer, whose ground
// changes texture almost every cell, keeps disc-sized chunks instead of being cut into single cells.
//
// How: the recompiler emits a copy of every guest function that uses the 0x4c-byte chunk record (and of the terrain
// mesh drawer) with a wider record and a larger group flag array (configs/drakan.toml [[variants]]
// "wide_terrain_chunks"); this module swaps those copies into the function table at start-up, all of them or none.
// Layouts and limits: runtime/world/include/world/TerrainChunks.h. Off: the original functions run untouched.
// A pack converted with wide chunks says so in its mod.toml (`terrain_chunk_textures = 32`); when mods.ini enables
// such a pack the wide layout is used whatever the setting says, because the disc functions would hang on its records.
//
// DRAKAN_TERRAIN_CHUNK_WATCH=1 (diagnosis): the chunk record vectors are handed to the guest through an aliased
// address (ps2_alias_watch.h), and every guest routine that reads or writes a record is logged with the record field
// offsets it touched ("[chunk-watch]" lines, every 10 s and at exit). PAL only.
#pragma once

class PS2Runtime;

namespace drakan_terrain_chunks {

// Installs the variants when the setting is on (and the watch when asked). Call before any other module replaces one
// of the functions (the terrain distance hook wraps the drawer). Safe to call more than once.
void install(PS2Runtime& runtime);
// True when the wide record layout is in use.
bool wide();

} // namespace drakan_terrain_chunks
