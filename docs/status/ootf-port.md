# Order of the Flame in the PS2 engine: status

Living tracker (reviewed 2026-11-06). Drakan 1 (Order of the Flame, PC) levels are converted from the player's own install into mod
packs that the recompiled PS2 engine plays; nothing from either game is committed. Detail lives in the dated notes under
`docs/journal/2026-10-31/` and `docs/journal/2026-11-01/` (`ootf-*.md`, `arena-*.md`, `game-select-ootf-boot.md`); formats are in the
`editor/packages/formats` submodule, pack rules in `reference/modding.md` ("Campaign packs").

**Current acceptance:** the November 4 header-corrected private candidate completes the original opening through normal New Game, returns keyboard/mouse control, and makes a native quicksave. A 541-second run ended with actual process exit 0 after normal movement and a sustained observation beyond the previous approximately 331-second allocator failure. Unintended Dust Devil generators and particle actors remain absent in all RAM samples; live guest heap settles near 16.33 MB instead of exhausting the guest heap. This verifies the specific current-header repair in that run, not uninterrupted campaign completion. A copied-card native load/move/save/move/reload test also passes with exactly restored saved coordinates and working movement after reload. Legitimate traversal from the fresh checkpoint remains in progress; the candidate is not promoted to the primary play folder. Audio was disabled in this harness run, so cinematic speech playback is unverified.
Earlier isolated level loads and scripted exits below do not establish full progression. See the
[current-header runtime acceptance](../journal/2026-11-04/ootf-current-header-runtime.md),
[class-reference and heap investigation](../journal/2026-11-04/ootf-class-reference-defaults.md), and earlier
[cinematic readiness investigation](../journal/2026-11-04/ootf-native-readiness-probe.md).

**November 5 progression repair:** the converter now preserves the first authored player's linked starting inventory through the synthetic Intro player. A fresh normal opening on the previously validated executable earned a native save with Atimar carried; continuations equipped it, performed an identified native melee swing, and preserved the equipped blade through save/reload. The first run was stopped after its earned save when the inventory tutorial blocked the test script; both continuations quit normally. A newer executable had reached its runtime limit during cinematic resource readiness, so this acceptance is bounded to the controlled executable and pack, not every concurrent renderer change. Ordinary progression continues from the earned checkpoint. See [starting inventory](../journal/2026-11-04/ootf-starting-inventory.md).

Converted cinematic actors are also retained by default after the model/clip and display-list repairs. Normal opening solo/co-op checks and isolated later lifecycle checks support that restoration. A further conversion fix supplies native draw lists for rigid generic props: an isolated ending comparison now renders textured room walls and the mirror frame around Delon's original animation. The middle black interval is an authored fade. The regenerated pack preserves every level/class payload and model ID; only the common model container changes. Loading the earned save now renders the tavern's stone and wood surfaces, and ordinary movement reaches its visible entrance. Cellar entry and quest collection remain in progress. These checks do not establish a naturally earned or fully verified ending. See [retained cinematic actors](../journal/2026-11-04/ootf-retained-cinematic-props.md) and [ordinary route evidence](../journal/2026-11-05/ootf-normal-route.md).

**November 5 rotation repair:** preserving unsigned source angles fixes a 16-degree native orientation error affecting 2,374 imported objects. A fresh normal opening on the controlled `7DBB` pack completed in 316 seconds, followed by native equip, save and reload. Tavern and entrance bases now match their authored orientation, including after reload. Traversal continues from this fresh earned card; earlier saves contain the old transforms. See [rotation encoding and fresh validation](../journal/2026-11-05/ootf-rotation-encoding.md).

A private Atimar-only model candidate now produces genuine melee damage against the pursuing Wartok from an earned save. It adds the missing draw list while preserving the complete authored sphere collision hierarchy; replacing that hierarchy with box bounds caused native weapon deletion. Ordinary guarding and counters subsequently kill the pursuer, followed by a native save and clean exit. The enemy stays dead after native reload, and ordinary movement and jumping reach the tavern interior with health unchanged. This verifies contacts, a normal-route kill, persistence and tavern entry in that candidate. See [ordinary combat and entry evidence](../journal/2026-11-05/ootf-normal-route.md).

