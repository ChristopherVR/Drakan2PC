// Arokh's breath while Rynn rides him (journal docs/journal/2026-11-16/breath-old-saves.md). PAL only.
//
// 1. The rider's stale attack state. Game_OnInputCommand (0x23ce18) hands the attack command 0x4006 to the mounted
//    dragon's controller (slot +0x164, which sets bit 1 of his +0x90, the breath) only while the rider's +0x94 has
//    neither 0x400000 nor 0x200000. Rynn sets 0x400000 when an attack starts (0x317c78) and her controller update
//    clears it when the swing ends (0x318348). If she mounts with a weapon equipped (the Ruined Village awakening
//    scene sets it as she climbs on), her controller stops running while she rides, the bit stays set and is saved,
//    and the breath key does nothing for the rest of the campaign. While the controlled actor is the dragon and his
//    rider (dragon +0x4d8, a registry reference) is mounted (+0x94 bit 0), the bit is cleared. Both games.
// 2. A breathless Order of the Flame dragon. A dragon gets his breath from the Dragon Power-ups linked to him: each
//    adds an entry (weapon, alternate weapon) to his list at +0x6a8 (count +0x6b0, capacity +0x6ac, chosen +0x6a4),
//    message 0x1028 of his handler 0x27d0e0. Saves of an older OotF pack (whose Fireball Power Up had no breath) hold
//    a dragon that took his power-up while it was empty: the list was never made, and the power-up is gone. While an
//    OotF dragon is controlled outside a multiplayer arena with that list never made (count 0, no buffer), the
//    handler is called once with the converter's fire stand-in ("!! FireBall 3 !!" / "!! FlameBreath !!", the Drakan 2
//    Fireball Power Up's fields, ootfbreath.ts). A dragon that took a power-up normally always has an entry.
// DRAKAN_DRAGON_BREATH=0 turns both off; DRAKAN_DRAGON_BREATH_LOG=0 silences the `[dragon-breath]` lines.
#pragma once

#include <cstdint>

class PS2Runtime;
namespace drakan_input {
class IGuest;
}

namespace drakan_dragon_breath {

void install(PS2Runtime& rt);
// Every PadMgr update on the EE thread; `gameplay` = the world is running (no menu or inventory screen).
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram, bool gameplay);

} // namespace drakan_dragon_breath
