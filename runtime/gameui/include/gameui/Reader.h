// Builds a Snapshot from guest memory. Pure reads through IGuestMemory; addresses in Addresses.h.
// Call it on the EE thread (at screen 0x15 message 10 or after the pad update), then publish the snapshot.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gameui/GuestMemory.h"
#include "gameui/Model.h"

namespace gameui {

// dec(x) = rotr32(x ^ key, 8); enc(v) = rotl32(v, 8) ^ key.
inline uint32_t decodeValue(uint32_t raw, uint32_t key) {
    const uint32_t x = raw ^ key;
    return (x >> 8) | (x << 24);
}
inline uint32_t encodeValue(uint32_t value, uint32_t key) {
    return ((value << 8) | (value >> 24)) ^ key;
}

bool isRamPointer(uint32_t p);
// Item handle check: > 0x53 (not a continuation marker), inside RAM, resource index != 0xffff.
bool isItemHandle(const IGuestMemory& m, uint32_t h);

// State kept across reads (owned by the game side, one per reader).
struct ReaderState {
    uint64_t sequence = 0;
    // Native details lines by item handle / spell index, cached whenever that item / spell is the native selection.
    std::unordered_map<uint32_t, std::vector<std::string>> itemDetails;
    std::unordered_map<int, std::vector<std::string>> spellDetails;
};

enum class ReadStatus {
    Ok,
    NoContext, // ctx pointer not set up (boot, loading)
    NoPlayer,  // no player data object
    Dragon,    // riding Arokh: the stats are read, the inventory is not
};
const char* readStatusName(ReadStatus s);

// Fills `out` completely (it is reused, previous contents are replaced). `out.native.valid` is true for Ok only.
ReadStatus readSnapshot(const IGuestMemory& m, ReaderState& state, Snapshot& out);

// ---- the pieces (exposed for tests and for the glue's own guards) ----
struct GuestRoots {
    uint32_t ctx = 0, res = 0, strings = 0, key = 0;
    uint32_t player = 0, ctl = 0;
    uint32_t topScreen = 0, topScreenId = 0;
};
// ctx / res / string manager / key, and the player from the top screen (when it is 0x15) or ctx+0x4b0.
ReadStatus findRoots(const IGuestMemory& m, GuestRoots& r);

// Resident string `id` from the string cache (no load is ever requested). nullopt when not resident.
std::optional<std::string> lookupString(const IGuestMemory& m, uint32_t stringMgr, uint32_t id);
// Resource record class / property block of a resource index; 0 when out of range.
uint32_t resourceClass(const IGuestMemory& m, uint32_t res, uint32_t index);
uint32_t resourceProps(const IGuestMemory& m, uint32_t res, uint32_t index);
// prop(id) and prop(id, k) of a property block.
uint32_t prop(const IGuestMemory& m, uint32_t block, uint32_t id);
uint32_t propIndexed(const IGuestMemory& m, uint32_t block, uint32_t id, uint32_t k);

void readStats(const IGuestMemory& m, uint32_t key, CharacterStats& out);
void readSkills(const IGuestMemory& m, uint32_t key, SkillSheet& out);
// Grid items (records 0..23) with footprints, cellItem, and the shop stock record.
void readItems(const IGuestMemory& m, const GuestRoots& r, Snapshot& out);
void readSpells(const IGuestMemory& m, const GuestRoots& r, SpellBook& out);
void readQuests(const IGuestMemory& m, const GuestRoots& r, std::vector<QuestEntry>& out);
void readRegions(const IGuestMemory& m, std::vector<MapRegion>& out);
// The nine details buffers in order, blank ones dropped (the native screen pads with "" and " ").
std::vector<std::string> readDetails(const IGuestMemory& m, uint32_t ctl);

} // namespace gameui
