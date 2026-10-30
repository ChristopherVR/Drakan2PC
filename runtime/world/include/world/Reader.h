// Reads the guest's actor registry into a Snapshot. Pure reads through IGuestMemory, no guest calls, every pointer
// bounds checked (a torn read while the guest runs yields at worst a skipped entity). EE thread, once per frame.
#pragma once
#include <cstdint>
#include <unordered_map>

#include "gameui/GuestMemory.h"
#include "world/Addresses.h"
#include "world/Snapshot.h"

namespace world {

using gameui::FlatGuestMemory;
using gameui::IGuestMemory;

// Scratch kept between reads so the steady state does not allocate.
struct ReaderState {
    uint64_t sequence = 0;
    std::unordered_map<uint32_t, uint8_t> listBits;
};

// dec(x) = rotr32(x ^ key, 8) (the game's obfuscated counters).
uint32_t decodeValue(uint32_t raw, uint32_t key);

// Fills `out` completely (entities are replaced, capacity reused).
ReadStatus readSnapshot(const IGuestMemory& m, const Globals& g, ReaderState& state, Snapshot& out);

// Pieces, exposed for tests and for the adapter's guards.
// Returns false unless `h` is a live registered handle: RAM pointer, handle vtable, own ref == slot + 1.
bool isLiveHandle(const IGuestMemory& m, const Globals& g, uint32_t h, uint32_t slot);
// Fills everything but listMask / refCount from the handle at `h`; `ctx` gives the resource db.
bool readEntity(const IGuestMemory& m, const Globals& g, uint32_t ctx, uint32_t h, EntitySnapshot& out);

} // namespace world
