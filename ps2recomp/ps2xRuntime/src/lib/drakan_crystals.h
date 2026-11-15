// Drakan: Order of the Flame Magic Crystals (journal docs/journal/2026-11-16/ootf-fire-crystal.md).
//
// A converted Fire / Ice / Lightning Crystal is a clone of the mana power-up (class 0x100ab): used from the inventory
// it only refilled the mana, which an OotF campaign neither shows nor uses. The pack's `gameplay/crystals.ini`
// (drakan_crystals_policy.h) names the effect each crystal casts on the PC; this module hooks the power-up's message
// handler 392aa0 and, on the use message (0x10cb) of a listed crystal, spawns that effect at Rynn through the engine's
// public spawn 0x181ad8 with Rynn as the owner, once the game is back in play. The native use is untouched (it still
// consumes one charge). PAL only. DRAKAN_CRYSTALS=0 turns it off.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class PS2Runtime;
namespace drakan_input {
class IGuest;
}

namespace drakan_crystals {

// The `gameplay/crystals.ini` texts of the enabled packs (ModManager::crystalFiles), in load order.
void setTables(const std::vector<std::pair<std::string, std::string>>& files);
void install(PS2Runtime& rt);
// Every PadMgr update on the EE thread; `gameplay` = the world is running (no menu or inventory screen).
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram, bool gameplay);

} // namespace drakan_crystals
