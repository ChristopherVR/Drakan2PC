# Issue 5: the Rift World quick-load hang was a stale node in the terrain draw-data LRU

Follows [Volcanic route](../2026-11-06/ootf-volcanic-route.md), "November 7 follow-up": F8 in converted Rift World hung in
about half the runs, guest thread 1 in the walk `0x15e5a0` -> `0x15c820` (called from `RenderWorld` `0x139010`) over a
list at `0x4dff60` that never came back to its head. Probes and runs are private under `build/harness/ootf-lru/`
(`lru.py` walks the list in a RAM dump, `summary.py` per run, `survey.mts` counts layer blob forms, `run.py` + `recipes/`).

## What the list is

`0x4dff60` is the head of a doubly linked LRU (node `+0` prev, `+4` next, `+8` owner) of the engine's **terrain layer draw
objects** (vtable `0x51ad90`, one per LVL layer: `+0x04` layer id, `+0x14/+0x18` origin, `+0x1c/+0x20` size in cells,
`+0x10c` the blob's chunk records of 0x4c bytes, `+0xdc` a block of built draw pages, `+0x118` the LRU node). The pages'
bytes are counted at `0x4dff10` against a budget at `0x4dff0c` (0x1f3333 in Rift World).

* Insert at the tail: `FUN_00154060` (builds the block) and `FUN_0015ff38`, both only when the block has pages
  (`block+0xc > 0`).
* Touch (unlink, relink at the tail): `FUN_0015f350` and `FUN_00161a78`, reached from the per-frame visible-layer passes
  `FUN_00153e10` / `FUN_00153e70`. **Neither checks the page count.**
* Release: `FUN_0015c820` frees the pages and the block, but unlinks the node **only when the block has pages**. It runs
  from the eviction walk, from the layer's own release (`FUN_00159998`, level unload) and from a vtable method.
* Eviction: `FUN_0015e5a0` (from `RenderWorld`) walks from the head releasing layers not drawn this frame
  (`+0xac` != the frame stamp) while the bytes in use are over the budget.

So a layer whose block ends up with zero pages is linked by the touch path but never unlinked. When the level is torn down
for a load, its object is freed with the node still in the list; the new level's layers are appended behind it, and the
first eviction walk after the load follows the freed node into whatever the heap has put there since.

## Evidence in RAM dumps

The old `rw1` run (passed, pack from before the Morph Plug fix) and `old-1` (current exe, the same `ootf-volroute` pack):

| Dump | List |
| --- | --- |
| before F5, after F5, after the walk | intact, 17 to 19 nodes, **one** layer with no pages: layer 1299 ("layer 157", 32 x 30, 957 invisible cells, 3 holes, no drawn cell) |
| after F8 (`ram_loaded`) | head.next is the freed layer 1299 node (`0x7d7670`); its prev no longer points to the head, its owner is gone (heap data) |
| after D (`ram_after`) | same, and in `rw1` the freed node's next already reads 0, a loop |

Whether the walk then hangs depends on what was written over the freed object, which is why it was intermittent and why it
did not follow the Morph Plugs or the texture budget.

## Converted data, not the runtime

`survey.mts` over the blobs (`readPs2Blob`):

* **Disc, 14 PAL levels:** every layer without a drawn cell uses the empty blob form (`u32 0`, no chunks); no chunked blob has
  only chunks without textures (chunks that list none occur only next to chunks that do: 89 in 9,232).
* **Converted pack (old):** every level with such layers wrote them chunked with all chunks empty (Rift World 25 layers,
  Volcanic 94, 243 in the 23 levels); the converter's `buildPs2TerrainBlob` never wrote the empty form (the editor's resize
  path already did).
* **Stock level quick load** (disc data, same exe, recipe `stock.txt`: walk, F5, walk, F8, D): Surdana `st-1`..`st-4`
  and Ravenshold `rv-1`, `rv-2` all quit cleanly, no watchdog; in every dump (before and after the load) the list is intact
  (74 / 41 nodes in Surdana, 45 to 55 in Ravenshold) and no layer in it is without pages.

The engine path is original code that the disc data never drives into the bad state, so the fix belongs in the converter.

## Fix (formats `5dfe29a`)

`buildPs2TerrainBlob` writes a layer with no drawn cell (all holes / invisible) in the disc's empty form: `u32 0`, then the
normals as before. Such a layer gets no draw block at all (`FUN_00154060` returns before allocating it when the layer has no
chunk records), so it is never linked. Collision comes from the layer's zlib block and is unchanged. Test: "writes a layer
without one drawn cell in the empty form, as the disc does" (`terrainblob.test.ts`). The new full pack has no chunked blob
without a texture (Rift World: 25 empty layers, 12 chunks without textures among 3,620).

## In game (`dist/DrakanPC/drakan.exe`, direct starts, god mode)

Pack `build/harness/ootf-lru/packfix` (formats `5dfe29a`, full 23 levels), exe `7e1572e9...`, against the
`ootf-volroute` pack (before the fix) on the same exe:

| Runs | Pack | Result |
| --- | --- | --- |
| Rift World `fx-1`..`fx-8` (dismount, W, F5, W, F8, D) | fixed | **8 of 8** clean, no watchdog; Rynn back at the saved spot after F8 (7 exact to the unit, `fx-6` 25 units off), D moves her; the list is intact after every load (18 to 23 nodes, none without pages) |
| Rift World `old-1`..`old-3` | before | `old-2` hung (watchdog in `0x15c820` / `0x15e5a0`; its stall dump shows head.next = the freed node of layer 1299, then heap words and a loop at 0); `old-1` and `old-3` did not hang but their lists were already broken after the load (same freed node) |
| Volcanic, new sanctum-area quicksave (`mk-fx`: key, scene 9.6a, mount, breath, cave mouth, F5; Arokh did not land, so the save is mounted) | fixed | saved and quit cleanly |
| Volcanic start, F8 loads that save (`sn-1`..`sn-3`) | fixed | **3 of 3** load, back within 10 units of the save, list intact |
| Volcanic start, F8 loads the old `card-sanctum` save (`sx-1`, `sx-2`) | fixed | stall in the restore: `[memset] bad size` from `0x405714`, then the allocator (`0x4acdd0`) spins. That save was written by an older exe and converter (class ids and logic defaults changed since, formats `34ddc63`), so it is not a valid save for this pack; the earlier stall of the same card on its own pack (inventory call through a null vtable, `0x341548`) was not reproduced with a fresh save |

The Alwarren stalls at the exit to the Islands (`ootf-alw` `alw-13`, `alw-14`, `alw-15`, journal
[Alwarren hammer](ootf-alwarren-hammer-and-scenes.md)) are the same fault: their watchdogs sit in `0x15e600` / `0x15c820`
(`alw-13`, `alw-15`) or in the allocator (`alw-14`, where the walk released a freed layer and freed its stale page
pointers), and their dumps before the exit hold two or three Alwarren layers without pages in the list (`alw-16`, which
passed, had one). Converted Alwarren has 8 layers without a drawn cell in the old pack and none chunked in the new one.

## Still open

* Old saves made before formats `34ddc63` do not load on the new pack (above); the sanctum save has to be made again.
* The engine flaw itself (the touch path links pageless layers, the release does not unlink them) stays; only converted
  data reached it. The editor's terrain writer already used the empty form for resized and new layers; a same-size edit
  that paints every cell of a drawn disc layer invisible goes through `patchBlob` and was not checked.
