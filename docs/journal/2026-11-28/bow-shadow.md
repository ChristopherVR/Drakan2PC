# Bow missing from Rynn's shadow (Order of the Flame)

Owner's report: "There is also no shadow for the bow properly with Rynn". Work folder (not committed):
`build/review/bow-shadow/` (private card `card33` = the owner's Slot 1 copied as quicksave slot 33, recipes, runs,
probe scripts, test packs).

## 1. How the character shadow is made

Only the native projected shadow is in play with the owner's settings (`ray_shadows = 0`, `shadow_distance = 3`).
The projector `0x1ab890` (a0 = projector manager, a1 = the character, hooked in `drakan_ghost.cpp`) renders a
silhouette into a small render target that is then laid over the ground:

* it walks the character's skeleton (`+0x50`) attachment list (`+0xf4` array, `+0xfc` count, stride 0x6c, a registry
  ref at `+4`) and the manager's own list (`+0x5c`); an attachment counts when its object has a model and byte `+7`
  bit 0 set;
* each one is drawn by `0x1ab530`: model of the object (`vtable +0x2c`), model again from the model manager
  `0x4dae20`, posed vertices from the skeleton (`*(sk+0x120)`, else model `+0x24`) and then **the model's strip
  display list, model `+0xa8`** (`0x173658`: `**(model+0xa8) + slot * 0x28`). Without it nothing is drawn and the
  function just releases the model.

Model `+0xa8` is the record 0x0211 (`0x174768` loads it). Static props are drawn in the world from 0x0203/0x0204
without it, so a held item can be visible and still have no shadow.

## 2. What Rynn holds (owner's save, RAM dumps)

`runs/bs-lever/ram_lbow.ram`, Rynn handle 0x97f210, attachments in skeleton order:

| Attachment | Model | 0x0211 (`+0xa8`) | Byte 7 |
| --- | --- | --- | --- |
| left holder: Long Bow (class 0x1004f) | 0x3148b (RESOURES 0x148b `LongBow_Wooden`, skinned, 142 vertices, 9 joints) | none | 0x19 |
| right holder: sword (class 0x1005f) | 0x31f5b | present | 0x19 |
| quiver (type 11) | 0x3149c (`StandardQuiver`, rigid) | none | 0x19 |

All three pass the attachment test; only the sword has a display list. The OotF Long, Heavy and Speed Bow classes show
the Drakan 2 disc model of "Wood Bow:  Long" (`ootfps2model.ts` weapon rows), which is the disc's own
`LongBow_Wooden`: the pack ships RESOURES.MOD with that model unchanged.

## 3. Native trait of the disc data

On the PAL disc RESOURES.MOD (`probe/` scripts), 8 skinned bow models have no 0x0211: LongBow_Wooden (0x148b),
longbow_wooden_e, ShortBow_RuneBone, shortbow_runebone_e, BowOfTheOrder, LonBow_War, ClkWrkBow_Savage, Bow_E3. All
other bows (ivory, runebone long bow, short wooden, compound, elemental bows) and all swords, axes, maces, clubs,
hammers and staves have one. So in Drakan 2 the same wooden long bow casts no shadow either, by the same code path
(not run in a Drakan 2 level: no Drakan 2 save with that bow was at hand). Fixing it for Order of the Flame is in
scope because the converter chooses this model for every OotF bow.

## 4. Proof that the display list is the gate

All runs: `play_run.py --save-ootf card33 --env DRAKAN_GAME=d1 --env DRAKAN_PLAYTEST_GOD=1`, the player's
`dist/DrakanPC` settings, recipe `recipes/out.txt` or `lit.txt` (place at 177152 57800 164864, an open, lit snow
floor in Grimstone Mines, camera looking down). Test packs through `DRAKAN_MODS` (copies of the installed packs plus a
small pack with `new/RESOURES.MOD/` entries).

| Run | Bow model | Shadow |
| --- | --- | --- |
| `lit-before`, `lit-before2` | disc LongBow_Wooden, no 0x0211 | body only, no bow |
| `ivory` | LongBow_Ivory records (disc 0x0211) in place of 0x148b | bow and string in the shadow |
| `ivory-ours` | Ivory geometry, our encoder's 0x0211 | bow and string in the shadow |
| `wooden-ours`, `lit-after2` | LongBow_Wooden + our 0x0211 only | bow and string in the shadow |

One run (`lit-after`) logged `VU1 reserved lower` XGKICK errors and lost the world for some frames; the exact same
recipe and pack (`lit-after2`) and two more pack runs gave none. `dist/DrakanPC/drakan.exe` was replaced by another
session's deploy within minutes of that run (20:47), so it is not attributed to the list; see "Not verified".

## 5. Fix (formats `ootfdisplaylist.ts` `heldItemDisplayList`, `ootfpipeline.ts`)

The converter remembers the disc models its weapon classes show (`nativeWeaponModels`). When it writes the host
RESOURES.MOD it adds a 0x0211 to each of them that is skinned, has none and fits one vertex window in its own vertex
order (one group, identity layout): vertex, polygon and skeleton records stay byte for byte, the list names the
0x0203 vertices as they are. Of the OotF weapons only LongBow_Wooden qualifies (4078 bytes, one group of 142 vertices,
208 triangles, the same bytes as the tested `bow-dl` pack). Rigid models (the quiver) are not touched: they draw in the
world without a list and adding one changes how they are drawn. A model that does not fit gets a conversion warning.

## 6. Verification with the installed pack

Pack converted again (`extras.py ootf --source build/ootf/cab_disc --work build/review/bow-shadow/ootf --play
dist/DrakanPC --force`, build `493b53de681b8791` kept, log line `held weapon model 5259: strip display list added`)
and installed with `Build-LocalGame.ps1 -Incremental -ModDirectory build/review/bow-shadow/ootf/mods` (deployed exe
SHA-256 `AF59E5E2...4B70`). Against the pack it replaced, the only change in the whole zip is RESOURES.MOD, and in it
the only change is the added entry `0x0211 / 0x148b`: every other model, the swords and axes included, is byte for
byte the same, so their shadows cannot change.

`runs/final-installed` (installed pack, no `DRAKAN_MODS`, owner's settings, no VU1 errors): the bow and its string
are in Rynn's shadow at every angle (`f_bow0`, `f_bow1`). Side by side with the old pack:
`build/review/bow-shadow/evidence_bow_shadow_before_after.png` (left `lit-before2/o_bow0`, right
`final-installed/f_bow0`).

## Not verified

* A native Drakan 2 level with the wooden long bow (same model and code path, see section 3).
* The quiver on her back still has no shadow (rigid disc model without 0x0211, native too).
* The single `lit-after` VU1 packet error run (section 4).
* No screenshot of the sword's shadow: in the harness the mouse wheel and the quick slot keys did not swap the bow
  for the sword with this save. The sword model (0x31f5b) already had its list and its records are unchanged.
* Firing the bow with the new list (another session works on bow firing; not touched here).
