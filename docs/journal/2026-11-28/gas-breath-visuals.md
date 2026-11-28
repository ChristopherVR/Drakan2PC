# Wartok Canyons: the poison gas breath (HUD icon and in-flight look)

Report: after Arokh takes the poison gas Quickening crystal, the HUD breath orb stays the flame when the wheel selects the
poison, and the poison shot is heard but not seen in flight.

## HUD icon (fixed, verified)

The PS2 HUD (`0x3ad450`) shows the icon of the chosen breath entry (`+0x6a4` index into the 16-byte list at `+0x6a8`),
word `+8` of the entry. AddBreath (`0x27d6f0`) fills it from the power-up's PS2-only field `HUD Bitmap Index` (disc: fire
0, sonic 1, ice 2, lightning 3, darkness 4, a green orb). The PC Dragon Power-up has no such field, so every converted one
kept the template's 0: the flame for any breath.

Fix (formats `ootfbreath.ts` `powerUpHudIndex`, `ootfpipeline.ts`): after the class references are relocated, a converted
campaign power-up takes the icon of the disc power-up in the same ODB with the same Weapon or Alt. Weapon (Ice Powerup 2,
Lightning Power up 3, the "On" Quickening copies too), and a breath converted from the PC (only the poison gas has no
Drakan 2 class) the green orb 4. Inert crystals and arena power-ups keep theirs. The log lists each change (`breath icon:`).

Evidence (`build/review/gas-breath/`): `hud_poison_fire_poison_after_fix.png` (installed pack, the owner's settings,
`Slot33` copy: green after the take, flame after `wheel -1`, green after `wheel 1`), `installed_fix_green_icon_after_take.png`;
before: `before_fix_flame_icon_no_projectile.png` (flame throughout).

## The poison projectile in flight (partly fixed)

PC data (`Resources.odb`): "Main Poison Breath" 3037 is a projectile (engine 8) with model 3674 "Poison Ball" (0.15 lu,
pale green, UVs 1 to 2), a "Poison Gas Trail" 3290 sprite (engine 1, 16-frame puff, scale 2 to 4, growth 1/s, then Die)
released every 0.01 s, the "Poison Gas Light" 5009 and the "Poison Flash" 3457 (engine 125, no PS2 class, dropped). The
Alt. weapon "Alt Poison Breath" (engine 70) has no PS2 class and stays empty. On the PC the look is the trail of puffs.

Converted (RESOURES 18299): model 8033, Smoke Object 18534 (model 8086), Special Effect the light. In game the projectile
flies (watch `class=10008`: `e+8 = 0x31f61`, ~40000 units/s, visible bit set) but nothing is drawn.

Test packs (`DRAKAN_MODS`, records patched in a copy of the installed zip, runs `build/harness/gasb-t*`):

* The smoke object works on the PS2: pointing 18299 at the Sonic breath's smoke (14699) draws its rings (`gasb-t3`).
* Class 18534 with the sonic model 5993 and the sonic fields draws rings (`gasb-t6`); with its own model 8086 it draws
  nothing, whatever the fields, shading (`0x10000`, `0x10010`), texture count (16 or 1), normals, winding or texture
  (even the sonic texture): `gasb-t10a`, `t12b`, `t12c`, `t13a`, `t13b`, `t14`, `t16*`.
* Model 5993 with the converted gas texture draws green puffs (`gasb-t15`), and model 8086 with 5993's vertex, polygon
  and bounds records draws them too (`gasb-t17` with the sonic fields, `gasb-t18` with the PC fields, smaller).

Fix (formats `ootfmodel.ts` `discSpriteCard`, `ootfassets.ts` `discSpriteLayout`): the card of a converted sprite
animation (engine 1) is written in the disc smoke sprite layout (corners (-x,-y) (x,-y) (-x,y) (x,y), normals -Z, triangles
(1,3,2) (1,2,0), OBB without polygon lists), each corner keeping its PC texture coordinates. Pack rebuilt with
`extras.py ootf --play dist/DrakanPC --force` (build `493b53de681b8791` kept) and installed by `Build-LocalGame.ps1
-Incremental -ModDirectory build/ootf/mods`.

Not verified / open:

* With the installed pack and the owner's settings no puff trail is seen in flight (`installed_fix_inflight_no_trail_k.png`,
  run `gasb-k-fix`: Arokh held at 289792 40000 262000 facing the shrine, two shots, frames every 0.1 s during both
  flights). The puffs showed only around the head in the held-dragon runs, which shoot along a heading the camera does not
  follow; the native fire ball was not visible in flight in the same runs either, so these harness views do not settle
  the trail. It was not looked at while flying freely.
* The poison ball (8033) was never seen. Its draw distance comes from its size (`shading & 0x10000` clear, journal
  2026-11-12/gfx-models.md) and its UVs lie between 1 and 2; setting `0x10000` did not show it (`gasb-k-s3`).
* Every converted sprite animation card in every level now has the disc layout; only the poison trail was run.
* A save that took the poison crystal with the earlier pack may keep the flame icon if the breath list (with the icon
  word) is restored from the save; not checked (the owner's current quicksave is on foot in Grimstone Mines).
* While the HUD switches to fire the orb's top looked cut flat in one frame (`hud_poison_fire_poison_after_fix.png`, middle);
  not looked into.
