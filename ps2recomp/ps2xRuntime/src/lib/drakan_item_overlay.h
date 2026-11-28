// Depth clear under the native 3D item overlays (PAL; GitHub issue 22). Math and evidence:
// drakan_item_overlay_policy.h, docs/journal/2026-11-28/issue-22-quick-slot-depth.md.
//
// The Quick Inventory bar (and the other callers of the item overlay draw 0x3aff40) clears the world depth only under
// the item handle's own bounding box before it draws the item and its part objects over the world. A wrapper on the
// clear (0x1342e0, called from 0x3aff40 with return address 0x3b0784) widens that rectangle by the parts' boxes, so a
// part such as the club's head is no longer hidden behind nearer level geometry. The clear stays the game's own GS
// packet, in its place in the draw order; nothing else changes.
// DRAKAN_ITEM_OVERLAY_DEPTH=0 keeps the original rectangle; DRAKAN_ITEM_OVERLAY_DEPTH_LOG=1 logs widened rectangles.
#pragma once

class PS2Runtime;

namespace drakan_item_overlay {

// Installs the wrapper when the disc profile has PAL addresses and the function exists. Safe to call more than once.
void install(PS2Runtime& runtime);

} // namespace drakan_item_overlay
