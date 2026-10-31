# PvP arena completion: loadout and auto-equip, reliable pickups, saving, spawn yaw (2026-10-31)

Follow-up to `mp-arenas.md` / `pvp.md`. Tags: **[game]** measured in game (PAL disc, two instances, `build/harness/<name>`),
**[dec]** from the Ghidra decompile, **[open]** not done / not verified. Reference: `docs/reference/netcode.md` "Starting loadout",
"Pickups", "Weapons and damage".

## 1. Starting loadout and auto-equip
* [dec] Inventory controller = the player **data** object (`handle +0xfc -> +0x10`) `+0x584`, 24 cells x 16 bytes at `+0x30`.
  My first attempts passed the actor handle instead of the data object and the stow crashed in `0x33ed28` (`JALR 0`). [game]
* [dec] `0x320f70(playerData, item)` stows an item (hide model + free cell); `0x341058(ctl, cell, slot, flags)` posts message `0x1045`
  whose handler calls `0x31e460(player, cell, slot, flags)`; that returns silently when `0x341858(item)` is false: a weapon's
  property `0x13` against the melee skill record (`0x3d5a30(stats, 1)+4`). A new game has skill 1.
* [game] A City new game already carries a weapon in cell 0 (`0x33734`, the skill-4 axe), so "starts empty" was wrong; it is
  just unusable (this is why the earlier posted / nested equips did nothing). The arena raises the melee skill to what the weapon
  asks for (logged), then marks the cell as the only hot slot and lets the input glue press QUICK1 (`takeQuickSlot`): the
  weapon is in the hand (`player+0xa48` holds the actor). Screenshots: `build/harness/pvp_city7/{a,b}/*_armed.png`, `pvp_s12/a/extra_30000.png`.
* [game] The factory returns the new actor's handle; the loadout item is stowed once it is registered (`0x320f70` on a level item
  that is still in the world hung the guest, on a fresh one it works). Inquisitor needs `loadout = 0x3372b` (nothing usable in a
  new game there), `0x33cbc` (dagger) cannot be spawned in City / Inquisitor.
* [game] `drakan_ghost::ready()` flaps while the peer's ghost is created; the arena ticks pause meanwhile (logged now), which is why B
  sometimes armed late.

## 2. Pickups
* [game] Root cause of "potions / bow sometimes never appear": the factory returns an object the registry never takes
  (self ref `+0x12` = 0) for some resources (potions `0x33752`..`0x33754`, bow `0x31bc9`, dagger `0x33cbc`, club `0x3217b`); weapons
  `0x3372b` `0x31f30` `0x3372a` `0x33734` and the power-ups `0x31b33` / `0x31b34` work. It is deterministic per resource, on both
  instances. The arena binds by the returned handle, gives up after 45 boundaries with a log line, and `mp_maps.ini` uses spawnable
  resources only.
* [game] A spawned item that falls to the ground used to look "taken" (moved away from its map position) and produced a false claim;
  the home position now follows it for 150 boundaries after it appears.
* [game] End to end (`pvp_city7`, 19/19 checks): A's pickup vanishes -> `claim: pickup 0` -> `server: pickup 0 ... taken by player 1`
  -> B `taken by player 1: removed here` -> both log `is back` after the respawn time. The test aid `DRAKAN_ARENA_TEST_TAKE`
  makes the item vanish near A (the walk-in touch pickup was proven earlier; adding to the inventory by the engine's routine
  on a level item hangs).

## 3. Weapons in PvP
* Melee claims already carry the engine's own contact damage (item `0x1a` x blade multiplier), and the server caps each table weapon at
  4 x its damage; blades whose holder is not found now claim as the right hand (two-handed).
* Projectiles read their own property `0x1a` (logged as `damage property`), fallback arrow 5 / spell 10. [open] not run: no bow /
  spell scenario was driven this session, so whether other arrows and spells carry a value there is unknown.

## 4. Saving, yaw, falling
* [game] F5 / F8 in an arena: `quick save ignored: multiplayer arena` / `quick load ignored` (`pvp_city7`). The writer veto itself is
  still never reached because the glue stops the request first.
* [game] Spawn heading: placement writes the rotation rows; the spawn faces the arena middle unless its `yaw` is set (`[nettrace-local]`
  yaw 1.571 -> -1.423 and the player moves along it).
* [open] The fall-out-of-bounds respawn did not trigger in these runs (nobody fell); the code path is the old one.

## 5. Fights
City (`pvp_city7`, all 19 checks), Surdana Meadow (`pvp_sur*`), Inquisitor (`pvp_inq2`: loadout, equip, quick save pass; the two
instances run at about 4 fps there on this PC, so the fight and the pickup test did not finish; see the final report).

## 6. USA
Documented in `docs/reference/regions.md` "Multiplayer arenas on USA": audited address entries and typed layouts are missing, nothing
could be verified without a USA disc.

## Process
`drakan_ghost.cpp` of another session was broken twice by `\n` turned into real newlines; fixed in place (three string literals).
