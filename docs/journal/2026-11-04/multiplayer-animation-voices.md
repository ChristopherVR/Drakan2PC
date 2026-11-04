# Multiplayer character airborne fallback and damage voices

## Evidence

* Read PAL SYSTEM.ODB / NPCS.ODB and each selected model's MOD 0x0208 animation list. All 14 NPC choices lack
  jump / airborne / landing / roll clips; their Dodge Left / Back / Right animation arrays are empty. Their rigs
  have 57 or 60 joints; unrelated NPC dodge clips have different rigs. Replaying Rynn's joint tracks is not safe.
* The old mapper fell back to Run for Jump, InAir and Roll, producing running feet during side jumps / cartwheels.
* Rynn's six HitRxn sound arrays share SYSTEM ids 417, 418, 423, 425, 427, 428, 588. NPC Damage Reaction Sounds
  are empty / zero except Nichols (NPCS ids 2052, 2053, 2055), whose SDB names are Nichols_HitRxn_2 / _1 / _3.

## Changes

* Hold a skeleton-compatible cower / combat-ready pose for missing airborne / evasive clips, without run cycling.
  This does not provide authored directional jumping. Correct directional motions still require compatible assets
  or a verified retargeting pass. Keep the player controller's real movement and trajectory unchanged.
* Sanitize non-finite / negative source animation times before mapping to avoid contaminating skeleton playback.
* Generate original damage-voice metadata alongside the roster. The ghost PlaySound hook changes only exact Rynn
  HitRxn resource ids while a local NPC view is active. Nichols uses his own voices; NPCs with no configured voice
  suppress that vocalization. Preserve impacts and every unrelated sound. Respect level-dependent SYSTEM bank ids.
* The sound hook chains whichever PlaySound implementation is installed, preserving sound diagnostics.

## Verification

* Rebuilt `character_roster_test` and ran it successfully: all 14 NPC airborne / evasion fallbacks differ from Run,
  remain held across time, and never reuse a foreign bank. Tests cover non-finite source time, Nichols voice choices,
  missing voice silence, Rynn / unknown selection, hard landing preservation and same-low-id bank isolation.
* All six `test_character_previews.py` tests pass. Ruff check passes for the generator; formatting applied.
* No game build / deployment performed by this subtask. Root integration will record compilation / live evidence.


## Continued implementation: compatible motion assets and split channels

The preceding held-pose / silence changes were insufficient and are superseded by this implementation.

* Added `character_motion.py` / `character_motion_retarget.ts`, deriving a private 29 MB PAL mod pack from the user's
  own disc. Retarget all 219 controller animation clips to all 14 selected NPC models (3,066 clips / 6,132 records),
  with deterministic model / source-clip ids. Preserve source key timing, distinct left/right rolls/cartwheels,
  jumps, airborne/landing phases, weapon grips and attacks. Match 57/57 joints on villager rigs and 58/60 on warrior
  rigs; two unmatched accessory joints retain the target bind pose. Explicit aliases pair villager finger chains.
* The retarget math recovers model-axis motion deltas through the source bind frame and applies them through the
  target bind frame. Target bind translations preserve limb lengths; render-only clips retain root bind translation because the controller
  already integrates world displacement. The generic retargeter also supports scaled root motion for independent actors.
  Source skeleton tracks are never replayed directly on an incompatible target. Original MOD bytes are preserved,
  extending only the animation-id array. New ADB entries and MOD record overrides coexist with other mod packs.
* Gate generated clip mapping on the matching mod actually being loaded. Preserve exact source clip/time locally
  and remotely. No protocol change: network poses already carry controller source clips, time and channel masks.
* Found and fixed another defect: the former ghost driver applied Upper to all NPC channels except channel 0, so
  lowerbody and leg/sheath channels could play an attack instead of locomotion. Translate each target channel to
  the matching source anatomical channel before using the transmitted `lowerMask`.
* All 14 curated NPCs are male. Missing configured damage voices now use the original Nichols nonverbal male damage
  reaction set rather than silence; own configured voices take precedence. Only exact Rynn damage vocals change.
* Harness private play folders now include deployed mods/previews. `coop_characters.py --motion` exercises real
  equip, standing/forward/left/right/back jumps, attacks while strafing, and save/recreation with phase screenshots.
  Opt-in ghost logs record source/mapped animation transitions and positions for local and remote bodies.

**Automated verification:** rebuilt/passed `character_roster_test` (exact directional clip/time mapping, mask topology,
voice substitution and unrelated-effect preservation); 11 TypeScript retarget tests pass, including same-rig rotation/
timing preservation and agreement with independently implemented PC-delta conversion across different bind frames.
Formats TypeScript typecheck passes. Largest generated entry is 51,276 bytes, under the native whole-request limit.
Initial live gameplay / sound evidence was pending at this checkpoint; see the verification below.


### Live motion validation and a rejected first pass

