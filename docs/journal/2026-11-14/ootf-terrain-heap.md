# OotF terrain heap: the terrain draw pages move out of the game heap (round 5)

Follows [play check A](ootf-playcheck-islands-rift.md) section 1 and [play check B](ootf-check-b.md) section 1: walking in
the Islands (and in Wartok Canyons' lower canyon, and on the load from Alwarren into the Islands) logged
`[memset] bad size 0x20400fe ra=0x157d7c` and the game jumped to address 0 or hung. Scratch, tools, runs and evidence:
`build/review/ootf-heap/` (private). Nothing from the game is quoted.

## Mechanism

* **Allocator.** The terrain draw-block builder (`FUN_00154060` -> `FUN_00154d98` -> `FUN_00156518` / `FUN_00156f58` ->
  page writer `FUN_00157a08`, all in 0x154060..0x157de8) takes every block from the game's one newlib heap: `malloc`
  `0x4ac628`, `realloc` `0x4acef0`, `free` `0x4acc40` (each between the guest semaphore pair `0x4bc1a0` / `0x4bc180`).
  `sbrk` (`0x4bc7b8`) grows the heap up to `EndOfHeap`, which the runtime answers with 0x1f00000; the main stack sits at
  0x1f80000..0x1fc0000, the runtime's RPC packet pool at 0x1f00000. So the heap is 0x5b8600..0x1f00000, about 25 MB.
* **Exhaustion, not corruption or a leak.** In the failing trace of check A (`is-crash-trace`), the last layer's group
  arrays come back from `malloc` as 0: the page writer's group pointers read 0, 0x44, 0x88, ... (offsets from a null
  base), it copies its staging buffer to those low addresses, overwrites the group's own size word and the next
  `memset` gets a size of 0x20400fe. Check B's stall dump shows the same as a page array pointer of 0x10. The pages
  are freed: the LRU of 7 November (`0x4dff60`) releases layers not drawn this frame while the bytes counted at
  `0x4dff10` exceed the budget at `0x4dff0c` (0x1f3333), and the 7 November fix keeps that list intact. But the blocks
  of the layers drawn in one frame are never released, and check B measured 0x5eacf0 bytes for them in the lower canyon,
  three times the budget, on a heap that also holds the level.
* **Why only converted levels.** The converted terrain has 10 to 30 times more chunks per visible list than any disc
  level (check A: Islands 5,656, disc at most 524), because the 8-texture chunk rule cuts crowded chunks down to single
  cells, and every chunk costs group arrays and at least one 0x400-byte page.

## Fix: a terrain page pool above the retail RAM (no visible change)

The PC runtime is not held to 32 MB. `PS2_RAM_SIZE` is now 64 MB (host mapping, address mask, DMA and bounds checks);
the game keeps the retail layout through a new `PS2_RETAIL_RAM_SIZE` (32 MB): the main stack from `SetupThread`, the heap
end, `GetMemorySize`, the async callback stacks, the loader's initial `$sp` and the RAM dumps the harness reads. So the
game's heap, stack and every object it allocates stay where they were, and the many host-side readers that check
"pointer below 0x2000000" keep working.

`drakan_terrain_pages.cpp` (`[general] terrain_page_pool`, env `DRAKAN_TERRAIN_PAGE_POOL`, on by default) wraps the
three allocator functions: a `malloc` or `realloc(0, n)` whose return address lies in the builder takes a block of a
best-fit pool over 0x2000000..0x4000000 (`world/TerrainPagePool.h`), a `free` or `realloc` of a pool address goes back to
the pool, and everything else runs the original allocator untouched. A heap-style size word sits in front of each block.
If the pool is full the original `malloc` runs as before (logged). The wrappers are installed after the object motion
tracker's `free` hook, so pool addresses never reach it. The engine builds, budgets and evicts the same pages; only their
addresses differ, and the VIF1 DMA reads them through the same paths (64 MB bounds).

Pool use in play (the log prints each new 4 MB of peak): Islands and Wartok Canyons settle at 4 to 8 MB in 16,000 to
32,000 blocks, well inside the 32 MB.

The converter's repaint of 14 November (formats `e671be8`, `limitChunkTextures`, 8 to 27 % of the drawn cells take a
neighbour's texture) is no longer needed and is off again by default: `DRAKAN_TERRAIN_CHUNKS=repaint` turns it on (formats
change below). The packs the owner has (converted before `e671be8`, or with `split`) play with the new exe as they are.

