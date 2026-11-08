# Issue 5: the Volcanic rift box, the one-process run to the credits, Delon with the carried blade, Arokh's flights

Follows [the rift sequence note](ootf-rift-sequence-delon-tunnel.md) (section 3, "the rift box does not open after the
Runeblade") and item 8 of [outstanding work](../2026-11-07/outstanding-work.md). Private runs under `build/ootf12/h/` (own
executable copies in `build/ootf12/play`, sha256 `5c63cef4...` the 16:27 build, and `build/ootf12/play2`, `81a206d8...`
the `build/final-game` build), other agents' games left alone.

## 0. Inputs rebuilt

The clean-up had deleted `build/ootf/cab_disc` and `build/odbx/data`. Both were rebuilt from the user's own files:

* the PC install: `python analysis/scripts/ootf_cab.py "<Downloads>/Compressed/Drakan Order of the Flame/Drakan.zip" --out
  build/ootf/cab_disc` (337 files, the retail disc image `DRAKAN.mdx` in the zip, no patch applied);
* the PS2 inputs: `extract()` of `scripts/harness/ootf_level_run.py` on the ISO in the game folder (template `LEVEL1NA`,
  89 files in `build/odbx/data`, the host MOD / TXD in `build/odbx/hosts`).

Packs (all `--template LEVEL1NA --campaign 1`, **no `OOTF_TEST_*` variable set**), each converter tree a `git archive` of
the main commit that pinned that formats commit, with its own `node_modules` junctions:

| Pack | formats | main (editor-core, scripts) | Files |
| --- | --- | --- | --- |
| `pack-v9` | `918de7e` (2026-11-06 route) | `6a2b8414` | 10,698 |
| `pack-f5` | `f5d3c6b` (2026-11-07 `mkrw4`, `full2`) | `6fdd97eb` | 10,698 |
| `pack-c` | `9e1856f` (current pin) | `HEAD` (`a87d79aa`) | 10,751 |

## 1. The rift box: no pack or executable regression, the blade was never taken

**Static comparison.** In Volcanic the Runeblade 2239, Timer 2331 and the boxes 2318 / 2961 are identical in `pack-f5`
and the current pack (objects, links, parameters, the Runeblade's class record); `pack-v9` differs only by the PC logic
defaults that `34ddc63` added (the timer's "Time Until Trigger" 1 instead of the PS2 5, "Disable/Re-Enable Message" 4, the
boxes' "Detect Only Once?"). The registry slot of an object is its index in the level's object list (Runeblade 1104,
Timer 2331 1158, box 2318 1147, twin 2961 1685), the same in every pack.

**The same step on the 16:27 executable** (`vrf5` on `pack-f5`, `vrhd` on the current pack; the recipe of the earlier `vr5`):
both fail as `vr5` did, and in both the Runeblade is **not in the inventory** after the E taps (carry cells: only the
cage key `0x346c3`), the blade's handle stays in the world and Timer 2331 stays in the registry. The `took` screenshot
shows why: the blade is still inside the ice on the pillar. The recipe skipped the fire-arrow puzzle (sanctum switch,
button, arrow, melt), so E at the blade reaches through the ice only sometimes.

**Re-reading the earlier runs** with the carry cells and slot 1158:

| Run (2026-11-08, `build/ootf11`) | Runeblade carried after E | Timer 2331 after |
| --- | --- | --- |
| `vrw1` | yes (`0x346a3`, cell 1) | **gone** (fired and destroyed, "Destroy After Timeout?") |
| `vr4` | yes | **gone** |
| `vr2`, `vr5`, `vr6` | no | still there |

So the pickup does start the timer whenever it happens; the runs that tested the box (`vr2`, `vr5`) were the ones in
which the blade had not been taken, and the run that took it (`vrw1`, `vr4`) never reached the box (no Arokh in reach). The
note's "RAM in `vr5` and `vr6`: the pickup never started the timer" was a pickup that never happened, and the "cell 1 the
Runeblade" evidence came from `vrw1`, not from those runs. Both earlier passes that opened the box took the blade outside
the ice: `vr14` / `full2` after the melt, `mkrw4` from a pack that moved the blade next to Arokh. No converter or runtime
change is needed for the box.

**Positive check on the clean pack** (`pack-c`, 16:27 executable, the route of section 2 in segments from quicksaves): after
the melt Rynn took the blade with the first E on the pillar top (`r9`: carry cells `0x346c3`, `0x346a3`; Timer 2331 in slot
783 before, gone after), walked into Arokh from his side, and mounted in box 2318 (`r10`): message box, save screen, one
Cross, Rift World gameplay at (342418, 100119, 319535) with both items still carried.

## 4. Arokh flying off, and "E does not call him"

Read in the PAL ELF (Ghidra decompile, scratch only) and checked against the class data:

* **Calling is a hold, not a tap.** Command `0x4009` (pad L1 / key E) arms a 0.5 s timer on press (player `+0xb5c`); a
  release inside that window is Use (items, levers, locks, talk). If the key is still held when the window runs out, the
  player tick calls the same routine as command `0x4008`, the call: it plays Rynn's call line and sets the dragon's call
  flags (`+0x590` bits `0x200004`), and the dragon tick then turns him to fly to her and plays one of his "Call Response
  Sounds". The `vr3` / `vr4` missions tapped E (one input sample) ten times: ten Uses with nothing to use (Rynn's "nothing
  here" line), never a call. So "E did not bring him back" was the test, not the game.
* **Flying off is his native follow and wander logic.** The rider-less dragon tick reads the class field "Follow Human
  Distance (lu)" (field 147, times 2,048): when Rynn is farther than that he takes off and flies to her, and lands where
  he can; when she is near he idles and every 15 to 25 s may wander. The converted Volcanic dragon has the PC value, 10 lu
  (20,480 units), which is also the PS2 class default. Detectors with "Does Cave Entrance Teleport?" send him to meet her
  at a cave marker ("Meet At Cave Entrance"). The PC dragon class has the same fields with the same values (Follow Human
  Distance 10, Call Response Sounds, Can't Land Sounds, Meet At Cave Entrance Sounds), so the behaviour is the original
  design carried by the original data, not a converter or engine fault. In the earlier runs Rynn was teleported 290,000
  units to the blade and back; he followed her as far as he could land and was then 26,000 units from the cage platform
  on the ledge at (272,000 155,500 472,000), which she could not walk to.
