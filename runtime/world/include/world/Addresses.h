// Guest layout facts of the actor registry (PAL, SCES-500.06). Confidence per fact is in docs/reference/world-model.md.
// Offsets inside objects are region independent; the four absolute globals go through Globals so the adapter can
// translate them with drakan_region::guestAddress(). std only.
#pragma once
#include <cstdint>

namespace world {

// Plausible heap / data pointer, the same window gameui uses.
inline constexpr uint32_t kRamPtrMin = 0x100000;
inline constexpr uint32_t kRamPtrEnd = 0x2000000;
inline bool isRamPointer(uint32_t p) {
    return p >= kRamPtrMin && p < kRamPtrEnd;
}

// Absolute guest addresses (PAL values are the defaults).
struct Globals {
    uint32_t contextPtr = 0x5565d4;   // word holding the engine object (0x4de5e0 on PAL)
    uint32_t hpKey = 0x4fe86c;        // key word of the dec()/enc() obfuscation
    uint32_t playerStats = 0x4fbac8;  // player stats block S (HP, mana, gold ...)
    uint32_t handleVtable = 0x51b8b0; // vtable every registered handle starts with
};

// ctx = *(Globals::contextPtr): the engine object.
namespace ctx {
inline constexpr uint32_t kRegistryTable = 0x7c;     // ptr to the handle table, 8-byte entries {handle*, refcount}
inline constexpr uint32_t kRegistryCapacity = 0x80;  // slots allocated
inline constexpr uint32_t kRegistryHighWater = 0x84; // highest slot index in use (ref valid when ref <= this + 1)
inline constexpr uint32_t kLevelPath = 0x98;         // NUL-terminated disc path of the loaded .lvl (64 bytes)
inline constexpr uint32_t kLevelName = 0x298;        // NUL-terminated display name (e.g. "Surdana")
inline constexpr uint32_t kResourceDb = 0x320;       // resource db object
inline constexpr uint32_t kPlayerHandle = 0x4b0;
inline constexpr uint32_t kCameraHandle = 0x4b4;
// Five secondary handle-pointer arrays {u32* items, u32 capacity, u32 count}; semantics unknown (membership bits).
inline constexpr uint32_t kSecondaryLists[5] = {0x44c, 0x458, 0x464, 0x470, 0x488};
inline constexpr uint32_t kLevelPathBytes = 0x100;
} // namespace ctx

// Resource db: 0x24-byte records.
namespace resdb {
inline constexpr uint32_t kRecordTable = 0x8;
inline constexpr uint32_t kRecordCount = 0x10; // inclusive limit: valid index <= count
inline constexpr uint32_t kRecordSize = 0x24;
inline constexpr uint32_t kRecId = 0x0;    // resource id (e.g. 0x20a50 = Rynn)
inline constexpr uint32_t kRecType = 0x4;  // == handle.type
inline constexpr uint32_t kRecClass = 0x8; // class id (0x10009 Rynn, 0x1001c Arokh, 0x1001b camera)
inline constexpr uint32_t kRecProps = 0xc; // property block (usually == handle.props)
inline constexpr uint32_t kRecModel = 0x10;
} // namespace resdb

// Registered handle (about 0x120 bytes).
namespace handle {
inline constexpr uint32_t kVtable = 0x0;
inline constexpr uint32_t kRefTag = 0x4;    // packed word, meaning unknown
inline constexpr uint32_t kModelId = 0x8;   // model / mesh resource id (== record+0x10 for ~95% of handles)
inline constexpr uint32_t kType = 0xc;      // object type (== record type)
inline constexpr uint32_t kResIndex = 0x10; // u16 resource record index; 0xffff = none
inline constexpr uint32_t kSelfRef = 0x12;  // u16 own registry ref (= slot index + 1)
inline constexpr uint32_t kPos = 0x14;      // float x, y, z
inline constexpr uint32_t kRot = 0x20;      // float[3][3]
inline constexpr uint32_t kProps = 0x54;    // property block (re-read, it moves)
inline constexpr uint32_t kFlags = 0x9c;    // state word, meaning unknown
inline constexpr uint32_t kInner = 0xfc;    // inner object; *(inner+0x10) = instance data
inline constexpr uint32_t kInnerData = 0x10;
inline constexpr uint32_t kInstCurrent = 0x14; // dec(): durability / quantity of items (types 10, 11)
inline constexpr uint32_t kInstMax = 0x18;     // dec()
} // namespace handle

// Player stats block S (dec() fields).
namespace stats {
inline constexpr uint32_t kHp = 0xc;
inline constexpr uint32_t kHpMax = 0x10;
inline constexpr uint32_t kMana = 0x1c;
inline constexpr uint32_t kManaMax = 0x20;
} // namespace stats

// Object types as observed (h+0xc). Names are descriptive guesses, see the reference doc.
namespace type {
inline constexpr uint32_t kPlayer = 3;
inline constexpr uint32_t kCamera = 4; // also the many class 0x10070 / 0x1003d ... world actors
inline constexpr uint32_t kWeapon = 10;
inline constexpr uint32_t kUsable = 11;
} // namespace type

// Classes.
inline constexpr uint32_t kClassRynn = 0x10009;
inline constexpr uint32_t kClassCamera = 0x1001b;
inline constexpr uint32_t kClassDragon = 0x1001c;

} // namespace world
