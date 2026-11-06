# Issue 5: converted cut-out textures drew their holes solid

Follows [invisible walls](ootf-invisible-walls.md) ("Possible follow-up"). Probes and runs are private under
`build/harness/ootf-cutout/` (`disc.mts` / `holes.mts` survey the disc textures, `cutobjs.mts` / `levelcut.mts` list the PC
objects that use cut-out textures, `packcut.mts` / `diffsum.mts` survey and compare packs, `run.py` + `recipes/` the game
runs). Packs from formats `7392049` (base) and the same tree with the fix (new). Nothing from either game is quoted here.

## What the flags word is

The u32 at +16 of a TXD texture header is the engine's alpha selector, not a format field:

* `sub_1bf838` (texture parser) reads it only when byte +37 has 0x02 and maps 1, 2, 4 and 7 / 8 to native material flags
  0x10000, 0x20000, 0x40000 and 0x80000; a colour-key index at +20 (not -1) adds 0x8. `drakan_scene_assets.h` mirrors this.
* The draw path at `sub_18c52c` tests the native flags with `0xf0000` and the key bit and passes the result to the alpha
  enable call (0x1478b8). With selector 0 and no key, texture alpha is off: alpha-0 texels are drawn in their palette colour.
* 0x80000 is the blended class; the others draw with the GREATER 0 alpha test (`runtime/gs/src/ray_scene.cpp`).

Disc survey (all TXDs of the extracted ISO): every 8 bpp texture with alpha-0 texels has a nonzero selector (219 of 221 carry 8,
nearly all with soft edges; NPCS 6314, a hard 8 bpp cut-out, carries 1; one FX texture 4). 16 bpp textures carry 1, 32 bpp 7,
24 bpp and opaque 8 bpp 0. In all 220 8 bpp textures with holes whose palette was checked, the alpha-0 entry has a colour
(the edge's), never black.

## The 43 textures

The converter wrote 8 bpp textures with only hard holes (no graded alpha) with selector 0. A 23-level pack has 43 of them: 40
in RESOURES, 3 skies in WORLDCON. 16 and 32 bpp textures were not affected (selector 1 and 7). From the PC side (placed objects of
the 13 campaign levels) they are:

| What | Models | Where (placed objects) |
| --- | --- | --- |
| Pine and snowy tree branches | Large / Med / Small Pine, Medium / Large Tree LiteSnow | Ruined Village 149, Wartok 75, Grotto 51, Grimstone 17, Intro 23 |
| Torch heads, sconces, mine shafts and supports | gtorchp, HTorch, TSconce01, Shaft01, Support01/02, DoorSupport01 | about 400 over 9 levels |
| Skeletons | Skeleton01..07 | Mountain levels 59, Tropical levels 34, Volcanic 2 |
| Banners | TrollBannera01, WallBanner01 | Grimstone, Grotto, Wartok |
| Hanging body, spiked torso, cages, water barrel | HangingBody2, Torso on Spike01, Cage01/02wPerson2, Water Barrel | Wartok, Volcanic, Grimstone |
| Skies | SkyLevel01b, Night Sky | Ruined Village, Succubus Lair |
| Creatures and weapons | Death Mage, Death Mage Shaman, Navy Hueges, tuiri lady, Bow, one of Hueges / Ballista | many |

## Fix (formats `cd48742`)

`encodePs2Texture` gives an 8 bpp texture with holes and no graded alpha selector 1, like the disc's 16 bpp cut-outs and its
8 bpp cut-out NPCS 6314; graded alpha keeps 8 and opaque textures keep 0. This also covers forced 8 bpp (skinned models,
fast-draw weapons) and the 16 bpp to square 8 bpp upgrade. The alpha-0 palette entry of an exact (at most 256 colour) texture
now takes the mean colour along the hole edges, as the quantised path already did, so filtered edges do not darken.
Test: "gives an 8 bpp cut-out the alpha-test selector (flags word 1) and its holes the edge colour" in `ootftex.test.ts`.

Pack comparison (23 levels, 10,698 files): only RESOURES.TXD, WORLDCON.TXD and PROCTEES.TXD differ. 43 textures change selector 0
to 1; 743 other 8 bpp textures with holes change only the colour of their alpha-0 entry. In every changed texture the alpha
plane and every visible texel decode the same. Every LVL, MOD and ODB is byte-identical.

## In game (`build/harness/issue5-dist/drakan.exe` from main `c8d511ea`, direct starts, god mode, each run on both packs)

| Run | What | Base pack | New pack |
| --- | --- | --- | --- |
| `gro-*` | Grotto, snowy pines 275..281, close up from both sides | branch cards drawn as solid dark quads, nothing seen through them | ragged needle edges, the cave wall and sky show through the holes |
| `gro-*` | same pines from 12,000 units | every pine a smooth solid cone with dark edges | the layered branch silhouette of the PC trees |
| `gro2-*` | Grotto, Standing Banners 263 / 264 | each banner a solid rectangle, the gap between its two strips black | two separate strips with swallowtail ends, the background between them |

The cut-out edges show no dark fringe. Not shown: the placed views of the Grotto and Wartok skeleton groups, Wartok torches
and the Spiked Torso landed inside geometry (flat fog colour on both packs), so those objects were not seen; they use the
same textures and selector as the ones above.
