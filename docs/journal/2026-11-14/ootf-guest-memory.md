# Guest memory: the game heap grows into up to 224 MB of PC memory (round 6)

Follows [terrain heap](ootf-terrain-heap.md). The owner asked for more than 64 MB and for it to be dynamic. Scratch,
tools, runs and evidence: `build/review/ootf-mem/` (private). Nothing from the game is quoted.

## The limit and why 256 MB

The recompiled code decodes a guest address on its fast path as `(a & 0x1FFFFFFF) < PS2_RAM_SIZE` and masks the
offset with `PS2_RAM_SIZE - 1`, so guest RAM must be a power of two, and everything from 0x10000000 up is the hardware
map (I/O registers 0x10000000, VU memory 0x11000000, GS 0x12000000, IOP RAM 0x1C000000, BIOS 0x1FC00000; the
scratchpad 0x70000000 and the KSEG / uncached aliases 0x20000000, 0x30000000, 0x80000000, 0xA0000000 fold onto RAM
through the same mask). 0x10000000 = 256 MB is therefore the largest guest RAM that collides with none of them; the
uncached accelerated window 0x30100000 still lands at 0x100000 because 0x10000000 is a multiple of the RAM size.
`PS2_RAM_SIZE` is now 256 MB (`ps2_memory.h`, with a static assert against the I/O base); the game keeps its retail
32 MB layout (`PS2_RETAIL_RAM_SIZE`: stack, heap end, `GetMemorySize`, RAM dumps). WebAssembly keeps 64 MB.

## Host memory: reserve, commit as used

`runtime/ps2_guest_ram.h` (`GuestRamReservation`): Windows reserves the whole range with `VirtualAlloc` and commits
only the retail 32 MB at start; the rest is committed by its user as it grows and decommitted when it shrinks. A guest
access to a reserved page that nobody committed (a wild pointer, a debug scan of all RAM) is caught by a vectored
handler that commits that 64 KB granule and continues, so nothing that worked on the fully mapped 64 MB crashes now
(the handler leaves committed pages alone, so the `PS2X_WATCH_RDRAM` protection faults still reach their own
handler). POSIX maps the range lazily (`MAP_NORESERVE`); other targets allocate it zeroed. One granule of slack past
the end keeps a host copy of a few bytes from a masked pointer at the top in bounds. Before, the 64 MB were allocated
and cleared with `memset`, so all of them were resident from the start.

## The heap: the game's own allocator, on more memory

The 14 November pool served only the terrain builder. The general answer turned out to be one level lower: the
game's allocator is newlib's dlmalloc (malloc `0x4ac628`, free `0x4acc40`, realloc `0x4acef0`, memalign `0x4ad1c0`,
calloc `0x4ad7e8`), and it grows only through `sbrk` (`0x4bc7b8`: break at `0x517084`, compared with `EndOfHeap`).
dlmalloc already handles a "foreign sbrk": when sbrk answers with an address that is not its old top end, it fences
the old top chunk with two size-4 headers, frees the rest of it and starts a new top at the answer (read in
`malloc_extend_top`, `0x4ac300`: the `beq a1, s4` test, the second sbrk for the alignment correction and the
fencepost stores of 5 = 4 | in use). So `drakan_extended_heap.cpp` answers sbrk itself (`[general] heap_extend`, on):

* below the retail end it behaves as the game's sbrk;
* a growth past it returns 0x2000000 and moves the break there; later growth continues up to 0x2000000 +
  `[general] extended_heap_mb` (default and most 224), committing host pages as the break rises;
