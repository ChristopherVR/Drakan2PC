# Co-op characters and dragons - 2026-10-31

Design (the user's words): the host plays Rynn; each joining co-op player picks another character; every player needs their
own dragon for the dragon sections. Code: `ps2xRuntime/src/lib/drakan_coop_character.{h,cpp,table.inc}` (roster, clip
mapping, selection), hooks in `drakan_ghost.{h,cpp}` (stand-in and dragon bodies), `drakan_netplay.cpp` (character on the
wire, respawn on change), protocol v7 (`PlayerState.character`). Table generator:
`analysis/scripts/coop_character_roster.py` (reads the player's ISO; ids, names and clip durations only; CSV copy of the
roster in `analysis/coop_characters.csv`). Evidence tags as in `world-model.md`: **[disc]** from the ISO data, **[dec]** from
the decompile, **[run]** observed in game, **[inf]**, **[?]**.

## 1. Which characters can stand in for a player

**No other model shares Rynn's skeleton [disc].** The 0x0208 character record of every model lists its node names
(`chnr_upperbody`, `torso`, `Lhand`, ...). Rynn's model (3877, SYSTEM.MOD) has 578 names (hair, eyebrows, eyelids, jaw,
five fingers per hand ...). Compared with all 242 class models of SYSTEM and NPCS.ODB the best overlap is 25 percent
(Desert Female, Villager Female); the humanoid NPC bodies have 320 to 700 names and different channel numbering (Rynn: upper
body node 2, neck 4, head 5; Villager: upper body node 1, head 5, neck 4, jaw 6, weapon 30). Animation key data is per
node, so Rynn's clips cannot be written onto another model (option (a) of the task is out). The only 100 percent matches are
Rynn's own helper classes (Prefetcher, Level Trans Teleporter, same model).

**Option (b) is what works: the partner's body is an NPC-class actor and plays the NPC's own clips.** The humanoid class
is `0x1009d` (the engine's "NPC" behaviour, 80 classes in NPCS.ODB: villagers, sailors, northmen, guards, the Mage, the
Monk, Ranwulf, the Captain ...). Their class fields hold per-class clip lists, exactly like Rynn's: `Run`, `Walk`, `Base Idle`,
`Turn Left/Right`, `Attack Animations`, `Hit Rxn ... anim`, `Death`, `Taunt`, `Cower`. 46 of the 80 have idle, walk, run,
attack and death clips (the female villagers and a few others have only idle, walk and run: unusable for combat, listed
as unusable in the table). Deduplicated by model that leaves the roster below (see `drakan_coop_character::roster()`; the
full per-class table is `analysis/coop_characters.csv`).

Resource record ids: `0x50000 | class id` (database 5 = NPCS.ODB in every level; verified against the creature ids already
seen in `world-model.md`: Yellow Orc class 4395 is `0x5112b`). NPC clips are `0x50000 | NPCS.ADB id`; Rynn's `0x20000 | SYSTEM.ADB id`;
Arokh's `0x40000 | id` was observed in Ravenshold (`0x40663`, "Hover" is SYSTEM.ADB id 1635; the 4 is that level's
database index, see section 5).

