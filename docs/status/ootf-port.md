# Order of the Flame in the PS2 engine: status

Living tracker (reviewed 2026-11-06; Volcanic updated 2026-11-12). Drakan 1 (Order of the Flame, PC) levels are converted from the player's own install into mod
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

**November 14 performance:** converted levels are slow because of their terrain, not their objects: Order of the Flame
ground changes texture nearly every cell, the engine draws one pass per texture per chunk with at most 8 textures a chunk,
so converted levels have 1 to 2.4 terrain passes per drawn cell against 0.03 on the disc (Wartok Canyons start: 5,200 to
6,000 draws and 16,600 VU1 runs per world frame; Surdana 232 draws). A re-cut of the chunks that keeps every pixel removes
only 1 to 4 % of the passes and was not landed; the gain is in the renderer (joining the terrain passes' draws) and is
listed for the renderer sessions. The huge terrain texture coordinates are stale entries that the microcode itself
transforms in place, not converter data; what is drawn is right. On a quiet machine at 4K the Ruined Village runs at 60 /
86 fps (DLSS quality / off; the 60 with DLSS is explained in the round 4 note below), Wartok Canyons at 35 / 54, Grimstone
Mines at 101 / 139. The opt-in frame queue changes no Order of the Flame frame time but steadies the Ruined Village (1 %
low 27 to 19 ms); it stays off until a contention check and a played session. See
[performance](../journal/2026-11-14/ootf-performance.md).

**November 14 play check of Alwarren, Succubus Lair, Volcanic, Wartok Canyons (round 5):** converted terrain hung the
game: Wartok Canyons in the lower canyon (4 of 4 runs on the `dist` pack, also on the pack from before rounds 3 and 4) and
the load from Alwarren into the Islands (2 of 2). Cutting crowded terrain chunks down to single cells (formats `b4deb22`)
gave layers up to 752 chunks (disc: 25); the engine builds a draw page per chunk texture for every layer in view and ran out
of guest heap while building one (`FUN_00154060`, page list read from a failed allocation). Each disc-sized chunk now keeps
at most 8 textures, so no chunk is cut (formats `e671be8`, `limitChunkTextures`; `DRAKAN_TERRAIN_CHUNKS=split` keeps the old
form); 8 to 27 % of drawn cells per level take a neighbour's texture. On the new pack Wartok runs through the same spots,
quick-loads to the exact spot and its exit loads Grimstone Mines; Alwarren's exit loads the Islands. Water surfaces that draw
as flat pale blue or lilac planes in all four levels are left to the water work. Needs a regenerated pack. See
[play check B](../journal/2026-11-14/ootf-check-b.md).

**November 14 renderer draw cost (round 4):** the Direct3D 11 renderer now binds only the draw state that changed
(`[video] draw_state_cache`, on by default, `0` = the old path). Same picture (identical replays, a read-back check of
every draw's state in the game). On a quiet machine at 4K with the owner's settings Wartok Canyons runs at 38.2 / 55.3
fps (was 34.4 / 53.1, DLSS quality / off), the Ruined Village at 60 / 83.4 (60 / 82.4), Grimstone Mines at 101 / 136 (100
/ 134). Joining the terrain passes into fewer draws (a texture array of the ground tiles) is not built. See
[renderer draw cost](../journal/2026-11-14/ootf-terrain-gpu.md).

**November 14 terrain batching (round 5):** `[video] terrain_batching` (default 0, env `GSR_TERRAIN_BATCHING`) keeps
consecutive ground passes that differ only in their texture in one batch and draws them with one classification,
binding and constants upload, switching only the texture between them, in order. Replays hash identically off and on
(Direct3D 11 and WebGPU, six captures); in the game 78 % of the Wartok Canyons draws are joined, 7.3 passes each. A
played canyon tour (walk, fight attempt, F5/F8, mine exit, F5/F8 in the mines) looks the same both ways. Frame times are
not measured yet (the machine was never quiet), so the default stays off. See
[terrain batching](../journal/2026-11-14/ootf-terrain-join.md).

**November 14, CPU side (round 4):** the Ruined Village is not capped at 60 fps: with DLSS quality the frame is the GS
worker's submission plus a `Present` that blocks about 5 ms for the GPU, while the GS worker waits for that present;
switching only the DLSS mode moves the rate (performance 65, quality 60, DLAA 38, off 85) and the `Present` wait with it.
The lever is on the renderer side (GPU time per frame with DLSS, and the GS worker idling during the present). The VU1
terrain texture stage is 40 % of the VU1 engine time in the Ruined Village (not free, as round 3 had it), but no scene is
bound on the VU1 worker alone, so it was not changed. The frame queue is now the `drakan.ini` switch
`[video] frame_queue` (default 0); a played session with it passes, but under GPU contention it made frames slower (up
to 2.3 times in one pair), so it stays off. See [CPU side](../journal/2026-11-14/ootf-cpu-frame.md).

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
the way-back box offered the level she had just left). A mounted exit into a level without a dragon (Islands to Rift
Crystal) now lands Rynn on foot, as on the PC: the runtime hands her control at the respawn and clears her rider state, and
the converter drops "Maintain Player Position" on exits whose destination has no PC way back
([mounted arrival](../journal/2026-11-06/ootf-rift-mount-arrival.md)). Per-level state
(placed near the trigger, then real walk, prompt, save screen, load; `DRAKAN_START_LEVEL` direct starts, god mode):

| Level | Reached by a walked exit | Its own exit | Native F5 / F8 in the level | Blockers |
| --- | --- | --- | --- | --- |
| Ruined Village | from the Intro (normal New Game) | to Wartok: walk-in (Nov 4, PC detector 1075) | yes (Nov 5) | quest route (house, book, temple) still being walked |
| Wartok Canyons | yes (Nov 4) | to Grimstone: dismount (hold Cross / C), walk east into the PC mine-mouth box, prompt, save, load (`w03-04l`) | not checked | none found (a direct start puts Rynn on Arokh) |
| Grimstone Mines | yes (`w03-04l`) | to Grotto: PC "Creature" box, prompt, save, load (`g04-05b`) | yes: after the 120 s opening scene, exact saved position after F8 (`w03-04l`) | none found |
| Grotto | yes (`g04-05b`) | to Islands: PC box 313, prompt, save, load (`g05-06d`) | yes: exact saved position after F8 (`g04-05b`) | none found |
| Islands (hub) | yes (`g05-06d`, mounted) | to Rift Crystal: the rift box lies under the whirl doors of the bell tower, which stands in the cave system; Rynn in the box fires it, prompt, save, exit taken (`tower-d`, `i06-08c`, `i06-08d`). The earlier blank screen was the camera inside the rock around the tower, not an engine stop (`tower-c` draws). | F5 / F8 work, but S did not move the dragon between them, so the restore was not discriminating (`g05-06d`) | none for the exit: the mounted arrival in Rift Crystal (no dragon there) crashed the guest; fixed, Rynn now arrives on foot like on the PC ([mounted arrival](../journal/2026-11-06/ootf-rift-mount-arrival.md), `v1`). The bell route works: with the Bell Hammer carried (moved onto the tower floor by a test aid), entering the tower centre rings the bell, the "Bell Hammer" scene plays, the whirl doors open and Rynn drops into the rift box and reaches Rift Crystal on foot (`bell-h`, [Islands bell](../journal/2026-11-06/ootf-islands-bell.md)); the Alwarren hammer is carried over through the walked exit (`alw-16`, `alw-17`, [Alwarren hammer](../journal/2026-11-07/ootf-alwarren-hammer-and-scenes.md)) |
| Alwarren | from the Islands (2026-11-01 walkthrough) | to Islands: placed into Creature Detector 153, prompt, save, `exit taken` (`alw-13`, `alw-14`); Rynn arrives in the Islands on foot (`alw-16`) | not checked | Hammer route ([Alwarren hammer](../journal/2026-11-07/ootf-alwarren-hammer-and-scenes.md)): six crystals, pillars, six locks and the barriers work after three fixes (logic class defaults, equippable keys, language-safe class ids; formats `34ddc63`, runtime field-list rebase). The Islands load after a long Alwarren session works: 6 of 6 on formats `7392049` (`long-1` to `long-7`, German tree, no pageless layer in the terrain LRU), so the earlier stalls were the LRU fault fixed in `5dfe29a`. Open: the war giant kill. Aimed placements face him, the sword now equips (Tab, E) and hits land (150 -> 90), but he grabs, throws and eats Rynn through god mode, and a pinned Rynn never hits; a time-based script cannot fight him, so the PC reveal of the hammer is unplayed (a test aid unhides it). With the closed-loop driver (counter-attacking his reach and swipe, his animation read live) he goes from 150 to 90 within 10 s, but a lunge-step grab still ends every attempt (`g1` to `g13`, [closed-loop driver](../journal/2026-11-07/ootf-closed-loop-driver.md)). With the hammer carried the bell rings, and with Down, Cross the second save completes and Rift Crystal loads with control (`bell-2`, [giant, Islands, rift](../journal/2026-11-07/ootf-giant-islands-rift.md)) |
| Rift Crystal | yes (`v1`, from the Islands box on Arokh: on foot at the start, W, F5, S, F8 back at the exact saved position, D) | to Islands Night: placed on the WhirlpoolSteps, two jumps over the rim, Rynn slides onto the invisible basin floor into the PC box: prompt, save, load (`r08-09d`). Fixed: the box was cloned from a model-less PS2 trigger class and never placed (2026-11-06 progression 2) | yes: exact saved position after F8 (`v1`) | none found |
| Islands Night | yes (`r08-09d`, from the whirlpool) | to Succubus Lair: real walk into PC detector 4928, prompt, save, load (`n09-10`) | yes: exact saved position after F8 (`r08-09d`) | none found |
| Succubus Lair | yes (`n09-10`) | to Volcanic: standing on the ship's deck box runs the PC chain (counter, `ship2`, fade), the ship sends ON to the exit, prompt, save, load (`s10-11`) | yes: exact saved position after F8 (`n09-10`) | none found; the Drakan 2 quest line shown on arrival was the converter's shared marker class, fixed: it now reads "Get Arokh" ([quest marker text](../journal/2026-11-06/issue-5-quest-marker-text.md)) |
| Volcanic | yes (`s10-11`) | to Rift World: walked (`vr14`): with the Runeblade taken, mounted in the PC rift box 2318: message box, save, load, Rift World with control | yes: exact saved position after F8 (`s10-11`, and after mounting in `v-mount2`) | Whole route played on a pack of the committed converter with tick-based recipes and test-aid placements (`vr11`, `vr14`, [Volcanic route](../journal/2026-11-06/ootf-volcanic-route.md)): key, scene 9.6a frees Arokh, mount, breath on the catching fire object, cave-mouth dismount, the Succ Switch pressed twice turns the platform so the Arrow Detect Object stands in front of the ice, the arrow hits it, the ice Morph Plug melts (Morph Plugs fixed, formats `918de7e`), Rynn on the central pillar takes the Runeblade with E, remount, rift exit. The "no floor under the blade" was a placement artifact. November 7 (`full2`, `rift1`, [landing and stair](../journal/2026-11-07/ootf-volcanic-landing-and-stair.md)): the route now drains the lava first (Lava Rune into Key Lock 2229 / 2939, as on the PC), Arokh is flown onto the cave-mouth apron and lands 5 of 5 (`landD`), the platform bridges have a floor (formats `f5d3c6b`), and the whole route to Rift World plays on one pack. With the closed-loop driver the spiral stair is walked on foot and the Runeblade taken in 5 of 5 runs (`st1` to `st6`), and the remount from behind is engine geometry, not a converter defect: his body holds Rynn at 1,440 to 1,490 units against the 1,448 mount radius (2 of 5 from behind, at once from his side; [closed-loop driver](../journal/2026-11-07/ootf-closed-loop-driver.md)). Once Rynn has dismounted him in the session (also after F5 / F8) he turns toward her like Drakan 2's Arokh and the remount from behind works 5 of 5 (`vr1` to `vr5`); his `Initial Mode` is the PS2 default 1 as in Northern Tundra, so nothing to map ([break-away floors, Arokh facing](../journal/2026-11-07/ootf-breakaway-floors.md)). Earlier fixes: cage key class ref (`6332a2b`), model-less launcher (`8605c83`), Stomp Player order and scene 9.2 (`vol-a`). Packs from formats `ac234e9` (2026-11-09) to the 2026-11-12 fix crash Volcanic at load; fixed (scene orc lookup), gate and exit replayed (`proof4`, [Volcanic rift](../journal/2026-11-12/ootf-volcanic-rift.md)) |
| Rift World | yes (`vr14`, walked from Volcanic, mounted; Cross lands Rynn on foot, W walks her) | ending | yes: F5, walk, F8 back at the exact saved spot on foot in 8 of 8 runs (`fx-1`..`fx-8`, formats `5dfe29a`). The old hang was a converted layer with no drawn cell written with empty chunks: its draw object stayed in the terrain draw-data LRU after it was freed ([quick-load LRU](../journal/2026-11-07/ootf-quickload-lru.md)) | Dr. Evil (isolated proof only); its Morph Plugs (RibDoor, gate eyes) exist (10); a direct start loads with no scene waiting and 12.1 MiB guest heap in use (`idle-12`) |

See [progression journal](../journal/2026-11-06/ootf-progression.md), [part 2](../journal/2026-11-06/ootf-progression-2.md) and
[mounting and Volcanic](../journal/2026-11-06/ootf-mounting-volcanic.md). Mounting needs no button: Rynn walks into a landed Arokh
(as in Drakan 2, checked in Northern Tundra); Cross held dismounts once he has landed; converted Wartok remounts the same way.

**November 6 Arokh flight (issue #5):** Arokh not moving under W was not a placement artifact: unplaced at the Islands start
and after the walked Grotto exit he got full forward speed and zero velocity, while a placed Arokh in open air flew. His
flight look-ahead (a 6,144 unit ray along the velocity) counts a hit on any object box he is inside, and the Islands
arrival tunnel lay inside one: the converter's draw-list upgrade of rigid props had collapsed every authored collision box
hierarchy into one box (326 props, among them the PC tunnel shell); it now keeps the hierarchy (formats `14c9554`). The
runtime also drops look-ahead hits on an object whose box holds the start of the ray (the arrival can put him inside the
PC "Bounding Box" slab on the tunnel floor). Now the direct start flies 38,000 units out of the tunnel in 3 s, the
Grotto arrival climbs, turns and flies out, Ctrl lands him and Cross held hands control to Rynn; Drakan 2 flight is
unchanged. Open: an arrival low enough to be inside the slab gets out only slowly, and Rynn could not walk off him at the
one landing spot tried. See [Arokh flight](../journal/2026-11-06/ootf-arokh-flight.md).

**November 6 detector scenes and the Islands bell (issue #5):** detectors that play a cut scene themselves convert to FX /
SYSTEM classes, whose databases have no SSD, so 20 scene refs of 8 levels were dropped (Islands 4, Volcanic 7, Wartok 3, ...).
They now go to the template's Stomp Player SSD and a Stomp Player beside each detector runs them; 18 are converted, the other
2 name sequences missing from their PC SSD. The PS2 Use handler only opens a lock with a Key of its code, and the Islands bell
lock hangs 23,000 units above the tower floor: the Bell Hammer now converts to a Key and the lock to a carry-gated detector
column, so ringing the bell, its scene, the whirl doors and the drop into the rift box work in game (`bell-h`). See
[Islands bell](../journal/2026-11-06/ootf-islands-bell.md) (per-level sequence audit there).

**November 6 Stomp Player play order (issue 5):** the PC and PS2 Stomp Players number "List Play Order" differently (the PS2
inserts "One Scene Per Trigger And Looped" at 2) and the PS2 player keeps playing by itself once triggered, so a copied PC
"In Order Loop" replayed its scene for good (Volcanic "Arrow No Flame" every 5 s) and "All at Once" became "Randomly". The
converter now maps PC 0 / 1 / 2 / 3 to the PS2 order with the same per-trigger behaviour (1 -> 2, 3 -> 4, 2 -> 3, a multi-scene
0 -> 2), writes the PC Initial State, Message To Play Next and class-default scene lists (the PS2 class default was a Drakan 2
scene), and starts detector scenes whose detector sends Off through a twin detector (Volcanic 9.2 never started). 24 players in
8 levels change; in game the button scene plays once with control back and 9.2 plays at the cage (`vol-a`); Alwarren's "Bowling for Crystals"
no longer restarts every frame and locks input (`alw-new` against `alw-old`), and Grimstone's "positioning the hider" runs at load. See
[Stomp Player play order](../journal/2026-11-06/ootf-stomp-play-order.md).

**November 7 Alwarren hammer and cut scenes (issue 5):** the hammer route was played step by step. Three converter / pack
defects blocked it: name-mapped logic classes kept the PS2 class defaults (Alwarren's counters counted the timers' Off up),
converted Keys were not equippable so the Use handler never handed a key to its lock, and new class ids collided with the
German / Italian / Spanish trees' own Level 1 - Common classes, whose field lists the pack's override also misaligned (the
game here runs German). Fixed in formats `34ddc63` and the runtime's ODB patcher (`rebaseFieldLists`). One converted scene
per level was triggered and ran to its end with control back: Wartok MagicFarmer_1, Grimstone Interrogation, Grotto Fly
Through 1, Rift Crystal Wisp2, Islands Night Rimril dies, Succubus ALC (spiral puzzle), Rift World 10.1a. Open: the war
giant kill, and an intermittent load stall into the Islands after long Alwarren sessions. See
[Alwarren hammer and scenes](../journal/2026-11-07/ootf-alwarren-hammer-and-scenes.md).

**November 6 Volcanic route (issue 5):** the sanctum puzzle works end to end up to the Runeblade on a pack of the committed
converter: the Succ Switch (pressed twice) turns the platform so the Arrow Detect Object stands in front of the ice, the
arrow hits it and the ice melts through its Morph Plug. Morph Plugs exist again campaign-wide (Volcanic, Rift World doors and
gate eyes, Alwarren, Intro logo): their frames are converted with one collision shape each, which also removes the heap
corruption of the earlier trial (formats `918de7e`). The Runeblade "out of reach" (no floor on the platform's central
disc) was a placement artifact: Rynn takes it from the central pillar, and the walked rift exit reaches Rift World with
control (`vr14`). Open: a quick load in Rift World hangs intermittently (engine cache eviction, also without the plugs). See
[Volcanic route](../journal/2026-11-06/ootf-volcanic-route.md).

**November 7 war giant, Islands loads, second save (issue 5):** on formats `7392049` with the German tree: six long Alwarren sessions all reach the Islands with Rynn on foot (the allocator stalls are gone with the terrain LRU fix); the second save on a card needs Down to an empty slot (Cross on the used slot 1 opened the overwrite question), then Rift Crystal loads with control (`bell-2`, test aid for the hammer). Converted Air Currents carry Arokh correctly; only one Islands node uses the class default (PC 1, PS2 16), which changes where the stream ends, so it stays as converted. Open: killing the starving war giant needs a reactive driver (he grabs and eats Rynn; scripted bursts took him from 150 to 90). See [giant, Islands, rift](../journal/2026-11-07/ootf-giant-islands-rift.md).

**November 7 Volcanic landing, lava and bridges (issue 5):** the flaky cave-mouth dismount was the route, not the converter: the recipes put Arokh in the middle of the 2,048-unit-high sanctum tunnel (he was pushed up through its roof or into the sanctum), and they never drained the lava, whose collision slab over the basin keeps him off the apron. With the Lava Rune used at its lock (the PC chain: ruin detector, strong box, Mechanism Object lifts the rune; the lock lowers the lava 9 lu) a flown approach stops on its own in front of the rock over the tunnel, lands and dismounts in 5 of 5 runs (`landD1` to `landD5`). The platform's four bridges had no floor because copied PC collision lists named the wrong triangles after quads were split; fixed (formats `f5d3c6b`, `remapObbPolygons`). Key, rune, lock, free Arokh, mount, breath, flown landing, switch twice, button, arrow on 2366 (impact trace names it; not drawn), melt, blade (placed on the pillar), on-foot sanctum quicksave (loads back to the unit), remount, rift exit and Rift World on foot all play on one pack (`full2`, `rift1`). Open: walking the spiral stair with scripted input (24 tries, best 1.32 of 1.45 lu; needs closed-loop steering) and remounting from behind Arokh. See [landing and stair](../journal/2026-11-07/ootf-volcanic-landing-and-stair.md).

**November 7 closed-loop driver (issue 5):** `DRAKAN_INPUT_LIVE` (harness only) lets a driver append input lines while the game runs and read guest RAM through a status block; `scripts/harness/live_drive.py` steers from that (face, fight by the target's animation, waypoint paths with height gates, walk into an actor). Volcanic: the spiral stair walked on foot to the Runeblade 5 of 5 (the foot must be approached along the stair, and the walked radius changes from 1.1 to 1.29 lu); the remount from behind fails or succeeds on the 1,448-unit mount radius against Arokh's own body (2 of 5; from the side at once), not a converter defect. Alwarren: the war giant is hit only while he reaches or swipes (150 to 90 in 10 s), and a lunge-step grab ends every attempt, so the Bell Hammer reveal stays unplayed. See [closed-loop driver](../journal/2026-11-07/ootf-closed-loop-driver.md).

**November 7 break-away floors, per-object pieces, Arokh facing (issue 5):** converted break-away floors kept the PS2 template's bridge chunks (every PC floor folded into one class; Ruined Village spawned 570 floor entities, 14 chunks per brick). Their pieces are now converted into the template ODB's own MOD / TXD (LEVEL1ON, MOUNTAON) and named with db 0, as the disc's floors are (formats `04e1ee3`): Ruined Village has its brick floor (38 objects, 47 pieces), the pieces under Rynn fall, fade and stay gone after F5 / F8 (`fl2`, `fl3`); Alwarren, Rift Crystal, Grimstone Mines and Grotto load and quickload to the exact spot. The editor draws an object's own `Model List` (`58202939`). Arokh: `Initial Mode` is no mapping gap; after a dismount he turns toward Rynn and the remount from behind works 5 of 5. See [break-away floors](../journal/2026-11-07/ootf-breakaway-floors.md).

**November 7 invisible walls (issue 5):** converted invisible walls, bounding boxes and trigger volumes (119 placed objects,
Volcanic's Arrow Detect Object 2366 among them) drew as black boxes: their fully transparent texture was written 8 bpp with
flags word 0. It is now written like the disc's own "invisiblewall" texture (16 bpp, flags word 1, alpha clear); models and
collision are unchanged (formats `7553750`). In game the black boxes are gone in Volcanic (cage Bounding Box) and Succubus
Lair (Invisible Walls 3474, 3483), and Rynn still stops at 3483. 2366 itself was replayed on November 7: not drawn, still hit by the arrow (`full2`). See
[invisible walls](../journal/2026-11-07/ootf-invisible-walls.md).

**November 7 cut-out textures (issue 5):** the TXD flags word is the engine's alpha selector (0 = alpha off). 43 converted
8 bpp textures with hard holes (pine and snowy tree branches, torches, sconces, skeletons, banners, skies, some creatures)
carried 0 and drew their holes solid. They now carry 1, the disc's alpha-test selector, and hole palette entries take the
edge colour (formats `cd48742`); only three TXDs change, every LVL, MOD and ODB is byte-identical. In Grotto the pines and
Standing Banners now show the background through their holes. See [cut-out textures](../journal/2026-11-07/ootf-cutout-textures.md).

**November 7 Rift World, the last level, and the ending (issue 5):** Rift World is the final level (13 campaign levels). The
chain was played from a natural Volcanic arrival (Arokh keeps his fire breath; direct starts have none) through the left tunnel, the
hall and the central shaft to the altar, the four Dr. Evil heads were killed with real breath hits at their real 250 hit points (test
aids `DRAKAN_PLAYTEST_AUTOAIM` / `_ALT` / `_WAYPOINTS` / `_WP_HOLD`, `env-flags.md`), the mirror-room scene and the outro credits
(Drakan 2's German list, about 4.5 minutes) played and the game returned to the title. The RibDoors guard side vaults (not the
route; PC terrain seals them too) and Arokh's stall came from flying under about 1,400 units over the floor, not from the converter.
The Ebon Dragons die to breath (gate counter 4 -> 0, test aid `OOTF_TEST_EBONHP`, formats `4d21976`); Alwarren's war giant now dies
to the sword at 150 hit points with the aim aid. Ebon Dragons also die at 150 with the aim aid; the tunnel is flyable by hand (entrance window about 3,500 units); one
14 minute process (Volcanic exit, Rift World, heads, credits) ran with no crash or memory growth. The Ruined Village opening takes about 250 s and ends by itself (checked again with the earlier no-input method). Delon was
invulnerable (the Death Mage template has Invulnerable 1): fixed in the converter, he now takes damage and his death reaches
counter 455. Delon takes breath damage (650 -> 185 in four rounds) and the campaign end card replaces the credits (per-read patch, verified
in game). Open: finishing Delon with a sword or bow, a chained run of all levels in one process (Ruined Village's Arokh is
story-gated and its exit needs him mounted). See [progression 3](../journal/2026-11-07/ootf-progression-3.md).

**November 8 "crash after Arokh flies away":** Arokh flies away at the end of the Intro ("Intro 3"), just before the
Ruined Village hand-over. 13 runs with the user's two packs, audio and display settings, by `game = ask` and by `d1`,
idle and with input, all reached the village opening and control: no crash reproduced, and the 0-byte dumps in the play
folder belonged to a harness run. Found on that path instead: a death in any converted level ended at the title with
"This save was made with an earlier version of the mod pack", because the level-start save the game reloads had no pack
record. Every `.rsg` written now gets one; a death in Ruined Village reloads the level and Rynn walks (native and modern
title). See [Arokh fly-away report](../journal/2026-11-08/ootf-arokh-flyaway-crash.md).

**November 8 music:** the PC music (`Music.rrc`: 11 DirectMusic segments + DLS banks) is now rendered by the converter
(`ootfdmusic.ts`) into PS2MUSIC.SDB tracks. The OotF main theme ("gui", PC id 4) plays on the title menu while Order of the
Flame is the chosen game (`campaign_title_music`) and at the start of the Intro (its cut scene music action); levels start
with their PC `Initial Music ID`. See [OotF main music](../journal/2026-11-08/ootf-main-music.md). The PC Music Trigger
regions now change the music inside a level: each becomes Trigger Detector halves (front plays `Music 1`, back `Music 2`, 0
stops) switching one Triggered Music Loop per music id, verified in Ruined Village
([level music triggers](../journal/2026-11-08/ootf-level-music-triggers.md)).

**November 9 intro movie:** the OotF opening stuttered and ran fast. Measured: with vsync the cut scene loop ran a full tick plus a
1 to 3 ms tick per 33 ms and the tick-snap dropped the rounding debt on the short tick, so every scene ran 6.5 % fast (fixed in
`snapTickToRefresh`; now 1.00 s of scene per second); with the player's vsync-off uncapped settings frames arrive in bursts
(flips 2 ms apart for about 70 ms, then a 65 to 100 ms hitch, not fixed); and each of the three scene changes stalls the engine
0.4 to 0.6 s while it loads and then catches its clock up. Fix by construction: the opening is rendered once on the player's machine
into `movies/ootf_intro.mkv` (fixed 1/60 s step, lockstep audio, 4:3 1920x1440 x264, `analysis/scripts/ootf_intro_movie.py`) and the
campaign start plays it over the live level, which still does the hand-over; Esc / Start / Cross skip it and fire the level's exit.
Verified: 2641 frames = 44.02 s of picture, sound and engine time; the playback mix equals the file's sound (correlation 1.0000), game
sound silenced, 0 repeated and 0 skipped pictures over the 44 s at vsync 60 Hz, skip and natural hand-over both load Ruined Village.
Unverified: HD textures in the render, fullscreen / DLSS displays, other refresh rates. See
[intro movie](../journal/2026-11-09/ootf-intro-movie.md) and `reference/modding.md` ("Intro movie").

**November 9 intro music:** "Psygnosis Presents" is the engine-rendered Intro 1 scene (the install has no movie file), nothing
new found in it. The title menu played the Drakan 2 SeaHigh GUI track instead of the OotF theme with `game = d1`: the runtime
refused the title redirect because the guest requests descriptors with length 0xFFFFFFFF (fixed in the executable). Ruined Village scene 381's cue 6
played over the level's loop 8 (the PS2 plays cues on top of loops, sequence messages do not reach them): the runtime now serves
silent chunks for every track but the newest in a campaign, as the PC replaces the playing music.
See [intro music](../journal/2026-11-09/ootf-intro-music-and-title-redirect.md).

**November 8 Drakan 2 characters and weapons:** Wartoks, orcs and the spider (already played by the disc classes) now keep
the disc's own animations by default, and plain weapons (swords, maces, axes, hammers, clubs, the long bow, the arrow quiver)
use the disc weapon model with their OotF damage (`--ps2-models 0`, `--enemy-animations ootf`, per pair in the editor's import
dialog; formats `d18eb87`). Rynn and Arokh were already the disc's. Seen in game: Drakan 2 orcs and Wartoks walk, attack and
die lying down, Rynn holds and swings the Drakan 2 long sword, the Ruined Village opening and the Islands arrival on Arokh
play. See [Drakan 2 characters and weapons](../journal/2026-11-08/ootf-ps2-model-substitution.md).

**November 9 spider-like creatures:** the converted-model Scavenger could not be hit by melee from most sides while the disc Scavenger class is (damage in 11 of 16 close cells from seven bearings on the default pack); it now plays as the disc Scavenger by default (formats `a06ddb0`, opt out per pair). The disc Spider is hit but takes 1 to 2 of 15 hit points per blow (its own class); Ticks keep their PC model. See [spider melee hits](../journal/2026-11-09/ootf-spider-melee-hits.md).

**November 9 scene Wartoks:** the cut-scene Wartoks (`Wartock Leather / Chain / Plate`, class 17 props) now use the disc Wartok model and rig by default (formats `d052dd4`, opt out per row): their PC clips are retargeted onto the disc rig, unmatched disc joints keep a native pose, and scene channels are remapped (spear: PC 9 to disc 10, left hand: PC 12 to disc 14). Seen in the Ruined Village opening (bridge, impale); see [scene Wartoks](../journal/2026-11-09/ootf-scene-wartoks.md).

**November 9 scene orcs and the scene-creature audit:** every creature actor of every OotF scene was audited; the cut-scene orcs (`Heuge Purple / Yellow`, class 17, Ruined Village 1.1 / 1.2a and Volcanic) were the one Wartok-or-orc class still on a converted PC model, and now use the disc Purple / Yellow Orc model and rig by default (scene rows, opt-out per row). Scene Sailors, Death Mages, Ebon Knights, Succubi and villagers stay PC models (no disc counterpart or under half of the joints match). Seen in the Ruined Village opening; later-level scenes are trigger-based and were not reached. See [scene creature audit](../journal/2026-11-09/ootf-scene-creatures-audit.md).

**November 8 Drakan 2 textures:** where Drakan 2 has a texture for the same kind of surface, the converter now copies the disc
texture instead of the OotF one (on by default; `--ps2-textures 0`, per pair in the editor's import dialog). Table: tropical plant
fronds, pine trunk and log bark, old tree and oak bark (formats `1e12669`). Ruined Village shows Drakan 2 pine trunks with it and
the OotF ones without; the copies keep the Drakan 2 HD textures. Tree canopies, palms and terrain have no Drakan 2 counterpart.
See [Drakan 2 textures](../journal/2026-11-08/ootf-ps2-texture-substitution.md) and `reference/modding.md`.

**November 12 texture quality next to the PS2 art:** every converted texture already gets a Real-ESRGAN HD image like the
disc's (all 3,645 pack textures are in the owner's `textures/`, and every converted texture drawn in Ruined Village, Grotto
and Islands was replaced). The gap is the PC source: half the disc's texel density on terrain (64 against mostly 128 px per
cell) and props (0.54), about 0.8 on characters; the converter loses only 34 to 47 dB of colour to 8 bpp. The HD images
were made from the wrong picture: the 128 px smooth enlargement of the 64 px ground tiles, row-doubled copies of 50 squared
textures (stair steps in the HD wings), and for 1,075 of 1,080 quantised textures the 8 bpp picture. With `--hd` the
converter now hands the HD step the PC pixels before any resize, and `hd_pack.py` upscales a smaller ground tile twice
to the guest size x4; pack files are unchanged. Needs the HD step rerun after a conversion with `--hd`. See
[OotF texture quality](../journal/2026-11-12/ootf-texture-quality.md).

**November 8 cage collision:** Rynn could walk into converted cages (shown on Volcanic's two; Wartok Canyons and
Grimstone Mines use the same models). The PC collides them through one sphere or boxes listing the bar polygons; on the
PS2 a prop's sphere does not stop her and she pushes 100 to 150 units past listed bars. Cage models now get one solid
box without polygons, the disc's usual prop form (formats `246d61e`): with Volcanic's two cages lowered to her body in
test packs, the shipped pack let her up to 126 units into one and 30 units from the other's axis, the fixed one stops
her 359 to 450 units from either axis on every side walked. Found on the way (formats `c6ad739`): the display-list
upgrade of rigid props had rebuilt their collision lists by polygon centre, dropping 74,147 list entries from 303
campaign props and the blocker boxes of 83 (Dragon Platform, crates, the lock); the PC lists are now mapped through.
Open: the user's spot was not walked in place (the cages hang above the floor); other sphere-bounded props such as the
Arokh braces are unchanged; needs a regenerated pack. See [cage
collision](../journal/2026-11-08/ootf-cage-collision.md).

**November 8 pink terrain:** distant converted terrain drew pink (Wartok Canyons' cliffs and peaks at the start). Packs shipped
the template's splat TXD, whose texture n is Surdana's baked far texture of terrain group n with a magenta filler in its
unused area; converted slots sampled it (12 to 35 % of drawn cells per level landed on the filler, all other far terrain showed
Surdana's colours). The converter now writes each level's own far textures (formats `3e7d62f`); 0 pink pixels in Intro, Ruined
Village, Wartok, Islands and Volcanic with HD on and off (Wartok before: 4,939). Needs a regenerated pack. See
[pink terrain](../journal/2026-11-08/ootf-pink-textures.md).

**November 8 Rynn in scenes:** in converted scenes Rynn slid in a standing pose, her drawn sword hung from her head through
her neck, her boots sank below the ground while the scene expected a crouch, and her mouth never moved. The player's scene
clips named ids of the converted PC Rynn's ADB instead of the PS2 Rynn's `SYSTEM.ADB`; they are now retargeted onto the PS2
rig as new `SYSTEM.ADB` clips. Attach points and animation channels are skeleton channel indices and are now remapped by joint
name (PC 4 `Rweapon` is PS2 21). Converted speech gets decoded-format 0x0305 lip-sync tracks and the player's speech the
lip-sync bit 0x20. Verified in the Ruined Village opening (walk, stance, sword in hand, feet on the ground, lip controller
bound in RAM). Follow-up: the Intro's Rynn prop is now the PS2 Rynn too, the scene sword keeps the PC grip (weapon joints
keep the PC orientation in the retarget), lip tracks are scaled to the disc's speech level, and the jaw is seen opening and
closing in captured close-ups; the in-game sword grip is correct. Needs a regenerated pack. See
[Rynn in scenes](../journal/2026-11-08/ootf-rynn-scenes.md).

**November 9 Rynn rig audit:** the PS2 Rynn's 24 finger joints have no PC partner and sat on their open bind pose in every retargeted scene clip; they now take a native armed clip's grip (formats `a60809d`), both hands close on the scene sword. Hair (graded-alpha cards fixed in `956e798`), Julian's start pose in 1.3 and the scene-end handoff were checked in frames and are not converter faults. `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` now also works for campaign starts, so the Ruined Village opening can be captured whole. See [rig audit](../journal/2026-11-09/ootf-rynn-rig-audit.md).

**November 9 Rynn hands and hair, third pass:** the off hand floated a palm beside the sword hilt because the PS2 arm is not the PC arm; where a scene clip holds the left hand at the sword the retarget now solves the arm onto the native two-handed grip (formats `HandHold`, both hands on the hilt in the 1.1 crouch). Gameplay hair matches Drakan 2 (same mesh, seen from a high camera); the horizontal kinked ponytail in scene crouches is not clip data (three different hair key sets look the same), so the engine drives it: open, needs hair matrices read from RAM. See [rig audit](../journal/2026-11-09/ootf-rynn-rig-audit.md). Needs a regenerated pack.

**November 9 Rynn's ponytail, seventh pass (fixed):** the bar behind her head in scene 1.1 (crouch, lunge) was the engine's
ponytail cloth simulation never running, not clip data. The Intro's last scene puts Rynn on Arokh's saddle and the level ends
with her there; the handle's "attached" bit (`+4` 0x80) survives the level change, and the skinning pass skips the hair
simulation of an attached object, so its 18 simulated vertices were drawn at their raw rest positions for the whole Ruined
Village (scenes and gameplay). A scene that holds the player to the level's end now detaches her from whatever still carries her
at its last action (formats `fa98a83`, `detachHeldPlayer`); scenes that hand her back mounted are unchanged. Seen in frames
after the Intro: the ponytail hangs and swings in the crouch and the lunge. Needs a regenerated pack (and a new game: a save made
in Ruined Village after the old Intro may keep the stale bit, not tested). See [rig audit](../journal/2026-11-09/ootf-rynn-rig-audit.md).

**November 8 grunt poses:** killed Ruined Village grunts (PC orcs retargeted onto the PS2 Purple / Yellow Orc rig) stood back
up because every converted clip was written looping (PC clips have no loop flag); retargeted clips now take the flags of the
disc clip they replace and converted creatures' death clips play once. Seated, dice and lookout grunts hovered because the PC
orc's pass-through `root` joint, which lowers the body, had no PS2 partner and was dropped; it is now folded into `hueges`, and
converted root tracks keep absolute positions. A grunt killed with the sword was seen lying on the ground; seated grunts were
checked offline only. Creatures placed dead in a level (a Ruined Village grunt, dead slaves) now lie in their death clip
(seen in a frame). The spear grunt's arm hidden from behind matches the original Drakan 2 Purple Orc (the trident stance holds
the left forearm in front of the body). The arm that was really missing belonged to the converted scene orcs: a PC skinned
model's last channels (their right forearm, the Leather Wartok's right hand, a Wartok shin, the Scavenger's forearm) were cut
off with the stump caps miscounted; fixed and seen in the ambush. The scene Wartoks' head sits low between the shoulders by
design. Needs a regenerated pack. See [grunt poses](../journal/2026-11-08/ootf-grunt-pose-fixes.md).

**November 14 dead bodies standing upright:** the slaves placed dead in Wartok Canyons and Grimstone Mines stood upright
holding their pickaxe. Their converted class took the standing pickaxe idle as `Idle Death` and `Sleep Death` (template
death fields the PC class leaves empty got the Base Idle stand-in), and that idle was written to play once, so living
slaves froze after one swing. Empty death fields now take the creature's own converted death clip (formats `0fdd0a8`; it
changes only the three slave classes). Seen lying after the fix and through walking over, fighting beside and an F5 / F8
reload in Wartok Canyons, Grimstone Mines (two slaves) and Ruined Village (the dead orc, unchanged). All eight creatures
placed dead and the body props are listed in [placed corpses](../journal/2026-11-14/ootf-placed-corpses.md). Needs a
regenerated pack.

**November 8 level map:** the map screen of converted levels showed Surdana's picture (the converter never set the Level
Properties `Map Object`) and no quest markers (PC markers have no coordinates; they were written as journal-only). Each campaign
level now gets a Map class with its `Dragon.rrc` parchment (5 tiles joined to 640 x 480) and an estimated calibration, quest
markers are placed from their world position (type 2), and the import dialog has a "Map screen" section to pick another picture
or adjust the covered area with a live preview. Ruined Village shows its parchment with Rynn's arrow; levels that start mounted
(Grotto, Grimstone Mines, Wartok Canyons) cannot open the map at the start. Markers: PC markers whose class is the generic "Map
Trigger" carry only mission text bitmaps, so Ruined Village and Grotto have none. See [level map](../journal/2026-11-08/ootf-level-map.md).

**November 8 Ruined Village props and ambush:** converted rigid models of drawn classes other than generic props
(mechanisms, boulders, keys, locks, flames, items) had no strip display list and drew nothing while keeping their
collision (the village waterfall rocks, Dragon Falls rocks, swing blades, temple floor and stairs: 103 models); every
native model of those classes has one, so they now get it. A display-list model with a non-square texture also draws
nothing: the cart by the small house was invisible, so its barrel and crate floated; such textures are written square
(also the wreck carts, chests, ruined tables). The outhouse door, which the PC opens only with the trigger that shows its
hidden orc, opened by PS2 proximity before him; ambush doors now keep Auto-Open off. Verified before / after in game. See
[Ruined Village ambush and props](../journal/2026-11-08/ootf-ruined-village-ambush-props.md).

**November 9 Intro owl:** the perched owl showed brown and dark triangles over its chest. Its texture has graded alpha (selector 8, blended, no depth write) and the mesh is overlapping feather cards, so hidden cards painted over the body. Skinned-model textures now cut alpha at 50 % (depth-writing selector 1); verified in game on the Intro 1 close-up. Needs a regenerated pack. See [owl feathers](../journal/2026-11-09/ootf-intro-owl-feathers.md). Follow-up: the owl in motion matches an offline render of the PC model and rig (horn cards, flaring ruffs and the chest seam are PC geometry; no faces lost), and the Intro hand-over timer went from 44 s to 41.4 s (the last scene ends at 41.2 s; it left the wide owl shot standing for 2.8 s); the intro movie needs a re-render. See [owl follow-up](../journal/2026-11-09/ootf-intro-owl-feathers.md).

**November 8 Intro graphics:** the Intro showed mirrored Surreal Software logos under the Psygnosis owl and "a Production"
overlaid with its own mirror image. The PC culls back faces and the PS2 draws converted models from both sides; the title
cards are single-sided planes and flattened boxes. In cinematic levels the converter now samples every played scene: polygons
the camera never sees from the front are left out, and models turned away from the camera are hidden by the converted scenes
while they are. Seen in game: Intro 1 shows only the owl, plate and "Presents", "Production" reads correctly, Intro 3 is
unchanged. Model faders still pop instead of fading. Needs a regenerated pack. See
[Intro graphics](../journal/2026-11-08/ootf-intro-graphics.md).

**November 9 opening cut and impale:** Rynn's lying pose in Ruined Village scene 1.1 hung 450 to 535 units over the ground (the PC
keys place her standing origin 500 up, a lying pose has its joints near the origin): scene keys under a lying clip now rest the body
on the terrain. The Wartok's victim in scene 1.2a hung at the Wartok's hand; the PS2 attach puts the carried origin on the joint and
ignores later keys, so the converter replaces such attaches by world keys that lift him onto the spear tip (seen in game). See
[opening cut and impale](../journal/2026-11-09/ootf-opening-cut-and-impale.md). Needs a regenerated pack.

**November 9 Ruined Village Book backstory (on by default):** Atimar's book now plays the PC level's 250 s backstory (nine paintings,
camera path, narration) full-screen, then hands back to Ruined Village with the HUD and control. The thin strip it showed was the
runtime's loading-screen hold: the Book has no terrain, so no world camera ever replaced the loading screen's GUI packet, and the
presenter kept covering the top 85.5 % of each frame with the last menu frame; a world phase now ends that packet (runtime `gsr`).
The scene held Rynn and the camera to the level's end, which left her held (no HUD, no control) after the hand-back; the converter
now releases them at the scene's end (formats). `OOTF_BOOK_SCENE=0` restores the 28 s empty Book. Verified on a full 23-level pack
through the real path. See [Book backstory](../journal/2026-11-09/ootf-book-scene.md). Needs a regenerated pack and runtime.

**November 12 Volcanic crash and the rift box (issue 5):** the issue's "the rift box does not open after the Runeblade,
being bisected" was stale: the 2026-11-08 bisect had already shown those runs never took the blade (the box worked on 6, 7
and 8 November). But every pack converted since 2026-11-09 09:12 (formats `ac234e9`, the scene orcs on the Drakan 2 orc model),
the installed one included, crashes Volcanic at load in most runs (null call in object teardown at the first kill-list
flush), so nothing in Volcanic could be played. Cause: the scene creature lookup in the sequence stage matched class
objects by identity, which only hit in the level the class plan kept (Ruined Village); Volcanic's Heuge Purple props were
driven by their PC clips on the 55-joint disc rig, and its Heuge Yellow actions were dropped. Fixed in formats
(`sceneTockLookup`, matching by class signature); the pack changes only Volcanic's two sequences and Volcanic loads 6 of 6.
Played on the fixed pack (own play folder copy, real `drakan.ini`, windowed, god mode, placements before each trigger):
mounted in rift box 2318 without the blade nothing opens (also 200 s inside it); after the arrow melt Rynn takes the blade
with E (Timer 2331 runs), remounts by walking into Arokh, and the box gives the message box, the save screen and Rift
World. The same process went on through the tunnel, the four heads, the mirror scene and the campaign end card to the
title (`proof4`, no crash), so the "Volcanic through Rift World to the credits in one process" item holds on the fixed pack
(it had also passed on 2026-11-08, `full2`); still with placements and god mode, not a hand-played run. A level-change
arrival into Volcanic crashed the installed pack about 2,200 level frames in (2 of 2) and runs on the fixed one. From the 2026-11-08
notes, not replayed here: Delon was beaten on foot with the carried Volcanic blade without god mode (he hit Rynn twice for
15, spell or melee not separated), and "E does not call Arokh" was the test tapping E (calling is E held past 0.5 s;
flying off is his native follow distance). Needs a regenerated pack. See
[Volcanic rift](../journal/2026-11-12/ootf-volcanic-rift.md).