A separate controlled texture comparison restores Atimar's visible blade by making its texture square while preserving the original alpha metadata. The converter now applies guarded draw-list and indexed-square texture requirements to imported native melee weapons, preserving authored sphere bounds and resource IDs. The controlled renderer pack `F6FD` passes independent compatibility audits: all 23 level and 2,150 class payloads are unchanged, with differences confined to model and texture representations. An earned-card continuation shows the blade, opens the cellar door through ordinary interaction and passes into the descent with health unchanged. The open door persists through native reload. The key remains uncollected and broader weapon validation continues. See [melee representation and pack audit](../journal/2026-11-04/ootf-retained-cinematic-props.md) and [ordinary cellar entry](../journal/2026-11-05/ootf-normal-route.md).

Imported Long Bow nocking and shooting now work in an isolated native fixture after restoring its quiver's projectile reference and choosing a compatible rigid native Bow template. Six native launches consume exactly six carried arrows, including a shot while running and airborne with the sight retained. Original aiming/cadence parity, special effects and earned campaign acquisition remain open. See [projectile and rigid-template validation](../journal/2026-11-05/bow-projectile-and-rigid-template.md). A guarded representation upgrade also restores the visible wood Long in standing and jumping shooting poses; production model records exactly match the live-tested candidate. See [rigid Bow drawing](../journal/2026-11-05/bow-rigid-representation.md).

A separate third-person test now verifies a commanded native running jump (type 2), upward motion, a fresh fire press and a real shot consuming one arrow. Holding fire does not create extra shots in the bounded test. See [running-jump input acceptance](../journal/2026-11-05/bow-imported-running-jump-acceptance.md); this remains distinct from original combat timing parity.

The full-hole terrain conversion now preserves the native triangle collision flags. An identical-input comparison removes the false upper floor blocking the tavern descent; a continuation traverses the low stairs using ordinary crouched movement and reaches the cellar with health unchanged. Native saving and reloading pass. See [terrain collision and cellar traversal](../journal/2026-11-05/ootf-terrain-hole-collision.md).