**Clip mapping (the part that is ours).** Every clip Rynn can play is classified by the class field it sits in (generator:
219 clips, 19 activities: idle, walk, run, run back, strafe L/R, turn L/R, jump, in air, land, crouch, attack, hit, block,
draw, death, spell, roll, other). A ghost of a non-Rynn partner reads the pose it already receives (Rynn clip ids and
times), turns each clip into an activity and plays the character's clip for that activity, with fallbacks (strafe and run back
fall back to walk or run, jump / in air / land to run or idle, attack to idle when the NPC has none). Clip time is scaled by
the duration ratio (durations are the f32 after the name in the ADB 0x0501 header: Rynn run 767 ms, Villager run 800 ms) and wrapped
for looping activities. Channels: channel 0 plays the lower (locomotion) clip, all other channels the upper clip while the
two differ (attack / block / aim), else everyone plays the lower clip. This is the same lower / upper layering the Rynn
ghost uses, only the channel meaning is approximate (the NPC skeleton's channel to node table differs and was not decoded).

**Not done: the local body.** The joining player's own body stays Rynn on their own screen (the controlled actor, camera,
HP block, inventory and every `ctx+0x4b0` reader assume Rynn's class and skeleton). Swapping the model of the live player is
not safe: the skeleton instance (`actor+0x50`, 11 channels, node-to-channel table) belongs to the model and the controller
drives Rynn's clips. So everyone sees the partner as the chosen character, the chooser sees themself as Rynn. A first
person style local swap would need the third-person camera and the controller to run an NPC skeleton: out of scope.

## 2. Verified in game (two instances, PAL, Surdana, harness `scripts/harness/coop_characters.py`)

Runs under `build/harness/chars4`, `chars5`, `chars6`, `dragon3` (git-ignored; contact sheets `contact.png` in each). A hosts and
announces `DRAKAN_COOP_CHARACTER=ee7` (Male Villager), B joins as `1798` (Capn Serris); each screen shows the *other* player
as the NPC:

* **[run] The partner appears as the chosen character on both screens** (a villager beside Rynn on A, a heavy man on B; B's
  teleport test aid `DRAKAN_COOP_TEST_GOTO` keeps the two bodies apart). The actor is spawned with the NPC record
  (`0x50000 | class`, index looked up in the level's resource db), keeps the NPC init (it inserts itself: forcing the extra world
  insert with `DRAKAN_GHOST_NPC_INSERT=1` is not needed), the host swaps its handler for the inert ghost handler
  (events 4 / 8 swallowed, event 2 goes to the class's own handler captured at spawn).
* **[run] Clip mapping runs**: the stand-in's skeleton has **one channel** (not 11); the log shows its clip change from idle to
  the character's walk / run clip when the partner walks (`[ghost] id N channels 50f3b -> 50b9c`). Limb animation was not
  inspected up close (the walking partner is small in the screenshots); attack, hit and death clips are mapped but not seen.
* Level changes, saves and respawn on character change use the existing ghost lifecycle; the registry slot check of Rynn ghosts does
  not apply to stand-ins (they are tested by vtable and the dying flag, a stand-in's `ref` was valid in the run).
* Not done: weapons on stand-ins (no Rynn hand bones, `startWeaponChange` skips them), the joiner's own body (stays Rynn),
  HP / hit reactions of the stand-in (inert like every ghost), the character-select UI (roster API only).
* Creature keying skips ghost handles (`drakan_coop.cpp keyCreatures`): an NPC stand-in is type 7 with HP and would otherwise
  look like a placed creature.

## 3. Dragons

Research is in [world-model.md section 11](../../reference/world-model.md). Verified:

* **[run] A second Arokh body spawns and is visible** (`DRAKAN_GHOST_MIRROR=1 DRAKAN_GHOST_DRAGON=1`, `dragon3`): factory with
  record `0x20a60`, then the same post-spawn restore as the Rynn ghost. **Findings**: the dragon's init resets the camera mode
  word and **does overwrite `ctx+0x4b0`** even with mode 7 (the log shows `ctx+4b0 a5b6b0 -> dabc00`), restored by the existing
  save / restore; the spawn must **not** run inside the no-preempt section (the first attempt hung the guest, the init waits on
  the guest); the dragon is in the registry as type 3, class `0x1001c`, ref valid; the Rynn link (`+0x4d8`) of the local player
  was not checked after the spawn (**not verified**, the player kept moving and the camera stayed on Rynn).
* **Not done**: replay of a mounted partner's pose on the dragon ghost (API: `drakan_ghost::spawnDragon`, `setPose` remaps the
  clip high half with `DRAKAN_DRAGON_CLIP_HI`, default `0x40000`, unverified), the rider on the dragon, and wiring it into
  `drakan_netplay` (a mounted remote is still hidden). A second *flyable* dragon is not possible in the engine's single
  controlled-actor design: each partner flies their own Arokh on their own machine; the others would see a visual copy.

## 4. Files

`drakan_coop_character.{h,cpp}`, `drakan_coop_character_table.inc` (generated), `drakan_ghost.{h,cpp}` (`spawn(t, character)`,
`spawnDragon`), `drakan_netplay.cpp` (send `selected()`, spawn with the remote's character, respawn on change),
`drakan_coop.cpp` (one line), `runtime/net` Protocol v7 + tests, `analysis/scripts/coop_character_roster.py`,
`analysis/coop_characters.csv`, `scripts/harness/coop_characters.py`. Env: `DRAKAN_COOP_CHARACTER`, `DRAKAN_GHOST_CHAR`,
`DRAKAN_GHOST_DRAGON`, `DRAKAN_DRAGON_CLIP_HI`, `DRAKAN_GHOST_NPC_INSERT`.

