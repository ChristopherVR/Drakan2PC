# Drakan 2 textures for converted OotF objects (2026-11-08)

User request: where the PS2 disc (Drakan 2) has a texture for something specific, such as a particular tree, use it instead of
the Order of the Flame one, with a way to turn that off in the editor and the converter.

Probes and runs are private under `build/ps2tex/` (`inv.ts` model and texture inventory of both games, `thumbs.ts` + `match.py`
picture search, `veg.py` vegetation candidates, `hdcheck.py` HD hash check, `pairs.png`) and `build/ootf-run/ps2tex-*`. Nothing
from either game is quoted here; only names and ids.

## What matches

* **By model name: nothing for trees.** None of the OotF tree, plant or rock models has a namesake on the disc except the
  tropical plant: Drakan 2's `LEVEL3ON` "tropical plant01large(h)" is the OotF "tropical plant01" (same 80 polygons, UVs
  0..1) with a repainted frond texture (`LEVEL3ON.TXD#6041`). The disc's birches, pines (`WORLDCON` PineTree01, pine1_l,
  pine1SNOW), dead, arid and evil trees are other models; OotF has no birch at all.
* **By picture (a 16 x 16 thumbnail search over 9,158 decoded textures, correlation plus mean colour, four mirrorings):**
  hundreds of OotF textures are on the disc unchanged or at half size (the port reused them: torch frames, crates, RESOURES
  icons, `SYSTEM.TXD#5251` TreeBarkFragment = the OotF pine trunk). Using those changes nothing visible or loses resolution, so
  they are not in the table. Terrain: the only OotF terrain textures with a Proc Textures twin are a placeholder and lava,
  which the liquid path already maps.
* **By kind and layout (the table):** a disc texture qualifies only for the same kind of surface, the same aspect ratio and a
  UV use the OotF model can take. The OotF pines are five stacked cones textured with a top-down radial canopy; every disc
  pine or birch leaf texture is a side view of one branch or a leaf ball, so no foliage pair works. Bark tiles on both sides,
  so the bark pairs work:

| OotF texture | Drakan 2 texture | Why |
| --- | --- | --- |
| `Tropical Common#5425` (tropical plant01 / 01Large / 01Small, tropical tree02) | `LEVEL3ON.TXD#6041` | same model on the disc, repainted fronds |
| `Mountain World Common#4619`, `Intro Level Common#4619` (all pines and LiteSnow trees) | `WORLDCON.TXD#412` | trunk of the Drakan 2 pines |
| `Mountain World Common#5279`, `Intro Level Common#5279` (Log) | `WORLDCON.TXD#412` | Drakan 2 wraps its SnowyLog in it |
| `Ruined Village#5433`, `Intro Level#116` (oldtree, oldroots, Oak Tree) | `LEVEL2RE.TXD#545` | broadleaf bark of the Restoration Tree, 256 px instead of 128 |

Not mapped: pine and LiteSnow canopies (`4618`, `4620`, `4673`, `4674`), oak and old tree branch cut-outs (`115`, `117`,
`5432`, `5434`), log end rings (`5280`: the disc's SnowyLog end ring fills only the middle of its texture), palms, tropical
tree01 and bush, vines (the disc's copy is a quarter of the size), every terrain texture.

## Implementation (formats `ootfps2tex.ts`)

* `DEFAULT_PS2_TEXTURE_SUBSTITUTIONS`: rows `{ ootf: '<container>#<id>', ps2: '<disc TXD path>#<id>', label, why }`.
  `effectiveSubstitutions({ enabled, off, extra })` applies the user's choice; `ps2TextureSwapFromEdits` reads the CLI's JSON.
* `resolvePs2Substitutes` reads the disc TXDs once (through `OotfPs2Context.readPs2Txd`), taking animation frames that follow
  with the "next frame" flag. `OotfAssetBuilder.addTexture` looks the OotF texture up before converting it and stores the disc
  record byte for byte under the next free id (`useSubstitute`), keeping the OotF texture when the aspect ratio differs or a
  skinned model needs 8 bpp square. Two OotF textures naming one disc texture share one copy. The record keeps its colour key
  and alpha flags, so cut-outs draw as on the disc; the model's double-sided flag follows them.
* HD: the copy decodes to the disc picture, so its runtime hash is the disc texture's (`hdcheck.py`: `RESOURES.TXD` copies hash
  to `WORLDCON.TXD#412`, `LEVEL2RE.TXD#545`, `LEVEL3ON.TXD#6041`, all three already in `dist/DrakanPC/textures` from the disc
  pack). `ootf_hd_pack.py` skips hashes already present, so no OotF upscale replaces them.
* CLI `--ps2-textures 0|1`, `--ps2-texture-map <json>`, `--iso`; editor import dialog section "Use Drakan 2 textures where the
  disc has a matching one" with a tick per pair, "Add your own pair", remembered in the browser, and a result line.
* Tests (synthetic): `formats/test/ootfps2tex.test.ts` (table rules, edits, disc frames and alpha, byte-for-byte copy with
  frames in the model's texture list, aspect and creature fallbacks, one shared copy), `app/test/ootfps2tex.test.ts`.

## In game

Private copy of `dist/DrakanPC` without its textures (`build/ps2tex/game`), `ootf_level_run.py --no-convert` with the 13-shot
look-around of the pink terrain check, holding `build/ootf-run/game.lock`. Each level converted alone, once with the default
(`ps2tex-*-on`) and once with `--ps2-textures 0` (`ps2tex-*-off`). Every run loaded its level and passed.

* **Pack contents.** Between the on and off packs only the substituted `RESOURES.TXD` entries differ (by runtime hash). Ruined
  Village: on has the disc pine trunk (`WORLDCON.TXD#412`, also used for the log) and the Restoration Tree bark (`LEVEL2RE.TXD#545`);
  off has the three converted OotF textures instead. Islands: only the frond texture differs.
* **Ruined Village, HD on (`dist/DrakanPC/textures`).** Start view, sixth turn (`build/ps2tex/cmp_a06_pines.png`, on left, off right):
  the three pines behind Rynn have pale grey Drakan 2 trunks with the default and dark brown OotF trunks with the option off;
  foliage, cut-out edges and lighting are the same, nothing is stretched or missing.
* **Ruined Village, texture dumps (`GSR_TEXDUMP`, private texture folder, HD off).** The on run decodes the disc pine trunk and
  Restoration Tree bark hashes and none of the three OotF ones; the off run decodes all three OotF ones. (The pine trunk hash also
  shows in the off run: a template object uses it.)
* **Islands, texture dumps.** The on run decodes the Drakan 2 fronds (with the colour key applied at run time, so their runtime
  hash is the disc pack's alias entry, which the Drakan 2 HD pack already maps by `alias.txt`), the off run the OotF fronds. The
  plants are not in any of the start shots (Rynn starts on Arokh in a cave), so the fronds were not seen on screen.

## Open

* No Drakan 2 counterpart: pine and snowy pine canopies, oak and old tree branches, palms, tropical tree01, bush, vines, log end
  rings, every terrain texture. A whole-model swap (an OotF pine placed as the Drakan 2 PineTree01) would need model and
  collision work and was not tried.
* The Drakan 2 pine trunk is much paler than the OotF one; it is the Drakan 2 look, untick "Pine trunk bark" to keep the OotF one.
* The old tree, oak and log bark were checked by texture decode only, not on screen; the fronds likewise.
* The editor dialog section was checked by unit tests and typecheck, not clicked through in a browser.
