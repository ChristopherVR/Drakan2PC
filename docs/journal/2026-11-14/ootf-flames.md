# Order of the Flame: Drakan 2 fire for converted flames, Drakan 2 explosion for exploding barrels

Owner requests (round 7): "use Drakan 2's flames instead of the OotF flame models" and, added during the round, "exploding
barrels in ootf don't show any explosion". Converter fix only (formats `673f5a4`: `ootfflames.ts`, `ootfps2model.ts`,
`ootfpipeline.ts`), two new categories in the Drakan 2 model table, each row switchable. Scratch, packs, runs and frames
are under `build/review/ootf-flames/` (not tracked); nothing from either game is quoted here.

## 1. Why the flames never burned with Drakan 2 fire

The 487 converted flames (engine class 20) are clones of the disc's RESOURES "Wall Torch" (the only class 20 template
with a model in the host ODB). Their records are the template's field for field: `Dynamic Light` "Wall Torch Light",
`Fire Particles` "Wall Torch Flame Gen", `Looping Sound`; only model and bounds differ.

Class 20 has no entry in the 155-entry class table (`0x4fca40`); it falls to the generic object switch `0x3a8580`
(jump table `0x548370`), whose case builds a 0x40 byte object with vtable `0x530678`. Read from the ELF:

* tick `0x366828`: when the generator handle (`+0x3e`) is empty, the object's flag bit 0 is set and the camera is
  within the engine's flame distance, it calls the start `0x3669f0`; when the handle is set and the camera is past that
  distance plus 2048, or the flag is clear, it calls the stop `0x366cd0`.
* start `0x3669f0`: spawns the light (field 0, handle `+0x3c`), the generator (field 1, handle `+0x3e`) at the object's
  position, and starts the sound (field 2) at 0.2.
* stop `0x366cd0`: removes light and generator, fades the sound.
* messages `0x367150`: 0x1001, 0x1007, 0x1087 start, 0x1002 stops.

So the light and the fire start together, and a missing fire cannot be a gate in the handler. A trace
(`DRAKAN_TRACE_FN=3669f0,366cd0,2cdc60`, run `runs/trace_off1`, lines in `runs/trace_off1_fn.txt`) at the two torch
stands of the Ruined Village shows, for every flame: start, generator event 1 (init), one event 4 (tick), event 2
(destroy). "Wall Torch Flame Gen" has `LifeTime` 0.3 s; every generator of a torch placed in Surdana or Ravenshold has 0
(forever). The generator dies at once, its handle stays set, and the tick (which only starts a flame whose handle is
empty) never starts it again. RESOURES "Wall Torch" is placed in no disc level we decoded (Surdana places WORLDCON
"OTF Torch" 30 times and "OTF Wall Torch" 8 times: Drakan 2's own Order of the Flame torches).

Proof of the cause: the same pack with only field 1 pointed at WORLDCON "Parts : Wartok Wall Torch Gen" (the generator of
"OTF Wall Torch", named from RESOURES as `.DB` index 6) has 2 generators and 8 particle emitters at the two torch stands
5 s after arriving, where the original pack has none (`runs/expA1` against `runs/trace_off1`).

## 2. What changed

**Flames** (table category `flames`, rows for "Sconce Flame" (also "SconceFlame"), "Torch Flame", "Standing Torch Flame",
"Hanging Flame", "Hanging Torch Flame", "banner flame"; all name WORLDCON "OTF Wall Torch"):

* `Fire Particles` becomes the `Fire Particles` of the row's Drakan 2 torch class, rewritten into the clone's database
  frame (`moveClassRef`).
* The model becomes the host's "nullparticle" (a two triangle card with a fully transparent texture that disc
  projectiles carry their particles on), with the bounds block of a disc class that uses it. The OotF flame card is no
  longer drawn. Hiding by clearing `FLAG_VISIBLE` was not an option: the tick refuses to start a flame whose flag bit 0
  is clear.