**November 12 Scavenger melee ("flesh eaters we cannot hit"), not reproduced:** the creature is the Scavenger (19 in
Ruined Village, played by the disc Scavenger class since 2026-11-09; the installed pack and the owner's saves have it).
On the owner's executable, pack and saves a shown Scavenger was hit and killed in every scripted condition tried
(Atimar's Blade alone: 31 hits in 49 clicks in reach, 4 of 4 killed; the same at 15 and 30 Hz; with a 30 degree facing
error; with a delayed, noisy aim; in a fresh New Game; ambush Scavengers after their trigger). 7 of the 19 start hidden
until a trigger shows them and take no damage while hidden, but they are not drawn then. No combat or converter change.
`DRAKAN_MELEE_TRACE=1` now logs each swing and contact as the engine runs it, to capture the case in real play. Open:
what the owner did differently (hand play, full screen frame rates and co-op were not tested). Follow-up without god mode: against the hill Scavengers, which attack at once, Rynn with Atimar's Blade died in 5 of 5 fights from the owner's save and in 4 of 8 from a fresh game; their hits (5 points about every 2 s) cancel her swings (hit during 38 of 61 attack spans, 10 of those dealt damage) and the dagger needs eight hits. Carrying the PC flee limit onto the swapped class does nothing (the Scavenger engine class does not run away: 4 of 4 probes, and no disc class of it sets the limit). In the converter now: each Scavenger gets the 15 hit points the PC one loses before it flees (35 - 20) instead of the disc 40, so three dagger hits end the fight as they drive the original off; fresh game, same driver: 10 kills and 1 death in 11 fights against 4 and 4 in 8 (median time to kill 8.3 s against 23.1 s). It reaches new games only: a save restores its creatures with their saved health. Needs a regenerated pack. See
[flesh eater combat](../journal/2026-11-12/flesh-eater-combat.md).

**November 12 trap ropes and mace heads:** the swinging blades of Ruined Village (5) and Wartok Canyons (2) hung in the
air: the PC `Rope` class (its own 8.1 unit rope mesh, scaled per placement) was name-mapped to the disc's `Rope`, whose
model is a 0.2 unit stub; `Cable` (Grimstone's crystal) and `Static Rope` (Wartok) got a 5 unit disc rope pointing the
other way. Rope classes and classes placed with a rope mesh now keep the PC mesh (formats `15c2899`). With the rope drawn
the blade trailed it by up to 16 degrees and the swing sat off-centre: the engine binds a rope's carried objects after
its first step and turns the rope by small-angle steps, which only holds for the console's steady tick; the rope update
now runs with a 0 s tick until the bind and in steps of at most 1/30 s (runtime, PAL, `DRAKAN_ROPE_TICK=0` = native):
rope and blade agree to 0.0 degrees and swing -44 to +42. The Mace showed its handle only: weapons shown with a disc
model lost the disc weapon's child part (the class record's auxiliary reference), so also the Battle Hammer's head, the
Battle Axe itself and the sheen layer of five swords; swapped weapons now carry it. Seen in game before and after at the
owner's settings (3 to 9 fps under shared GPU load, so not dense in time). Needs a regenerated pack, the rebuilt
executable and an HD step for the rope texture. See [trap ropes and mace heads](../journal/2026-11-12/ootf-axe-trap-ropes.md).

**November 12 church stair scene (the Book): framing and subtitles.** The owner's "cinematic" on entering the church
stair is the walked exit into the Ruined Village Book and its 250 s backstory. At 16:9 it showed black beside the paintings
(they are flat pictures framed for the PC's 4:3 screen at field of view 80; the port widened the view), and it had no
subtitles, like every OotF scene: the PC game has no subtitle text, and the engine only subtitles actors with a speech
controller. Now the pack marks the Book `level_aspect = 4:3` and the runtime pillarboxes such a level; a subtitle file made
on the player's machine from the install's voice clips (`analysis/scripts/ootf_subtitles.py`, 298 lines here) is stored in
the clips' sound descriptors as timed pages (`ootf-convert.ts --subtitles`), and the runtime shows them with the narration
band for any actor. Seen on a full pack at 3840 x 2160 and in a 4:3 window through the walked exit (Rynn placed at the foot
of the stair: the floor needs the book), and in the Ruined Village opening. Open: the PC exit sequence "Book 1B" is not
converted, the text is unreviewed machine transcription, the builder does not run the tool yet. Needs a regenerated pack
and the new exe. See [church stair scene](../journal/2026-11-12/ootf-church-cinematic.md).

**November 12 church stair, follow-up: the walk-down, wide screen, subtitle review.** The PC exit scene "Book 1B" (Rynn
walking down the stair to the book) and the entrance scene now play: what fires such an exit starts a Stomp Player with
both scenes and a Timer that fires the exit when they are over, and the exit can no longer be touched (formats
`ootfexitseq.ts`). Played at 3840 x 2160 as one chain: the book taken in the priest's house (Rynn placed inside; the
pickup needs no key press and was never broken), placed outside the church, walked in, the book on the altar ("Book 1A",
the floor slides off), onto the stair, the walk-down, the save, the Book with subtitles, back at the foot of the stair with
the HUD. Only three transitions of the campaign name such scenes: this one, the Book's own (its backstory, already played)
and Volcanic's rift exit, whose scene "9.9" stalled the game when relayed and is left out (the Volcanic level file is
unchanged). Wide screen for the Book: a `4:3 fill` level setting now exists (the authored width fills the window, a
quarter of the height is cut, 21:9 and 32:9 get a 16:9 picture with bars) but stays off: it cuts heads in several
paintings; a side fill from the picture itself looks best and needs a present pass in the renderer backends; mirrored
edges look wrong. The subtitle tool keeps a corrected file, writes a review list with confidences and names the speech
model before any download. Same journal note, sections 8 to 10.