## In game

Own play folder (`build/review/ootf-heap/play`: the owner's `drakan.ini` from `baseline-play`, only the `elf =` line
pointed at a private ELF copy; HD textures, DLSS quality, 1280 x 960), the `dist` pack of 14 November
(`dist/DrakanPC/mods/ootf-levels.zip`, cut chunks, no repaint). Harness: `live_drive.py` through the check A plan driver
(`tools/push.py`), direct start, god mode, live keyboard / mouse / pad input; plan `plans/heap.py`. "Before" is the
`baseline-play` exe (sha256 `4fddfbc8...`), "after" the worktree build with the pool (`9d907978...`).

| Run | Exe | What was played / placed | Result |
| --- | --- | --- | --- |
| `is-before` | before | Arokh **placed** on the ground at (396288, 58049, 556032) as check A did, Ctrl to dismount; Rynn **walked** (W, mouse steering) to the crash spot (400384, 560128), looked round, F5 / walk / F8, then walked between waypoints round the spot | `[memset] bad size 0x20400fe ra=0x157d7c`, game frozen at 3 min 45 s (watchdog), as in check A |
| `is-after1` | after | the same, then 11 minutes of walking between 39 waypoints up to 12,000 units round the spot (x 389,816..404,871, z 550,072..571,102; 21 reached, the rest blocked by rock or sea), F5 / walk / F8 three times | **13 minutes, no `memset` line, clean exit**; F8 back within 50 units each time; pool peak 8 MB |
| `wa-after1`, `wa-after2` | after | Wartok Canyons: Arokh **placed** at the lower canyon spot of check B; the dismount did not take, so both runs **flew** Arokh between waypoints over the canyons for 10 minutes each (F5 / F8 three times each) | 13.5 and 13 minutes, no `memset` line, clean exits; pool peak 4 MB |
| `wa-before2` | before | Wartok on foot (pad Cross dismount at the start), Rynn **placed** at check B's hang spot (127658, 35478, 437057), walked round the lower canyon for 4 minutes | no hang in this run (check B's four hangs used other routes); not a reproduction |
| `wa-after3` | after | the same on foot, 10 minutes of walking between waypoints (the waypoints lay beyond rock, so she walked the river bank and ledges between x 120,800..133,000, z 421,000..432,000), F5 / walk / F8 three times | **14 minutes, no `memset` line, clean exit**; F8 back within 60 units; pool peak 8 MB; the same views as `wa-before2` (`evidence/wartok_before_after.png`) |
| `is-ab0` / `is-ab1` | after, pool off / on | Islands start (Arokh in the arrival tunnel), first picture | same terrain; the differing pixels are Arokh's wing beat and DLSS (`evidence/islands_start_pool_off_on.png`) |

Pictures: `evidence/islands_spot_before_after.png` (four of the eight look-round views at the crash spot, before left,
after right: the same ground, rock and sea; Rynn's pose, Arokh and the HUD orbs differ with the run),
`evidence/wartok_before_after.png` (lower canyon, before / after).

## Tests

* `world_terrain_page_pool_test` (new): alignment, sizes, double free, exhaustion, coalescing back to one block, 20,000
  random allocations and frees without overlap.
* `ctest --preset runtime-tests` in the worktree: 193 of 193 passed, 7 skipped (data or device).
* `ps2x-tests` preset: the build stops on a missing include in `vu_async_tests` (not touched here); the `ps2x_tests`
  executable gives the same 118 passed / 34 failed and the same crash at its end with and without this change
  (stash A/B in the worktree), so none of its failures come from here.
* Formats (`vitest run`, worktree): 61 files, 631 tests passed with the repaint off by default.

## Not verified

* The load from Alwarren into the Islands (check B's second hang) was not played; it is the same builder failing, so the
  pool should cover it.
* Islands Night, Volcanic, Rift World and Alwarren were not played with the new exe.
* No frame-time measurement (no turn of the benchmark lock); the wrappers add a lock and a range test to every guest
  `malloc` and `free`, and the pool's map work only for builder blocks.
* A pack converted with the repaint (`e671be8` default) was not played with the new exe (it has fewer chunks, so less
  pool use).
