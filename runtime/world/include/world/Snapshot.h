// Per-frame entity snapshot of the guest world: plain data, std only.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace world {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

enum class HpKind : uint8_t {
    None,       // not known for this entity
    Durability, // weapon / usable: current / max durability or quantity (dec() fields of the instance data)
    PlayerHp,   // the player: stats block HP / max
    Creature,   // NPC / creature (types 6, 7): plain ints at instance +0x24c / +0x250
};

struct EntitySnapshot {
    uint32_t handle = 0;  // guest address of the handle object
    uint32_t ref = 0;     // registry ref (slot index + 1), as stored in u16 refs elsewhere in the guest
    uint32_t classId = 0; // resource record class (0 when the record is out of range)
    uint32_t type = 0;    // object type (handle +0xc)
    uint32_t resourceIndex = 0;
    uint32_t resourceId = 0; // resource record id (record +0)
    uint32_t modelId = 0;    // handle +8
    uint32_t flags = 0;      // handle +0x9c, meaning unknown
    uint32_t refCount = 0;   // registry entry refcount
    uint8_t listMask = 0;    // bit i: present in secondary list i (Globals / ctx::kSecondaryLists)
    Vec3 position;
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // row-major 3x3 as stored
    float yaw = 0;                                   // radians, atan2(m[0][2], m[0][0]); convention not verified
    HpKind hpKind = HpKind::None;
    int32_t hp = 0, hpMax = 0;

    bool hasHp() const { return hpKind != HpKind::None; }
};

enum class ReadStatus : uint8_t {
    Ok,
    NoContext,  // engine object not set up (boot, loading)
    NoRegistry, // handle table missing
};
const char* readStatusName(ReadStatus s);

struct Snapshot {
    ReadStatus status = ReadStatus::NoContext;
    uint64_t sequence = 0;
    std::string levelName, levelPath;
    uint32_t context = 0;
    uint32_t playerHandle = 0, cameraHandle = 0;
    uint32_t registryCapacity = 0, registryHighWater = 0;
    uint32_t staleSlots = 0; // slots that hold a pointer which is not a live handle (free list links, stale)
    std::vector<EntitySnapshot> entities;

    const EntitySnapshot* find(uint32_t handleAddr) const;
    const EntitySnapshot* player() const { return find(playerHandle); }
};

inline float distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace world