**November 13 the Book scene skipped, and subtitles after a load.** Owner: Escape on the Book scene left Rynn "in an
empty world", and reloading broke subtitles. The engine's skip runs a skippable scene (PC flag `0x40`) to its last
action at once; the Book's exit waited on a level Timer of the scene's length, so a skip released Rynn into the Book's
grey void for the rest of the 250 s. The Book's scene now sends the exit its message at its last action itself (formats
`ootfseq.ts` `endMessages`): Escape, Enter, Start or Cross hand back to the foot of the church stair at once, as a watched
scene does. Reloading: the save carries the sound manager's handle counter, so after loading the exit save the Book's
clips came back with handles the subtitle tracker had already seen and the first nine lines had no text; loads now
reset it, and a clip's page ends when its sound stops. The walk-down and the save box stay unskippable (Escape opens the
pause menu / does nothing). Follow-up the same day: Back on the save screen at that exit, or loading a save made during
the walk-down, used to leave the Book unreachable (the relayed exit is a point and its Timers are destroyed once fired).
Now the entrance scene fires the exit itself, a re-offer box at the foot of the stair brings the prompt back after a
back-out (as often as needed, also after a save and load), and the runtime closes the exit in the level-change save, so
after the Book it is never offered again (manifest `one_way_exits`). Needs the new exe and a regenerated pack (a new campaign game). See
[Book skip and reload](../journal/2026-11-13/ootf-book-skip-and-reload.md).

