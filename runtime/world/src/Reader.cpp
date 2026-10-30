#include "world/Reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace world {
namespace {

constexpr uint32_t kMaxSlots = 1u << 16;
constexpr uint32_t kMaxListEntries = 8192;

float readF(const IGuestMemory& m, uint32_t a) {
    const uint32_t v = m.read32(a);
    float f;
    static_assert(sizeof f == sizeof v);
    std::memcpy(&f, &v, sizeof f);
    return f;
}

bool plausibleDurability(int32_t cur, int32_t mx) {
    return (cur == -1 && mx == -1) || (cur >= 0 && cur <= mx && mx <= 100000);
}

} // namespace

const char* readStatusName(ReadStatus s) {
    switch (s) {
        case ReadStatus::Ok:
            return "Ok";
        case ReadStatus::NoContext:
            return "NoContext";
        case ReadStatus::NoRegistry:
            return "NoRegistry";
    }
    return "?";
}

const EntitySnapshot* Snapshot::find(uint32_t handleAddr) const {
    if (!handleAddr)
        return nullptr;
    for (const EntitySnapshot& e : entities)
        if (e.handle == handleAddr)
            return &e;
    return nullptr;
}

uint32_t decodeValue(uint32_t raw, uint32_t key) {
    const uint32_t x = raw ^ key;
    return (x >> 8) | (x << 24);
}

bool isLiveHandle(const IGuestMemory& m, const Globals& g, uint32_t h, uint32_t slot) {
    if (!isRamPointer(h) || (h & 3u) != 0 || !isRamPointer(h + 0x120u))
        return false;
    return m.read32(h + handle::kVtable) == g.handleVtable && m.read16(h + handle::kSelfRef) == slot + 1;
}

bool readEntity(const IGuestMemory& m, const Globals& g, uint32_t ctx, uint32_t h, EntitySnapshot& out) {
    out = EntitySnapshot{};
    out.handle = h;
    out.ref = m.read16(h + handle::kSelfRef);
    out.type = m.read32(h + handle::kType);
    out.modelId = m.read32(h + handle::kModelId);
    out.flags = m.read32(h + handle::kFlags);
    out.resourceIndex = m.read16(h + handle::kResIndex);
    out.position = {readF(m, h + handle::kPos), readF(m, h + handle::kPos + 4), readF(m, h + handle::kPos + 8)};
    for (uint32_t i = 0; i < 9; ++i)
        out.rotation[i] = readF(m, h + handle::kRot + 4 * i);
    out.yaw = std::atan2(out.rotation[2], out.rotation[0]);

    const uint32_t res = m.read32(ctx + ctx::kResourceDb);
    if (isRamPointer(res) && out.resourceIndex != 0xffffu && out.resourceIndex <= m.read32(res + resdb::kRecordCount)) {
        const uint32_t table = m.read32(res + resdb::kRecordTable);
        const uint32_t rec = table + out.resourceIndex * resdb::kRecordSize;
        if (isRamPointer(table) && isRamPointer(rec + resdb::kRecordSize)) {
            out.resourceId = m.read32(rec + resdb::kRecId);
            out.classId = m.read32(rec + resdb::kRecClass);
        }
    }

    const uint32_t key = m.read32(g.hpKey);
    if (out.type == type::kWeapon || out.type == type::kUsable) {
        const uint32_t inner = m.read32(h + handle::kInner);
        const uint32_t inst = isRamPointer(inner) ? m.read32(inner + handle::kInnerData) : 0;
        if (isRamPointer(inst) && isRamPointer(inst + 0x20)) {
            const auto cur = static_cast<int32_t>(decodeValue(m.read32(inst + handle::kInstCurrent), key));
            const auto mx = static_cast<int32_t>(decodeValue(m.read32(inst + handle::kInstMax), key));
            if (plausibleDurability(cur, mx)) {
                out.hpKind = HpKind::Durability;
                out.hp = cur;
                out.hpMax = mx;
            }
        }
    }
    return true;
}

ReadStatus readSnapshot(const IGuestMemory& m, const Globals& g, ReaderState& state, Snapshot& out) {
    out.entities.clear();
    out.levelName.clear();
    out.levelPath.clear();
    out.playerHandle = out.cameraHandle = 0;
    out.registryCapacity = out.registryHighWater = out.staleSlots = 0;
    out.sequence = ++state.sequence;

    const uint32_t ctx = m.read32(g.contextPtr);
    out.context = ctx;
    if (!isRamPointer(ctx) || !isRamPointer(ctx + 0x4c0u))
        return out.status = ReadStatus::NoContext;
    const uint32_t table = m.read32(ctx + ctx::kRegistryTable);
    const uint32_t cap = m.read32(ctx + ctx::kRegistryCapacity);
    const uint32_t high = m.read32(ctx + ctx::kRegistryHighWater);
    if (!isRamPointer(table) || cap == 0 || cap > kMaxSlots || !isRamPointer(table + cap * 8u))
        return out.status = ReadStatus::NoRegistry;
    out.registryCapacity = cap;
    out.registryHighWater = high;
    out.levelName = m.readString(ctx + ctx::kLevelName, 64);
    out.levelPath = m.readString(ctx + ctx::kLevelPath, 0xff);
    out.playerHandle = m.read32(ctx + ctx::kPlayerHandle);
    out.cameraHandle = m.read32(ctx + ctx::kCameraHandle);

    state.listBits.clear();
    for (uint32_t i = 0; i < 5; ++i) {
        const uint32_t base = ctx + ctx::kSecondaryLists[i];
        const uint32_t items = m.read32(base), count = std::min(m.read32(base + 8), kMaxListEntries);
        if (!isRamPointer(items) || !isRamPointer(items + count * 4u))
            continue;
        for (uint32_t k = 0; k < count; ++k)
            state.listBits[m.read32(items + 4 * k)] |= static_cast<uint8_t>(1u << i);
    }

    const auto slots = static_cast<uint32_t>(std::min<uint64_t>(uint64_t(high) + 1, cap));
    out.entities.reserve(slots);
    for (uint32_t i = 0; i < slots; ++i) {
        const uint32_t h = m.read32(table + i * 8u);
        if (h == 0)
            continue;
        if (!isLiveHandle(m, g, h, i)) {
            ++out.staleSlots;
            continue;
        }
        EntitySnapshot e;
        readEntity(m, g, ctx, h, e);
        e.refCount = m.read32(table + i * 8u + 4);
        if (const auto it = state.listBits.find(h); it != state.listBits.end())
            e.listMask = it->second;
        if (h == out.playerHandle && isRamPointer(g.playerStats)) {
            const uint32_t key = m.read32(g.hpKey);
            const auto hp = static_cast<int32_t>(decodeValue(m.read32(g.playerStats + stats::kHp), key));
            const auto mx = static_cast<int32_t>(decodeValue(m.read32(g.playerStats + stats::kHpMax), key));
            if (hp >= 0 && hp <= mx && mx <= 100000) {
                e.hpKind = HpKind::PlayerHp;
                e.hp = hp;
                e.hpMax = mx;
            }
        }
        out.entities.push_back(e);
    }
    return out.status = ReadStatus::Ok;
}

} // namespace world