* Each placed flame moves to the foot of its OotF flame card (centre across, lowest point; `flameFoot`, placed with the
  object's scale and turn, `placedOffset`): the generator, light and sound start at the object's origin, and Drakan 2
  torches keep their origin where the fire burns. Standing torch flames move up about 1,260 units, sconce flames about 20.
  A tilted placement moves only when its foot is within 0.1 model units (Succubus Lair's 29 leaning sconces); two tilted
  standing torches (Islands Night 5296, Grimstone Mines 272) stay at their origin and burn at their base (warning).
* Light, sound, object flags, links and messages are unchanged. The light now sits at the flame foot instead of the
  torch's base point.

**Exploding barrels** (category `effects`, rows "Barrel (explode)" and "Barrel (Exploding)", Drakan 2 class MOUNTAON
"TNT Barrel"): the converted barrel's free `Smoke Generator` slot gets the disc barrel's `Explosion Generator`
("!! KA-BOOM Barrel Explosion !!": flash, fireball generator, staves, sound). A row field `"A>B"` copies Drakan 2 field A
into converted field B.

Why not replace the OotF `Explosion Generator` (tried, pack `fl3`): the converted OotF explosion ("exploding barrel exp",
staves plus a converted fire generator and smoke generator) draws only the staves, but it is what hurts: with it replaced
the orc standing at the barrels lived (40 hp before and after the blast) where the OotF explosion killed it (40 to -98);
with both (pack `fl4`) the orc dies as before (40 to -91) and the Drakan 2 fireball and flash show.

**Option**: on by default. `--ps2-model-map` rows `{"ootf": "<class>", "ps2": null}` turn a row off, the categories can
be turned off in the import dialog (now listing Flames and Effects), `--ps2-models 0` turns every Drakan 2 model off. User
rows: a flames row needs `odb`, an effects row `odb` and `fields`.

A pack with the flame rows off (`fl0`) is byte-identical to one built before this change except where the rows act: with
them on (`fl2`) only RESOURES.MOD (the flame cards lose their draw lists, 4.5 KB smaller), the 8 flame class records, 11
LVLs (object positions) and `mod.toml` differ.

## 3. Evidence (owner's ini: copy of `baseline-play` with the dist exe of 90dff87b, private ELF; direct level starts,
empty card, god mode unless stated)

Placed views, the same poses for both packs (`tools/flames.py`): before = `fl0` (flame rows off), after = `fl2`.

| Kind | Level | Frames | Seen |
| --- | --- | --- | --- |
| Outdoor torch stands | Ruined Village, 2417 / 2418 beside the impaled skeleton | `runs/fl1_crop.png`, `evidence/flames_zoom_pairs.png` | the tall flat OotF card is gone; Drakan 2 fire with glow and a little smoke burns in the bowls |
| Indoor wall sconce | Alwarren 1452 | `evidence/flames_zoom_pairs.png`, `evidence/ev2_check.png` | Drakan 2 fire in the sconce cage |
| Standing torches in a cave | Rift Crystal 97 / 99 | `evidence/flames_zoom_pairs.png` | Drakan 2 fire inside the cage at the top of the pole |
| Trigger-lit torch | Ruined Village 808 (initially off, linked from detector 2682) | `evidence/rv_trigger_flame808.png` | dark and no fire objects before; after walking into the detector with W: light, generator and 4 emitters (fl1), light only (old pack) |

Census 5 s after arriving (flame / light / generator / emitters within 3,000 units): every view has as many generators as
flames with `fl2` and none with `fl0`; lights are the same in both.

No "large fire" exists among the class 20 flames: every OotF flame card is 0.1 to 0.15 model units tall. The big fires
(burning flames, Alwarren magic flames) are other engine classes and were not changed.

Played with real input:

* Rynn stood in the torch stand fire (placed on the bowl, god mode off, 5.6 s each, `runs/dmg_fl1`, `runs/dmg_off1`):
  the health read from the stats block stayed 100 with both packs, and the health orb looks full. The read-back is not
  trusted (it also read 100 in barrel runs that showed the death notice), so "the fire does not hurt" is not proven.
* Trigger-lit torch: see the table.
* Barrel struck: Rynn walks to barrel 247 with W and swings (`tools/boom.py`, god off, `runs/boomhp_fl2`,
  `runs/boomhp_fl4`, `evidence/barrel_strike_before_after.png`): before, the barrels go up with staves only; after, a
  fireball and a white flash, then the staves. The orc at the barrels dies in both (40 to -98 / -91; one run each). The
  line "Press Enter to load the last saved game" shows in the runs of all three packs while Rynn still stands; not
  looked into.
* Barrel set off by a boulder: Rynn pushes boulder 366 with W; once it rolls on alone she is placed at the camp to watch
  (`tools/boulder.py`, `runs/bv_fl2_1`, `runs/bv_fl4_1`, `evidence/barrel_boulder_before_after.png`): before, staves fly;
  after, fireball, flash and staves. The boulder sets the three barrels off in both (3 to 0); in one of four earlier
  pushes on the old pack it rolled past them (physics, `runs/bould_fl2`).

## 4. Cost

Rift Crystal, standing torch 97 in view, 50 generators and 126 emitters alive in the level near Rynn with the rows on (95
flames within 60,000 units). `scripts/perf/scene_ab_bench.py` (turn lock, owner's ini, interleaved, `bench/rc-flames`,
`bench/rc-flames2`):

| Runs | Flames and barrels off (pack `fl0`) | On (`fl2` / `fl4`) |
| --- | --- | --- |
| frame ms, 4 quiet pairs | 6.85, 6.95, 6.94, 7.05 | 8.14, 8.15, 8.26, 8.26 (+17 to +19 %) |
| 1 % low ms (medians) | 8.9 / 7.8 | 14.6 / 14.7 |
| EE ms per world frame (medians) | 5.6 / 5.0 | 7.4 / 7.5 |
| GS probe read-backs per frame | 0 | 1.0 |

World rate about 142 fps against 119 fps at this pose (4K, the owner's DLSS setting). Two more runs were left out: one
`fl0` run with another agent's game running (`rc-flames` `fl0-2`, `rc-flames3` `off-0`), one `fl2` run during a pack
conversion (`fl2-2`); the whole `rc-flames2` series ran beside another game in every run (frame times 8 to 26 ms) and
is not used. The tool's header names its scene "ootf-mines"; the variants start Rift Crystal at the pose above.

The Drakan 2 torch generator also spawns its "Wall Torch Halo" (a glow sprite). With the rows on the frame does one probe
read-back of the GS per frame; that the halo causes it is inferred (not measured without it), as Surdana and the Ruined Village opening already do (`ootf-performance.md` 7.2), and the
EE waits for it. The frame queue (`ootf-cpu-frame.md`) removes that wait and is opt-in. Turning the rows off restores the
old cost.

**Small flames.** The Drakan 2 fire does not scale with the object. 17 flames are placed at 0.2 (14 hanging lamps in
Islands, 2 in Islands Night, 1 in Wartok Canyons): each names the fire of WORLDCON "Candle01" in its own object
parameters (pack `fl5`). The census at the Islands lamps shows one emitter per flame instead of four (`runs/ev3_fl5_is`
against `runs/ev3_fl2_is`), so the object parameter is honoured; the lamps themselves were not seen (Rynn starts on Arokh
there and the dragon filled the view).

With all eight rows off (`--ps2-model-map tools/all_off.json`, formats `2da5ea5`, the same change rebased onto `0afdb74`), the pack is
byte-identical to one converted from the untouched formats `0afdb74` (`packs/fl_off` against `packs/up0`, every file).
Packs `fl0` to `fl4` were built from the change on top of `19d9187`; `fl5` (rows on) from the rebased change.

## 6. Alwarren's magic flame ring (asked during the round; not changed)

The ring round the barrier crystal is 18 of Alwarren's 93 "Alwarren Magic Flame" objects (engine class 43, a particle
emitter, `Initial State` 1, damage 1,000 hp/s). It is not the generator fault above. Every one is placed with PC flags
0x100 (no visible bit), and the converter copies the flags. In the pack the 17 emitters near the ring exist but draw
nothing (`runs/ring_fl5`). A test pack with only `FLAG_VISIBLE` added to those 93 objects (`tools/visflag.ts`,
`packs/ringvis`, `runs/ring_vis/al_ring_b.png`) burns: flames on every skull of the ring and a tall plume above it. So
the PS2 emitter runs only with the visible bit, where the PC one evidently ignores it: on the PC the ring must burn from
the start, because its only sender turns it off. The chain is fountain piece 1989 (class 17) to timer 1987 (0.1 s) to
timer 1993 (12 s, "Trigger Message" 0) to the 18 ring flames. The other 75 are switched by 38 timers (toggles that
repeat, so they pulse).

Not landed: with the ring burning at 1,000 hp/s, a broken off chain (fountain piece message, the two timers, the
emitter's reaction to the message) would block the way to the crystal, and none of it was played (a direct start has no
weapon to break the fountain piece). The fix to try is the visible bit on converted class 43 objects whose PC class
starts on, followed by a played check of the fountain, the ring going out 12 s later, and one of the pulsing groups.

## 5. Not verified

* Scaled flames: the 7 flames at 0.6 to 0.75 and the arena relic sconces at 7 to 8 burn at the normal torch size; the
  0.2 lamps burn as candles but were not seen in game.
* Grimstone Mines and Wartok Canyons flames were converted the same way but not looked at (both start on Arokh).
* Succubus Lair's tilted sconces (moved by their turn only) not looked at.
* Whether the OotF flame light sat at the base point or at the flame on the PC was not compared; it now sits at the flame.
* Exploding barrels in the Islands levels ("Barrel (Exploding)") were converted the same way but not set off in game.
* Barrels: whether the Drakan 2 staves add to the OotF staves' damage was not measured beyond the orc and the death notice
  (Rynn's health read back 100 even in runs that showed the death notice: the stats block read is not trusted).
* Fire, torch and explosion sounds were not listened to. The flame classes keep the template's `Looping Sound`; the
  barrels now run two explosion generators, so two explosion sounds may play. Class default sounds belong to the class
  sound agent (`build/review/ootf-class-sounds/`); this change touches only `Fire Particles`, the model of the flame
  classes and the barrels' `Smoke Generator`.
* Drakan 2 is untouched (converter only); no Drakan 2 scene was played.
* The flame distance: the engine starts a flame within `engine+0x38c` = 153,600^2 (one world view distance), so a torch
  beyond it shows nothing, as the old card was beyond the far clip there too.
* The bench measured one scene; a torch-heavy outdoor view (Alwarren town) was not measured.