* a negative sbrk (dlmalloc's trim) lowers the break and decommits the pages above it.

Every malloc, realloc, memalign, calloc and free of the game then works on its own chunk headers and bins, on both
ranges, with no wrapper on the hot allocator path (the old pool wrapped every malloc and free with a lock). newlib's
own `_malloc_r` shares the same sbrk and is covered the same way. Rules: `world/ExtendedHeap.h` `decideSbrk`.

One guard was needed: a static initialiser (`0x4ac1f0`) probes memory at start by mallocing most of the heap and then
three 32 MB blocks it expects to fail; each success adds 32 MB to a memory figure and is never freed. Its mallocs
(return addresses inside it) close the extension until its closing free (return address `0x4ac28c`), so it sees the
retail answer as before (`[extended-heap] start-up heap probe` lines with `DRAKAN_EXTENDED_HEAP_LOG`).

The terrain page pool stays for `heap_extend = 0` (`[general] terrain_page_pool`), now on the same reservation with
2 MB commit steps and give-back of a free top beyond one spare step.

Corrupt lengths: the HLE `memset` / `memcpy` still drop any size above the retail 32 MB (logged `bad size`), so a
broken length like `0x20400fe` is still caught although guest RAM is larger.

## Host code that assumed 32 MB (audit)

With game objects above 0x2000000 possible, every host reader and writer that masked a guest pointer with `0x1ffffff`
(which would alias 0x2100000 to 0x100000 and write actor positions into low RAM) or bounded it by `0x2000000` now
uses `PS2_RAM_MASK` / `PS2_RAM_SIZE`: arena, co-op, ghost, PvP, netplay, MP death, world snapshot, Tab screen,
portable camera, pad, save guard, movie player, foliage / model detail / polyclip / terrain distance bounds, the
combat and parry audits, the controlled-actor reader, and the input / gameui / world libraries
(`drakan_input::kGuestRamEnd`, `FlatGuestMemory`, `kRamPtrEnd`). Sizes, the game's layout, RAM dump files and
debug-only scans were left. The harness (`live_drive.py`) reads guest addresses up to 256 MB (`RAM_SIZE`), treats
unread extended pages as zero and keeps 32 MB dumps (`RETAIL_RAM_SIZE`).

## Model and animation caches (survey brief D)

With the heap able to grow, `[general] model_cache_mb` / `animation_cache_mb` default to `auto`: 16 / 8 MiB when
`heap_extend` is on (the extra bytes come out of the extended heap once the retail part is full), the level's own
budget otherwise; `0` keeps the level's own budget, a number sets it.

## In game

Own play folder (`build/review/ootf-mem/play`: the owner's `drakan.ini` from `baseline-play`, only `elf =` pointed at a
private copy; HD textures, DLSS quality, 1280 x 960), the `dist` pack of 14 November. Harness `live_drive.py` through
the check A plan driver (copied to `tools/`), direct start, god mode, live keyboard / mouse / pad input. Two other
agents' game instances ran during most runs.

"Exe A" is the worktree build of 10:32 (heap extension, before the cache default and the top slack), "exe B" the build
of 11:29 (all of this change, on main of 10:38 with the texture limits and cache settings of the other agents); "dist"
the coordinator's exe of 14 November (the 64 MB terrain pool).

| Run | Exe | What was played / placed | Result |
| --- | --- | --- | --- |
| `is-mem1` | A | Islands: Arokh **placed** at check A's spot, Ctrl to dismount; Rynn **walked** to the crash spot, looked round, then 11 minutes between 38 waypoints up to 12,000 units round it (21 reached; x 389,698..404,871, z 550,071..571,235); F5 / walk / F8 four times | **13 minutes, clean exit, no `bad size`**; F8 back within 70 units each time; the heap continued above 32 MB at the load and peaked 2 to 3 MB above it |
| `wa-mem1` | A | Wartok Canyons: pad Cross dismount, Rynn **placed** at check B's hang spot, walked the river bank and ledges for 11 minutes (x 121,960..131,313, z 421,224..430,061; 15 of 32 waypoints), F5 / walk / F8 four times | **15 minutes, clean exit, no `bad size`**; F8 back within 50 units; the heap stayed inside the retail RAM |
| `stress1` | A | Islands as `is-mem1`, 5 minutes of waypoints, with `DRAKAN_HEAP_EXTEND_AT=0x00c00000` (the retail heap ends at 12 MB, so the level's data lives above 32 MB) | 7 minutes, clean exit, no `bad size`; heap 19 MB above 32 MB; enemies, fights, water, three F5 / F8 (one in water drifted 2,000 units while swimming); pictures as usual (`evidence/stress1_sheet.png`) |
| `su-mem1` | B | Drakan 2, Surdana from the New Game start: 7 minutes of waypoints round the start outside the town gate (124 of 124 reached), F5 / walk / F8 twice | clean exit, no `bad size`; caches raised to 16 / 8 MiB on every load, model cache in use up to 4.9 MB, no drop lines; heap inside the retail RAM |
| `su-stress1` | B | the same with `DRAKAN_HEAP_EXTEND_AT=0x00c00000` and `DRAKAN_ENGINE_LIMITS_LOG` | 6 minutes, clean exit, no `bad size`; heap 13 MB above 32 MB; 16 cache trims, **0 bytes dropped**; pictures normal (`evidence/su-stress1_sheet.png`) |
| `membefore2` / `memafter2` | dist / B | Islands as `is-mem1`, 5 minutes | working set 1,367..1,462 MB / 1,593..1,638 MB, private 2,165..2,829 / 1,879..2,865 MB; exe B also carries the texture memory raise and cache settings merged from main, so the process numbers do not isolate this change. Guest RAM itself: dist commits and clears 64 MB at start; exe B commits 32 MB and then only what the heap reaches (0.1 to 3 MB in these levels) |

Host pages above 32 MB appear in the log as `[extended-heap] continued / grew: game heap N MB above the retail RAM ...
host committed N MB above 32 MB` (each new MB of peak), and `trimmed` when the allocator gives memory back.

## Tests

* `world_extended_heap_test` (replaces `world_terrain_page_pool_test`): the sbrk rule (retail below the end,
  continue at 32 MB past it, the probe's retail answer, growth, `sbrk(0)`, trim, never below the extended base, the
  limit), the probe and builder return addresses, the size word calloc reads, and the pool: no commit before the first
  block, whole-step growth, a large block in one commit, aligned blocks, give-back to one spare step, the limit, a
  refused commit, 40,000 random allocations and frees without overlap with the committed top following use.
* `guest_ram_reservation` (new, `scripts/tests`): reserve 256 MB, commit 32 MB, grow (rounded, monotonic, clamped),
  decommit and read zero again, the bytes below kept, a stray access at 200 MB committed by the fault handler, release,
  two reservations at once.
* `world_engine_limits_test`: the `auto` cache option.
* `bow_aim_test`: its "outside guest RAM" case moved to the new end.
* `ctest --preset runtime-tests` in the worktree: 196 of 196 passed (data or device tests skipped); the runtime source
  check preset built every changed hook file; `clang-format --dry-run --Werror` clean on the changed files; Python
  `test_live_drive.py` and `test_ootf_volcanic_route.py` pass (39 tests), ruff clean.
* Game builds: the worktree's own WorkDir, incremental after a full build; only the character motion pack step fails
  there (the worktree has no `editor/packages/formats` checkout), the exe links.

## Not verified

* No campaign level exit chain was played (Alwarren to Islands, Wartok to Grimstone): level loads were only the
  starts and the F8 quick-loads (four to five per run). Islands Night, Volcanic, Rift World and Alwarren were not played.
* The Surdana runs walked round the New Game start outside the town gate, not the town square the survey measured; no
  cache drop was seen, but the square itself was not visited.
* Flown Arokh over Wartok was not repeated (the 14 November runs flew it with the pool).
* Process working set before / after is not a clean comparison (exe B also carries the other agents' texture memory
  raise); the guest RAM part is 64 MB resident before, 32 MB plus the heap's reach after.
* `heap_extend = 0` with the pool (`terrain_page_pool`) on the new reservation was only exercised in the frame-time
  check, not played at the crash spots.
* POSIX and WebAssembly paths of the reservation compile-checked only by reading; not built.
* RAM dumps (F9, harness `dump ram_*`) still hold 32 MB, so objects in the extended heap are missing from them.
