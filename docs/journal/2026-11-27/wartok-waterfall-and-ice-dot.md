# Wartok Canyons: broken waterfall water and an ice crystal in front of Arokh

Report: "load our current quick save you'll notice the waterfall has water that is broken and there is an ice dot floating
in front of Arokh". The quicksave is the Order of the Flame slot 33 in Wartok Canyons (21:14), Arokh in the air at
`(125142, 49448, 367423)` facing the cascade west of the start.

All runs used `scripts/harness/play_run.py` on `dist/DrakanPC` with the player's `drakan.ini` (D3D11, 3840 x 2160, HD
textures, native water) and a copy of `mc0_ootf`, quick loaded with F8. Run folders are under `build/waterfall/` (not
committed). Fixed in the converter (formats `66573a8`); the game is unchanged. The installed pack needs a new conversion.

## The waterfall water

Both pictures reproduce from the save (`wf-before/loaded2.png`): a sheet of rippled water stands in the sky above the top
fall and a flat slab of water crosses the lower falls. Neither changes with HD textures off, far land off
(`DRAKAN_FAR_LAND=0`) or far scenery off.

The cascade is one PC water layer per run of falls. Layer 437 (cells 29..38 x 172..175) has pools at 65000, 49000 and
39000 with sloped cells between them where the falls are; layer 253 continues at 39000, 29000 and 19000. The waterfall
models ("Large Canyon FallsFront03_top", "_2nd", "_3rd") stand on the slopes.

`GSR_SURFACE_PICK` on the sky sheet (epoch 600, raster 267,96) found a water-pass triangle at world `(69630..71679, 65005,
354304..356358)`: cell 34, where layer 437's data says 49000. The slab (raster 325,373) was layer 253 at 39000 over cells
49..50, whose data says 19000. The PS2 water pass draws a whole water layer at one surface height (here its first pool),
so each stepped layer drew all its pools at the top pool's height. The disc's water layers are flat. Moving layer 437 far
down in a test pack (`wf-sink437`) removed the sky sheet.

Fix: `splitSteppedWater` (convert.ts) gives each pool of a stepped water layer its own flat layer, cut to its cells; the
largest pool keeps the index, id and lists, the others are appended and linked wherever the layer was listed. Sloped
cells on the falls are left out (the waterfall models cover them). Water at one height, shore differences of up to 512
included, is unchanged. Over the converted campaign the installed pack has 3 such layers in the Intro, 19 in Ruined
Village and 25 in Wartok (most are shore cells within 512 and stay as they are; Wartok gets 16 new pool layers).

Verified by applying the same function to the installed Wartok LVL (`build/waterfall/split.ts`) in a test pack: the sky
sheet and the slab are gone and each pool sits under its fall (`wf-after/loaded2.png`, `wf-after/turned.png`,
`wf-split-pl/p2.png`; before: `wf-before/loaded2.png`, `wf-turn/turned.png`, `wf-place/p2.png`).

## The ice dot

A crystal hangs at the screen centre just past Arokh's snout and moves with the view. Firing does not change it, targeting
does not, and it is absent from the older Wartok quicksave on the ground and in the air.

`GSR_SURFACE_PICK` at the screen centre found the crystal at world `(122141, 50128, 366969)`. The only object there in a RAM
dump is an engine 97 (weather) actor of FX class `0x42fbc`, "Snow Object" from the pack, at `(121899, 50019, 366872)`. A
weather emitter follows the camera about 3000 units ahead. In the older save the emitter was the disc's "Rain Object"
(model-less), so the snow area is what switched it on. The converted class was cloned from FX.ODB "Snow Object Old" (closest
defaults, model 3684, used by no disc level), and the engine draws that model, an ice crystal, at the emitter. The snow of
Ravenshold and the Tundra ("Snow Object 1." to "3.", models 4282 to 4284) shows none. Pointing the pack class at model 4283
in a test pack removed the crystal (`wf-snow4283`).

First fix: weather clones skipped "Snow Object Old" as a template. That only swapped the crystal for the next template's model (see the follow-up below).

Tests: `pc.test.ts` (cascade split, links, flat water unchanged), `ootfownmodel.test.ts` (weather clone). Formats suite:
81 files, 819 tests pass; `tsc` clean.

## Not verified

* No new pack was converted: the PC data is not on this machine. The fixes were checked on the installed pack with the
  two changes applied by hand (`DRAKAN_MODS` test packs, `DRAKAN_SAVE_PACK_CHECK=0`). Whether the save build stamp
  accepts the player's quicksave on a converted pack was not checked.
* No snow particles were seen in the Wartok shots with either model; whether the OotF snow should show flakes there was
  not looked at.
* Swimming in the split pools and the split layers in the Intro and Ruined Village were not played.

## Follow-up: a snowflake instead of the crystal

After a new conversion with the first fix the player saw a snowflake in front of Arokh when mounting him. The converted
"Snow Object" now had model 4282 ("Snow Object 2.") and still Particle Texture 0.

Why the disc levels show nothing: the weather classes they place are model-less and carry a texture. Ravenshold and
Shiverbane place LEVEL4ON / LEVEL5NE "Environmental Effect" (no model, Particle Texture `0x216b6` = FX texture 5814, FX
being dependency 2 of LEVEL4ON.DB); FX.ODB "Rain Object" is model-less with texture 5816. Only the FX.ODB "Snow Object Old"
and "Snow Object 1." to "3." carry a model and texture 0, and no disc level places them. The placed objects are alike
(flags 1, no scale), so the difference is the class: the emitter (engine 0x10061) draws its class model at the emitter,
about 3000 units ahead of the camera, and its particles from the Particle Texture. A converted snow class cloned from any
of the model classes showed that model and had no flake texture.

Fix (formats `8cfe537`): weather (97) is no longer an "own emitter model" class; it is cloned from a model-less template
(the closest is "Rain Object"), and a snow class (no "Rain-style Particles") gets FX texture 5814, the Ravenshold flake
(`WEATHER_SNOW_TEXTURE`, `weatherTexture`). The new conversion's Snow Object record has model 0 and texture `0x16b6`.

Evidence, all from the player's 23:03 quicksave (snowy canyon, on Arokh) with the player's ini, looking up past the snout:
`flake_up_compare.png` (left: the pack before this fix, a snowflake model above the snout; right: the new pack, none),
full shots `flake-before3/up.png` and `flake-after3/up.png`. The new pack installed with `Build-LocalGame.ps1 -Incremental
-ModDirectory build/ootf/mods` loads the player's quicksave by Continue and by F8 (`confirm-install`).

Not verified: the dismount and remount itself (Arokh hovers at every placement tried and would not let Rynn off, so the
flake was shown by looking up instead). Snow does not visibly fall in Wartok, before or after: the snow emitter is live
(class `0x42fbc`, 3339 units ahead of the camera in a RAM dump, `snow-after3`), but no flakes show against the sky
(`snow-after3/sky2.png`). Why was not found; the particle state was not traced.