**November 12 the tomb's push puzzle (issue 5):** the owner's "cannot push the objects in the lair" is the tomb of the Soul
Crystal in Ruined Village: a block (Rigid Body, Push Mode Slide) is pushed onto one of three pressure plates, then the plate
beside the statue is pressed and the Rising Floor Ring bridges the pit to the crystal. In the pack the block lay under the pit
in every save of the owner (the PS2 Rigid Body rests on terrain only and the tomb floor is a model), and the plates' detector
stand-ins sat at the plate objects' origin behind the pit, so nothing could be done and the crystal was out of reach. The
converter now turns such a block (linked to plates, standing on a model) into a Mover with a solid box that slides onto the
nearest plate tile when Rynn walks into it, and counts its arrival and the plate that raises the block's plate on the PC
(formats `d189acd`, `ootfpush.ts`). Played on a full pack in an own play folder without god mode: block onto the tile, F5 /
F8 in the middle (block and Rynn restored), plate, ring up; then over the ring to the crystal, and the lair scene from a
placement (`proof1`, `proof2`). It is a stand-in, not a physics push: a fixed slide, static plates. Open: a converted Rigid
Body does not move on terrain either (the Islands crate is most likely fixed), native Pressure Plates did not sense Rynn, the
other plate puzzles (Islands, Rift Crystal) do not enforce their order, old saves keep the old objects. Needs a regenerated
pack and a new game. See [tomb push puzzle](../journal/2026-11-12/ootf-lair-push-puzzle.md).