The earned route now continues through the cave and collects the Priest's Key by ordinary E interaction. The native pickup message and live Key inventory resource confirm acquisition, and that resource persists through native F5/F8. The route includes real damage from pursuers. Opening the priest's house, acquiring the book and the remaining campaign are still unverified. See [key collection and persistence](../journal/2026-11-05/ootf-normal-route.md#earned-priests-key-and-persistence).

**November 6 localized installs (issue #3):** the converter now detects the PC install's language from Dragon.rrc, keys
arenas, music, the intro, the Book and the first level on the level name ids instead of English folder and level names,
keeps German Windows-1252 punctuation in converted text, writes ASCII titles the runtime accepts (`Höhle` -> `Hoehle`), and
stops with an actionable `OotfSourceError` when placed objects need a missing database or a campaign level has no player
start or no readable exit field. The English conversion is byte-identical before and after (17,334 files). No German
install has been converted yet. See [localized installs](../journal/2026-11-06/issue-3-localized-ootf.md).

**November 6 Ruined Village opening (issue #5):** with no input at all (600 s and 313 s runs, full 23-level pack) the
opening scene ends by itself 140 to 160 s after gameplay start, the dying-villager scene follows and the HUD and player
control arrive at about +240 to +250 s; no key is needed and nothing soft-locks. See
[village scene](../journal/2026-11-06/issue-5-village-scene-and-title-notes.md). The end time varied by 30 s and more
because scene 1.2a only starts once its textures are loaded and the native 5.4 MiB texture cache was full, so it waited
0 to over 110 s for a buffer to free up; campaign packs now get an 8 MiB budget and 1.2a starts on its trigger in every
run (1.3 within 2.6 s across three runs, guest heap about 2.6 MiB higher). See
[scene timing](../journal/2026-11-06/issue-5-village-scene-timing.md).

**November 6 progression past Ruined Village (issue #5):** three converter defects and one engine difference kept walked
exits from working. The converter's own exit boxes sent message 5, which the exit's touch handler ignores (every disc
exit detector sends ON); the PC levels fire their exits from their own detector boxes, often far from the exit object
(the Islands exit object lies 77,000 units under the island); and two exits lost "Maintain Player Position" 0. Exit
detectors now send ON, the walk-in boxes sit on the PC detector boxes (keeping their gating) and the PC value is kept.
The runtime also ignores exit touches in campaign levels under the loading screen (a touch there left the exit queued
with no prompt and dead for good) and until the player has walked off her arrival spot (a cut scene that put her back in
the way-back box offered the level she had just left). Per-level state
(placed near the trigger, then real walk, prompt, save screen, load; `DRAKAN_START_LEVEL` direct starts, god mode):

| Level | Reached by a walked exit | Its own exit | Native F5 / F8 in the level | Blockers |
| --- | --- | --- | --- | --- |
| Ruined Village | from the Intro (normal New Game) | to Wartok: walk-in (Nov 4, PC detector 1075) | yes (Nov 5) | quest route (house, book, temple) still being walked |
| Wartok Canyons | yes (Nov 4) | to Grimstone: dismount (hold Cross / C), walk east into the PC mine-mouth box, prompt, save, load (`w03-04l`) | not checked | none found (a direct start puts Rynn on Arokh) |
| Grimstone Mines | yes (`w03-04l`) | to Grotto: PC "Creature" box, prompt, save, load (`g04-05b`) | yes: after the 120 s opening scene, exact saved position after F8 (`w03-04l`) | none found |
| Grotto | yes (`g04-05b`) | to Islands: PC box 313, prompt, save, load (`g05-06d`) | yes: exact saved position after F8 (`g04-05b`) | none found |
| Islands (hub) | yes (`g05-06d`, mounted) | to Rift Crystal: the rift box lies under the whirl doors of the bell tower, which stands in the cave system; Rynn in the box fires it, prompt, save, exit taken (`tower-d`, `i06-08c`, `i06-08d`). The earlier blank screen was the camera inside the rock around the tower, not an engine stop (`tower-c` draws). | F5 / F8 work, but S did not move the dragon between them, so the restore was not discriminating (`g05-06d`) | **blocker**: arriving in Rift Crystal on Arokh crashes (null method call at `0x23d924`, rider record `+0x57c`); Rift Crystal has no dragon. On foot not yet reached (a direct start is mounted and Cross does not dismount Arokh in the tower) |
| Rift Crystal | not yet (see Islands) | to Islands Night: placed on the WhirlpoolSteps, two jumps over the rim, Rynn slides onto the invisible basin floor into the PC box: prompt, save, load (`r08-09d`). Fixed: the box was cloned from a model-less PS2 trigger class and never placed (2026-11-06 progression 2) | not checked (direct start) | none found |
| Islands Night | yes (`r08-09d`, from the whirlpool) | to Succubus Lair: real walk into PC detector 4928, prompt, save, load (`n09-10`) | yes: exact saved position after F8 (`r08-09d`) | none found |
| Succubus Lair | yes (`n09-10`) | to Volcanic: standing on the ship's deck box runs the PC chain (counter, `ship2`, fade), the ship sends ON to the exit, prompt, save, load (`s10-11`) | yes: exact saved position after F8 (`n09-10`) | none found; the Drakan 2 quest line shown on arrival was the converter's shared marker class, fixed: it now reads "Get Arokh" ([quest marker text](../journal/2026-11-06/issue-5-quest-marker-text.md)) |
| Volcanic | yes (`s10-11`) | to Rift World: mount-only box enabled by the Runeblade pickup; Rynn on foot inside the box without the blade does not fire it (`v11-12b`, gate kept) | yes: exact saved position after F8 (`s10-11`) | Runeblade sits under an ice cover (fire-arrow puzzle), E at the pedestal takes nothing; mounting Arokh by E / Triangle / Cross / walking into him did not mount (`v11-mount`); flight not run |
| Rift World | not yet | ending | | Dr. Evil (isolated proof only); a direct start loads with no scene waiting and 12.1 MiB guest heap in use (`idle-12`) |

See [progression journal](../journal/2026-11-06/ootf-progression.md) and [part 2](../journal/2026-11-06/ootf-progression-2.md).

## What works

* Converter `editor/scripts/ootf-convert.ts` (`--campaign 1` for the campaign): terrain, textures, models, creatures (skinned, retargeted
  to PS2 rigs), classes, puzzles (plates, doors, counters), exits, texts, music, sounds. Release packs of 2026-11-02: campaign
  `ootf-levels` (13 levels, 9119 files) and arenas `ootf-arenas` (10 levels, 158 files, one `maps/*.ini` each).
* Game choice at the title ("Choose your game": Drakan / Drakan: Order of the Flame, `drakan.ini [general] game = ask|d2|d1`), a
  campaign-only New Game that starts at `OotF 00 - Intro` (no Drakan 2 intro), separate save cards, Order of the Flame title plaque.
  A regenerated editor pack from English D1 data also matches the German-target run showing the owl, logo and landscape
  before Ruined Village. Early Intro frames show a black screen with HUD. See the [timed capture review](../journal/2026-11-05/issue-1-cliff-intro-city.md).
* Earlier isolated tests loaded all 13 campaign levels and exercised host-fired `DRAKAN_LEVELCHANGE` exits, including Volcanic
  hops. Intro -> Ruined Village is reached naturally from the title. The November 4 converter fixes detector message routing;
  a real Ruined Village -> Wartok walk-in transition is verified with the opening cinematic disabled in that private fixture.
  The private November 4 candidate now reaches control through the normal opening; the complete walk-through chain remains unverified.
* Ten multiplayer arenas as maps tagged `Drakan 1 / CUSTOM` (4 air, 3 ground, 3 QOD-style), dragon-vs-dragon breath PvP in the air
  arenas (host-claimed hits, dragon health, death, respawn), ranged and melee PvP on ground arenas.
* Co-op stays on the Drakan campaign path and shares the New Game card; the OotF Multiplayer page has Host / Join / mplayer.com only.
* HD textures for both packs (`analysis/scripts/ootf_hd_pack.py <zips>`, output under `dist/DrakanPC/textures`, never committed).
  Object textures of 128 x 128 and larger with more than 256 colours are written 8 bpp when a refined palette keeps them within
  38 dB colour PSNR and an alpha error of 8 (392 converted textures, 50.6 MiB -> 13.1 MiB); the rest stay 32 bpp
  ([indexed textures](../journal/2026-11-06/issue-5-indexed-textures.md)). An opt-in native mip filter (`DRAKAN_TEXSCALE=1`,
  `ootf-native-mips.md`) stalls the heap intermittently (Islands Night), so it is off.
* Drakan 2 with both packs installed still boots normally (choose Drakan; `Level 0 - Intro` then `Level 1A - Surdana`; no OotF leakage).

## Known gaps

* German (or other localized) PC installs are handled from source review and synthetic tests only: header texts, Dragon.rrc
  ids, folder layout, Intro/Book timer lengths against a German dub and a German playthrough are unverified
  (`journal/2026-11-06/issue-3-localized-ootf.md`, "Still needs a real German install").
* Dragon breath in the air arenas draws a flame trail (host-spawned FX particles, `ootf-dragon-breath-visual.md`); it needs the FX
  resources in the arena level (true for the air arenas, other maps untested) and is not wired to the campaign dragon. Dragon health
  regeneration lifts the bar between hits (`ootf-dragon-pvp.md`).
* PC exit triggers: Islands Detector 4131 fires when Rynn is in it (the box stands under the bell tower's whirl doors; the bell
  route was not played), Succubus `ship2` runs (`s10-11`); Volcanic Detector 2318 (mount only) was only checked not to fire on
  foot without the Runeblade. A mounted arrival in a level without a dragon (Islands to Rift Crystal) crashes the guest.
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
