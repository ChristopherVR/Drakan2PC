# Order of the Flame in the PS2 engine: status

Living tracker (reviewed 2026-11-05). Drakan 1 (Order of the Flame, PC) levels are converted from the player's own install into mod
packs that the recompiled PS2 engine plays; nothing from either game is committed. Detail lives in the dated notes under
`docs/journal/2026-10-31/` and `docs/journal/2026-11-01/` (`ootf-*.md`, `arena-*.md`, `game-select-ootf-boot.md`); formats are in the
`editor/packages/formats` submodule, pack rules in `reference/modding.md` ("Campaign packs").

**Current acceptance:** the November 4 header-corrected private candidate completes the original opening through normal New Game, returns keyboard/mouse control, and makes a native quicksave. A 541-second run ended with actual process exit 0 after normal movement and a sustained observation beyond the previous approximately 331-second allocator failure. Unintended Dust Devil generators and particle actors remain absent in all RAM samples; live guest heap settles near 16.33 MB instead of exhausting the guest heap. This verifies the specific current-header repair in that run, not uninterrupted campaign completion. A copied-card native load/move/save/move/reload test also passes with exactly restored saved coordinates and working movement after reload. Legitimate traversal from the fresh checkpoint remains in progress; the candidate is not promoted to the primary play folder. Audio was disabled in this harness run, so cinematic speech playback is unverified.
Earlier isolated level loads and scripted exits below do not establish full progression. See the
[current-header runtime acceptance](../journal/2026-11-04/ootf-current-header-runtime.md),
[class-reference and heap investigation](../journal/2026-11-04/ootf-class-reference-defaults.md), and earlier
[cinematic readiness investigation](../journal/2026-11-04/ootf-native-readiness-probe.md).

**November 5 progression repair:** the converter now preserves the first authored player's linked starting inventory through the synthetic Intro player. A fresh normal opening on the previously validated executable earned a native save with Atimar carried; continuations equipped it, performed an identified native melee swing, and preserved the equipped blade through save/reload. The first run was stopped after its earned save when the inventory tutorial blocked the test script; both continuations quit normally. A newer executable had reached its runtime limit during cinematic resource readiness, so this acceptance is bounded to the controlled executable and pack, not every concurrent renderer change. Ordinary progression continues from the earned checkpoint. See [starting inventory](../journal/2026-11-04/ootf-starting-inventory.md).

Converted cinematic actors are also retained by default after the model/clip and display-list repairs. Normal opening solo/co-op checks and isolated later lifecycle checks support that restoration. An isolated ending diagnostic renders the original Delon animation, but its room geometry is not reaching native draw submission despite loaded model and textures; the middle black interval is an authored fade. These checks do not establish a naturally earned or visually complete ending. See [retained cinematic actors](../journal/2026-11-04/ootf-retained-cinematic-props.md).

## What works

* Converter `editor/scripts/ootf-convert.ts` (`--campaign 1` for the campaign): terrain, textures, models, creatures (skinned, retargeted
  to PS2 rigs), classes, puzzles (plates, doors, counters), exits, texts, music, sounds. Release packs of 2026-11-02: campaign
  `ootf-levels` (13 levels, 9119 files) and arenas `ootf-arenas` (10 levels, 158 files, one `maps/*.ini` each).
* Game choice at the title ("Choose your game": Drakan / Drakan: Order of the Flame, `drakan.ini [general] game = ask|d2|d1`), a
  campaign-only New Game that starts at `OotF 00 - Intro` (no Drakan 2 intro), separate save cards, Order of the Flame title plaque.
* Earlier isolated tests loaded all 13 campaign levels and exercised host-fired `DRAKAN_LEVELCHANGE` exits, including Volcanic
  hops. Intro -> Ruined Village is reached naturally from the title. The November 4 converter fixes detector message routing;
  a real Ruined Village -> Wartok walk-in transition is verified with the opening cinematic disabled in that private fixture.
  The private November 4 candidate now reaches control through the normal opening; the complete walk-through chain remains unverified.
* Ten multiplayer arenas as maps tagged `Drakan 1 / CUSTOM` (4 air, 3 ground, 3 QOD-style), dragon-vs-dragon breath PvP in the air
  arenas (host-claimed hits, dragon health, death, respawn), ranged and melee PvP on ground arenas.
* Co-op stays on the Drakan campaign path and shares the New Game card; the OotF Multiplayer page has Host / Join / mplayer.com only.
* HD textures for both packs (`analysis/scripts/ootf_hd_pack.py <zips>`, output under `dist/DrakanPC/textures`, never committed).
  The converter still quantises object textures above 128 px to 8 bpp because the guest's 32 bpp mip loop is slow; an opt-in native
  filter (`DRAKAN_TEXSCALE=1`, `ootf-native-mips.md`) removes the cost but stalls the heap intermittently (Islands Night), so it is off.