**November 12 the tomb's push puzzle, second pass (issue 5):** the stand-in above is gone; the block is a Rigid Body Rynn
pushes by walking into it and the plates are native Pressure Plates (formats `21128e2`). The three open points had these
causes. A converted Rigid Body never moved because the first pass gave its model a solid box: with box bounds the engine
sets no contact sphere (0x458d88), the body's contact query never clears its "just woken" bit and the tick never takes the
push velocity; the PC sphere bounds are kept now. Native plates "did not sense Rynn" because the first pass looked for the
tiles a quarter turn away: a placed model turns by the stored Y degrees minus 90, so plate 992 lies north beside the block,
991 west and 988 east; the plates also need a box with height. The block rests on an invisible terrain layer at floor
height that is named right after the room's layers in every layer list, so Rynn is filed on it too (the layer lookup
0x14b6b8 walks a list upward and never reached a layer appended at its end). Played on a full pack without god mode from
one placement inside the room (`evidence/proof3`): wrong steps make the plates chase each other back to the start, a short
push stops with Rynn, the block is pulled onto plate 992 and sinks with it, F5 / F8 restores block and plates, 991 then 988
raises the ring, over it to the casket platform. All 42 plates of the campaign are native now; a press was seen in Rift
Crystal, Islands (with the next plate rising) and Wartok Canyons, not in Grotto. The Islands crate is pushable (829 units in
one push). Not done: the walk in from the village and on to the lair, the crystal pickup seen, sound heard. Needs a
regenerated pack and a new game. Same journal note, section 7.

