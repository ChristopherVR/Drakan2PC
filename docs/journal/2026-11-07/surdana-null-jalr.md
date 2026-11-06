# Surdana: null JALR at 0x10aa6c and the silent hang after it

Log: `build/harness/cp_t1a/drakan.log` (recipe `build/harness/cp/t_1a.txt`, first `place 465987 159764 460554`).
`[guest-branch:missing-target] ... JALR source=0x10aa6c target=0x0`, then flips 0 for ~350 s while vblank stays 60.

## 1. Where it is

`0x10aa6c` is in `FUN_0010a980` (generated `f_0x10a980`, 0x10a980 - 0x10b010), a method of the controller object
(`this` = s1 = 0x18fd7d0) that the scene walk reaches through `0x109058` (`+0x170: 0x1090a8`, `+0x1d0: 0x140c30`).
Its job is to resolve a resource handle held in the controller (`this+0x50`, or `this+0x5c`) and update from it:

```
obj = 0x112118(0x4db050 /* resource cache */, handle)      // wraps 0x12b640(cache, {handle, 3, 1, 0})
if ((obj->flags & 3) == 3) ...ready path (0x10aa84)
else if ((obj->flags & 3) != 2) call obj->type->slot[+0x64](obj + adj)   // JALR at 0x10aa6c
else ... (loading) same call at 0x10aa50
```

`0x12b640` is the resource cache lookup (128 hash buckets, `(id * 0x59) & 0x7f`, see
`docs/journal/2026-10-31/level-change.md`). With the flag word `{handle, type 3, 1, 0}` its null exits are: the
handle's first word is 0 (0x12b664) or a miss that must not create (0x12b760). `0x10a980` never checks the result.
With `obj = 0`: `obj->flags` reads as 0, the type pointer `obj[0x18]` is 0, slot `[0x60]/[0x64]` of it is 0 and the JALR
goes to 0. That is exactly the log: `a0 = 0`, `vtableReadable=no`, `vtbl[*] = 0`, `v0 = 0`, `a2 = a3 = 1`.
The code after the call (`0x10aa74`) clears the handle field (`this+0x50` when a3 == 0, else `this+0x5c`) and leaves
through `0x10afd8`, i.e. the engine's own answer to "no resource" is to drop the handle.

## 2. Cause

A resource handle of an object in the scene walk whose resource is not resident in the cache. On the PS2 the same code
would jump to 0 as well; real play never gets there because the region around the player is resident. The `place` aid
moves the player 116000 units in one step (from 447901 171307 344205 to 465987 159764 460554), into an area whose
resources are still streaming in (the log shows `[resident-models]` being rebuilt right before the placement), so
the objects there reach the walk before their handle targets exist. It is timing dependent (streaming speed), hence
intermittent: 0 of 14 runs reproduced it here (14 runs of the recipe's first 12 s, two at a time, own exe copy
`build/h1a/play`; the two earlier runs and the rerun were also clean). Whether a long fast walk or a real level
change can reach the same state in normal play is not proven; the guest code is the same, and the miss needs a
handle whose target was evicted or never loaded, so it cannot be excluded. It is not a host data race: the `place`
hook runs on the EE thread at the PadMgr boundary (`drakan_playtest::beforePadMgr`), not concurrently with the walk.

## 3. Why it was silent

`reportMissingFunction` logs once and, with the default policy `ContinueToTarget`, leaves `ctx->pc = 0`. Every
`PS2_JALR`/`PS2_JAL` frame above sees `pc != ret` and returns, so the whole host stack unwinds to the scheduler. The
scheduler's `pc == 0` branch (`EeScheduler::run`) makes the main thread dormant without a word (a message only with
`PS2X_TRACE_RING`). The IOP/vblank/audio threads keep running: vblank 60, flips 0, process never exits.

## 4. Changes

* `EeScheduler::run`: the main thread ending after a missing-target report now prints
  `[fatal] main thread unwound to pc 0 after a missing branch target ...` and calls `requestStop()`, so a run ends in
  seconds with the call chain already in the log instead of waiting out the harness timeout.
  (`PS2Runtime::missingFunctionReported()`.)
* `drakan_null_call_recovery.h` + `dispatchGuestBranch`: the one known site (JALR at PAL 0x10aa6c, target 0) is
  skipped after the usual `[guest-branch:missing-target]` line, with `[recovered] ...`. Execution continues at
  `0x10aa74`, which clears the handle: the same outcome the engine takes for any unresolved resource. All other
  missing targets keep the strict behaviour. Test: `null_call_recovery_policy` (scripts/tests/CMakeLists.txt).
* `place` is unchanged: it cannot know which resources are resident, and with the recovery above a far placement
  no longer takes the game down at this site. A shorter hop or waiting for `[resident-models]` to settle would only
  narrow the window, and was not testable without a reproduction.