* Drakan 2 with both packs installed still boots normally (choose Drakan; `Level 0 - Intro` then `Level 1A - Surdana`; no OotF leakage).

## Known gaps

* Dragon breath in the air arenas draws a flame trail (host-spawned FX particles, `ootf-dragon-breath-visual.md`); it needs the FX
  resources in the arena level (true for the air arenas, other maps untested) and is not wired to the campaign dragon. Dragon health
  regeneration lifts the bar between hits (`ootf-dragon-pvp.md`).
* The real PC triggers were not walked: Islands Detector 4131, Volcanic Detector 2318 (mount only), Succubus `ship2`. The placed exit
  boxes are the tested path (`ootf-link-regression.md`).
* Volcanic runs at about 42 fps with the rift effect live (Vortex / Inner / Outer Rift movers).
* The exit scan cannot resolve level-local destination strings of converted levels; use the exit index with `DRAKAN_LEVELCHANGE`.
* The older separate packs share patched files (`RESOURES.MOD/.TXD`, `PROCTEES.TXD`, `WORLDCON.*`, `RESOURES.SDB` entries): the
  later pack wins and logs conflicts. Convert campaign and arenas together instead (no level paths = all 23 levels in one pack); for separate packs keep `ootf-levels` after `ootf-arenas` in `mods.ini`; arena models missing from the campaign
  copy were not checked in a full arena match with both packs installed.
* The Host Game mode strip still shows a disabled Co-op tile ("Coming") beside the PvP modes.
* The Ruined Village arrival fade is black for a few seconds after a host-fired exit; the arrival marker placement and a walked exit
  between levels were only checked for some pairs.
* Map-file key for an adopted loadout item (the ini only knows factory resources), `arena-pvp-open-items.md`.
* Duplicate-object warnings from source data (Alwarren, Grimstone Mines) are harmless but unexamined.

* 2026-11-02 state proofs (`journal/2026-11-02/ootf-state-proofs.md`): switch -> portcullis and plate -> mover chains proven by world dumps.
  An isolated Dr. Evil fight reaches credits after four real kills, using scripted wake, head HP reduced to 3, god mode and an altar
  teleport (`journal/2026-11-02/ootf-drevil-ending-and-creature-closeups.md`). This verifies the native death-counter-to-credits chain,
  not the natural Delon-triggered wake, normal boss difficulty or uninterrupted campaign completion. The earlier November 1 ending
  test injected death messages. Dying dragon now falls; breath visual: done, see `ootf-dragon-breath-visual.md`.
* 2026-11-02 pass (`journal/2026-11-02/ootf-unproven-creatures-chains.md`): Succubus drawn; Blade Dragons alive and attacking. The initial Queen / Hueges / Tick close-ups were superseded by the hidden-creature investigation below;
  Ebon Dragon and Drone now shown drawn and moving, bats alive but not photographed (`journal/2026-11-02/ootf-flyer-closeups.md`); switch-to-portcullis proven by state; the pack conflict is fixed by one combined conversion (`journal/2026-11-02/ootf-combined-pack.md`).

## Release checks (2026-11-02)

`npm run build` and `npm test` in `editor/` (63 files, 603 tests), `python scripts/run_tests.py` (213), `scripts/repository_guard.py`, ruff
and clang-format 19 clean; `dist/DrakanPC` rebuilt with `Build-LocalGame.ps1 -Incremental`. Smoke run evidence is private under
`build/ootf-run/rel/smoke/`. See `docs/journal/2026-11-02/release-smoke.md`.

- Hidden creatures (PC flag 0x01 clear) start hidden until a Detect and Execute / timer link fires: proven for Death Mage and Navy Hueges.
  Queen, Navy Hueges and Tick draw when visible. The Queen has no links, so the converter now shows unlinked hidden creatures
  (formats 10f4c65); Queen drawing is proven without the debug visibility flag.
- Tick activation by its detector remains unproven: teleporting to (126944,209400,152576), near Detectors 1691/1702 in Succubus Lair,
  fails with a missing-target JALR to zero at guest 0x10822c. The failure persisted after removing Ticks, their waypoints, detectors,
  timers, nearby scenery and music/sound objects, and after position offsets. This isolates an unresolved area/terrain/teleport issue,
  not a demonstrated Tick class defect. See [hidden-creature investigation](../journal/2026-11-02/ootf-hidden-creatures.md).