**November 12 white squares in the tomb (issue 5):** the "white squares floating looking like trigger points" in the same
room are not trigger detectors. They are the room's three dynamic lights: a PC dynamic light keeps the visible object flag,
and its converted class is a clone of the System light class, whose small white marker cube has a draw list. On the disc no
placed dynamic light of a class with that model carries the flag, so the converter now clears it (formats `db1a9c3`,
`ootfmarkers.ts`; 22 lights in seven levels, one byte each, nothing else in the pack changes). Detectors need nothing: the
disc's detector boxes have no draw list, and a detector entity was not drawn in game even with a drawable box and the visible
flag. Played before and after on full packs: the tomb without cubes, its push and plate detectors, the lair scene trigger,
the walk-in exit box and the plate stand-in of Wartok Canyons all still fire. Open: only the tomb's lights were looked at,
the other 19 were not visited; a weapon effect's light class with the same cube was not looked at. Needs a regenerated pack
and a new game (the game refuses a save made on another pack build). See
[detector squares](../journal/2026-11-12/ootf-detector-squares.md).

**November 14 item requirements:** the Chain Mail of Ruined Village could not be worn ("Exp. Level Needed 3",
"That can't be used right now." on the modern screen, greyed on the native one; Rynn is level 1). The PC game has no
requirement on any item, but converted armour kept the Drakan 2 template's Skill Level, which the engine checks against
the character level (Chain 3, Scale 4, Plate 6, Banded 7, and Dragon Armor mapped onto the disc class itself, 8). The
converter now writes 0 for armour as it already did for weapons and bows, and clones a same-name restricted disc armour
instead of reusing it (formats `ootfclass.ts`). Played on a fresh campaign game: Chain Mail, Atimar's Blade and a Battle
Axe equip on both screens, Rynn wears the chain armour, Armor Rating 20 (PC 0.2). Needs a regenerated pack and a new game.
Open: damage reduction in a fight and the other armours in game. See
[item requirements](../journal/2026-11-14/ootf-item-requirements.md).