* `build/harness/character-motion-live-1/` loaded the generated pack and emitted distinct left/right source clips,
  but screenshots showed bodies leaving view mid-cartwheel. This exposed duplicated positional root motion:
  the controller already moved the actor while the inert visual skeleton replayed the same displacement.
  This run was **not** accepted as a visual success, despite clean process exits.
* Generate render-only clips with constant target root bind translation, preserving rotating joint motion and
  all key times. Added a synthetic test that proves translation is removed while rotation still changes.
  Twelve TypeScript retarget tests pass.
* **[game]** `build/harness/character-motion-live-2/`, canonical `dist/DrakanPC-implementation` deployment,
  Monk host / Northmen Archer joiner. Both rig families were inspected in actual standing-jump and left/right
  cartwheel phases. The bodies remain visible, rotate/tuck correctly and retain coherent textured limbs.
  All 13 harness checks pass: local/remote standing-jump, airborne, landing, left/right evasion, identical exact
  source-to-generated mapping across clients, generated pack loaded, both partners spawned, clean exits.
  Quick-save removed and recreated both local and remote bodies without resource/heap errors.
* Existing inventory coordinate/Enter and mouse-only probes did **not** equip a weapon in these runs; the screenshots
  showed unarmed jump states. Bow investigation supplied the actual native-inventory keyboard path (dismiss help,
  direction selection, E/Square to equip). Armed motion and damage-voice verification continue with that recipe.


### Native channel allocation, armed motion and original male voice proof

* Further inspection found that MOD channel definitions are **not** the active runtime channel array. NPC instances
  start with one channel, whereas the controller adds channels dynamically. The provisional MOD-index mapping was
  therefore insufficient for moving attacks. PAL `0x102e70(skeleton, node)` creates/reuses a playback channel and
  updates the subtree's node-to-channel lookup through `0x108fb8`.
* Spawn NPC bodies through the native torso-node 2 channel allocation using a proper guest continuation (not a nested
  allocation call). NPC creation no longer holds the Rynn-only camera-mode no-preempt section. Live playback selects
  lower/upper pose by each channel's owning node at +4; runtime channel order and MOD list order are independent.
* **[game]** `build/harness/character-motion-armed-1/`: Monk Boss host / Ranwulf joiner, explicit harness-only native
  inventory setting, real Wood Club items attached (`3217b`). Both native skeletons allocate two channels and then
  gain a held-item channel. Independent run/attack states were observed (`20672/20686`, mask 7819), mapped to distinct
  target clips (`58246/58251`). All 14 checks pass, including native torso allocation and save/recreation. Both exit
  cleanly. Inspected `a/ready.png`, `a/a_left_3.png`, `b/b_right_3.png`, `b/b_strafe_attack_2.png`: coherent textured
  bodies, club grips and armed left/right flipping motion. HUD identifies the actual selected character after root's fix.
* **[game, genuine projectile]** `build/harness/character-voice-pvp-2/`: Nichols attacker / Monk victim, no injected
  claims. Actual arrow `dfb1b0` hit ghost 2/player 2, claim damage 10; server accepted 80 at the harness's scale 8, victim
  health 20. Victim native hit reaction logged 80 damage and allocated sound `50807` with gain about 0.70 / voice 45.
  This particular overall recipe was not accepted as clean because it expected native inventory while the primary
  deployment used modern inventory, leaving A waiting for gameplay; the damage/audio event itself was real.
* **[audio]** Mixed output `build/character-motion/victim-monk-2.wav` contains original NPCS.SDB sound 0x807
  `Nichols_HitRxn_3`. Decoded the disc's SPU-ADPCM, resampled to 48 kHz and compared against the actual stereo mix:
  normalized waveform correlation **0.9824567**, starting at 33.5099167 seconds. Quantitative result is in
  `build/character-motion/voice-wave-verification.json`. This proves actual original male-voice playback, not silence
  or merely a remapped id. The set is shared for curated male NPCs with no own configured damage voice.
* Found another real combat bug: the projectile target list included the shooter's own cosmetic body. A projectile
  could be marked consumed by a player 0 visual before reaching its opponent. Selection now filters real remote
  player targets before consuming the shot. Focused regression starts a shot inside a local cosmetic body and verifies
  it still reaches a remote target 700 units ahead, never emits a self/player 0 hit, and respects trajectory height.
* Correct projectile Damage property index to 0 (0x1a is Armor Piercing). Use the shared BowCombat native standard
  arrow adjustment only for a known locally-owned standard arrow; retain actual special-arrow/spell stats and server
  caps. Net match regression verifies the native 10-damage claim is accepted. Native-only and modern policy tests are
  shared with the bow work.

Remaining validation: genuine melee contacts are being isolated with explicit native inventory mode; only projectile
combat is claimed above. Four selectable classes across both rig families have live motion evidence; the remaining
models have generation/matching coverage, not individual live-match certification.
