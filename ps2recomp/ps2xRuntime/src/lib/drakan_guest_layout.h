// Named guest (PS2 game) addresses and struct offsets that the Drakan glue uses in several files. PAL (SCES-500.06)
// values; code that must run on other regions passes the absolute ones through drakan_region::guestAddress()
// (docs/reference/regions.md), offsets inside objects are region independent.
//
// Only facts with a clear meaning are named here; the evidence is in the docs/reference page next to each group.
// File-local addresses stay in their file.
#pragma once

#include <cstdint>

#include "world/Addresses.h"

namespace drakan_layout {

// ---- global data (absolute addresses)

// Word holding the engine object ("ctx", world-model.md); the offsets in `engine` below are inside it.
inline constexpr uint32_t kEngineContextPtr = 0x5565d4u;
static_assert(kEngineContextPtr == world::Globals{}.contextPtr);
// Word holding the render / viewport object: GUI coordinate space at +0xf8 / +0xfc (512 x 512), view scale at +0x3c
// (fmv.md, input.md, graphics.md).
inline constexpr uint32_t kRenderViewPtr = 0x5565c8u;
// Player stats block (HP, mana, gold, ...), the HP words obfuscated with kHpKey (netcode.md).
inline constexpr uint32_t kPlayerStats = 0x4fbac8u;
static_assert(kPlayerStats == world::Globals{}.playerStats);
// Key word of the stats obfuscation: a stored value is rotl8(value) ^ key.
inline constexpr uint32_t kHpKey = 0x4fe86cu;
static_assert(kHpKey == world::Globals{}.hpKey);
// Vtable every registered actor handle starts with (world-model.md).
inline constexpr uint32_t kHandleVtable = 0x51b8b0u;
static_assert(kHandleVtable == world::Globals{}.handleVtable);
// Active GUI screen pointer, 0 = no screen; a screen's id is at +0x18 (mouse-menus.md).
inline constexpr uint32_t kActiveScreenPtr = 0x4fb60cu;
// Native options object (input.md 3.2).
inline constexpr uint32_t kOptions = 0x4fb6d0u;
// World view struct: the 2x3 view template the camera copies, camera position at +0x6c, the view layer the camera is
// in at +0x8c (widescreen.md, graphics.md, level-format.md).
inline constexpr uint32_t kWorldView = 0x4dad58u;
inline constexpr uint32_t kWorldViewCameraPos = 0x6cu; // float x, y, z
inline constexpr uint32_t kWorldViewLayer = 0x8cu;     // layer index, -1 for none
// Resident model and texture managers (resource lists walked by the scene / foliage / detail code).
inline constexpr uint32_t kModelManager = 0x4dae20u;
inline constexpr uint32_t kTextureManager = 0x4db280u;
// World tick length in seconds (float); its reciprocal follows at +4.
inline constexpr uint32_t kWorldDt = 0x4de604u;

// ---- offsets inside the engine object (*kEngineContextPtr)
namespace engine {
inline constexpr uint32_t kResourceDb = world::ctx::kResourceDb;        // 0x320, resource db object
inline constexpr uint32_t kControlledActor = world::ctx::kPlayerHandle; // 0x4b0, handle the player controls
inline constexpr uint32_t kCameraActor = world::ctx::kCameraHandle;     // 0x4b4, camera handle
} // namespace engine

// ---- guest functions (entry PCs)
namespace fn {
// PadMgr update: runs once per frame before the actor lists are walked; the glue's per-frame hook point.
inline constexpr uint32_t kPadMgrUpdate = 0x1e3970u;
// Actor / item factory: spawns a resource (modding.md, netcode.md).
inline constexpr uint32_t kSpawnActor = 0x181ad8u;
// (engine, actor): queued kill, flushed in the frame tail (engine vtable slot 0x51a884, netcode.md).
inline constexpr uint32_t kKillActor = 0x1413c0u;
// Guest heap: malloc(a0 = size), free(a0 = pointer), realloc(a0 = pointer, a1 = size).
inline constexpr uint32_t kMalloc = 0x4ac628u;
inline constexpr uint32_t kFree = 0x4acc40u;
inline constexpr uint32_t kRealloc = 0x4acef0u;
// Per-object render method, a0 = the registered handle.
inline constexpr uint32_t kRenderObject = 0x189b38u;
} // namespace fn

} // namespace drakan_layout