**November 9 builder:** the guided builder (`DrakanPC-Setup.exe`, `Build-Drakan.cmd -OotfSource <path>`) can now convert Order of the
Flame from an install folder, disc folder, `.mdx` / `.iso` or zip and install the pack, and make HD textures for the disc and the pack,
with an estimated time per step. See [builder extras](../journal/2026-11-09/builder-extras.md) and `BUILDING.md`.

## What works

* Converter `editor/scripts/ootf-convert.ts` (`--campaign 1` for the campaign): terrain, textures, models, creatures (skinned, retargeted
  to PS2 rigs), classes, puzzles (plates, doors, counters), exits, texts, music, sounds. Release packs of 2026-11-02: campaign
  `ootf-levels` (13 levels, 9119 files) and arenas `ootf-arenas` (10 levels, 158 files, one `maps/*.ini` each).
* Class coverage (issue 3): with the Surdana template no placed PC class is unmapped in any of the 23 levels, for the English
  and the German install alike. Quickening Effect has a Particle Spawner stand-in, Brick17 and the arena Ambient Sound map;
  fog and sky changers (31), props whose model the install lacks (8) and the arena pads and crystals (108, exported to
  `maps/*.ini`) are kept out on purpose with a note each. See [class coverage](../journal/2026-11-08/ootf-class-coverage.md).
* Game choice at the title ("Choose your game": Drakan / Drakan: Order of the Flame, `drakan.ini [general] game = ask|d2|d1`), a
  campaign-only New Game that starts at `OotF 00 - Intro` (no Drakan 2 intro), separate save cards, and the Order of the Flame picture as the title and post-title loading art instead of the disc art ([title art](../journal/2026-11-08/ootf-title-art.md)).
  A regenerated editor pack from English D1 data also matches the German-target run showing the owl, logo and landscape
  before Ruined Village. See the [timed capture review](../journal/2026-11-05/issue-1-cliff-intro-city.md). Since November 8 the
  Intro draws no HUD (its scenes hold the synthetic player, as the disc Intro's scene holds Rynn) and converted scenes play at the
  PC field of view of 80 instead of 40, so the owl shot shows the owl's head and Ruined Village scenes are no longer cropped
  ([Intro HUD and owl framing](../journal/2026-11-08/ootf-intro-hud-and-owl-camera.md)). The Intro no longer starts with 12 s
  of black: the near wall of its inward-facing "Black Background" box (culled on the PC, drawn on the PS2) is left out, so
  the Psygnosis owl flies in from the first frame. The model faders (Psygnosis plate, "Presents", the DRAKAN title letters,
  Surreal text) now convert as props and show
  ([Intro black start](../journal/2026-11-08/ootf-intro-black-start.md)).
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
* Animated object textures play (waterfalls, torch and sconce flames, the Surreal logo): every frame is listed after the first in
  the model's texture list as on the disc; the Intro waterfall is shown flowing in game. See
  [waterfall flow](../journal/2026-11-08/ootf-waterfall-flow.md).
* Water: Rynn swims in converted water (water layers keep the PC water vertex kinds, moved into the PS2 range) and the
  surface draws with the engine's own water material (PC liquid textures map to the native Proc Textures water / lava entries,
  water layers carry no chunk data). Hole vertices of water layers are kept under the water and the bank (they drew as sheets in
  the air). Mountain water uses Surdana Water, the Islands the plain Water material; waterfalls use the Drakan 2 Surdana falls
  texture. Verified swimming in the Ruined Village ocean; Islands and Wartok water seen from Arokh. See
  [water](../journal/2026-11-08/ootf-water.md). Every layer now lists the water layers within 96 cells in its `visible` list (the PC lists were made for a short draw
  distance, so the Ruined Village sea ended in an edge against the sky); see [sea and sky](../journal/2026-11-09/ootf-sea-sky.md).
* Floating water (2026-11-14): the PS2 water pass draws every cell of a water layer, holes included, so PC hole cells stood
  as sheets in the air where a water layer ends at a drop, half-water shore cells let Rynn tread water above the sea, and 66
  all-hidden water layers (Volcanic, Rift World) drew where the PC shows nothing. Hole vertices over a drop now hang far down,
  shore cells count per triangle, water layers are cut to their water and the hidden ones become empty floors: the sheet area
  goes from 583 to 49 cells over the campaign, and the temple pool under the church draws its surface around Rynn again (the old
  rule's dropped vertices hid it). Swim, dive, surface and climb out played in Ruined Village, Alwarren and Islands Night; needs a
  rebuilt pack and a new game. See [water sheets](../journal/2026-11-14/ootf-water-sheets.md).
* Water, round 5 (2026-11-14): the 49 sheet cells left are water faces at real edges of the PC water (the Islands sea
  behind a waterfall model, against a rock wall, over a lower slab; the Rift World pool in the void) and bank slivers; the
  water pass has no per-cell test in its drawing loop, so the 0x40 / 0x80 triangle bits cannot hide them, and other hole
  heights (flat, down to the floor) looked the same at the Islands lip. No water converter change. Swim, dive and surface
  played in the Islands (also walked into the sea with no placement), Wartok Canyons (cave pool, three rivers), Volcanic
  (two places) and Islands Night; the Rift World pool has PC kind 0 and Rynn falls through it. The Islands freeze on foot by
  the east shore is the terrain chunk overflow of play check B and is gone with formats `e671be8` (7 of 7 runs). See
  [water round 5](../journal/2026-11-14/ootf-water-sheets-2.md).
* Holes in cave walls (2026-11-14): pale lilac (Mines) or sky blue (Grotto) triangles showed through walls and ceilings.
  The PC gives each terrain triangle its own texture and the converter made a cell with one hole triangle a whole hole
  (11,400 such cells over the campaign). Floors and ceilings now keep the cell and set the disc's per-triangle hole bits
  0x40 / 0x80; every 3 x 3 block of cells is also kept within the 8 textures a chunk can blend (crowded blocks drew flat,
  lighter patches; 1.2 % of cells change texture). Seen fixed by walking and turning round in the Mines slave room and a
  Grotto ice cave; needs a rebuilt pack and a new game. See [Mines triangles](../journal/2026-11-14/ootf-mines-triangles.md).
* Drakan 2 with both packs installed still boots normally (choose Drakan; `Level 0 - Intro` then `Level 1A - Surdana`; no OotF leakage).
* No spells in the OotF campaign (2026-11-14): the pack manifest says `campaign_spells = false` (the converter writes it), so
  while Order of the Flame is the current game there is no Spells tab in either inventory screen, no mana orb or mana bar, and
  Circle with empty hands parries instead of casting; Drakan 2 in the same exe keeps all of it. Seen before and after in both
  inventory styles and both pause menus, through F5 / F8; needs a rebuilt exe and a pack with the key (an older pack keeps the
  spells). See [no spells](../journal/2026-11-14/ootf-no-spells.md).
* No spells, part 2 (2026-11-14): both Skills pages list Melee and Archery only and take no point for Magic; the native tab
  row spreads its four tabs evenly (mouse boxes and glow follow); the Skills help box and the Circle line of the game's
  Controls screen use our own wording without spells; switching Drakan 2 -> Order of the Flame -> Drakan 2 on the title
  card in one session switches all of it. Needs a rebuilt exe only. See
  [no spells 2](../journal/2026-11-14/ootf-no-spells-2.md).

## Known gaps

* **Crash or hang in the Islands (and Wartok Canyons) when too much terrain is in view** (2026-11-14 play check): the
  terrain draw pages of the visible layers no longer fit in guest memory (`[memset] bad size ... ra=0x157d7c`, then a jump
  to 0 or a frozen frame). Converted levels have 10 to 30 times the terrain chunks of any disc level per visible list
  (Islands 5,656 against at most 524), because chunks with more than 8 textures are cut down to single cells. Also on the
  packs from before rounds 3 and 4. Needs a converter decision (fewer textures per chunk window, 10 to 20 % of Islands cells
  repainted). Islands Night, Rift Crystal and Rift World played without it. The Islands Ctrl landing always ends on one
  rock where Rynn is stuck on Arokh's back. See [play check A](../journal/2026-11-14/ootf-playcheck-islands-rift.md).
* German (or other localized) PC installs are handled from source review and synthetic tests only: header texts, Dragon.rrc
  ids, folder layout, Intro/Book timer lengths against a German dub and a German playthrough are unverified
  (`journal/2026-11-06/issue-3-localized-ootf.md`, "Still needs a real German install").
* Dragon breath in the air arenas draws a flame trail (host-spawned FX particles, `ootf-dragon-breath-visual.md`); it needs the FX
  resources in the arena level (true for the air arenas, other maps untested) and is not wired to the campaign dragon. Dragon health
  regeneration lifts the bar between hits (`ootf-dragon-pvp.md`).
* PC exit triggers: Islands Detector 4131 fires when Rynn is in it (the box stands under the bell tower's whirl doors, reached
  by the bell route, `bell-h`), Succubus `ship2` runs (`s10-11`); Volcanic Detector 2318 (mount only) does not fire mounted
  without the Runeblade and fires after the pickup (2026-11-12 `proof2`, `proof4`).
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
* Without spells the native Skills page keeps the empty strip and faint emblem of the third row (painted into its page art).

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
