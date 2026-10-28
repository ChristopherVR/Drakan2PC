# Main-thread death after the intro skip / during the Level 0 load: root cause and fix

Status 2026-10-17. Question (RECOMP_STATUS 4e): is the "movie player calls a destroyed listener" death (`[trace-ring] ... 0x440624 -> 0x0`) and the older
"null JALR at 0x12c4d8 during the Level 0 load" a game bug that also exists on the PS2, or a port bug?

**Answer.** Skipping the intro with START is normal and works. The deaths have two causes:

1. A **port bug** in the recompiled-code runtime, not in the game, not in the HLE timing and not in the scheduler priorities: a callee that a scheduler checkpoint unwinds is mistaken for a
   callee that returned (`PS2Runtime::dispatchGuestBranch` and the `PS2_JAL`/`PS2_JALR` macros). It causes the null JALR at 0x12c4d8 and the related pc=0/1/2 and 0x490bd4/0x12d430 deaths,
   and it can also derail the menu flow that then ends in the listener death.
2. A **latent game race** that the `DRAKAN_AUTOPRESS` test aid (START *and* CROSS in the same frame) can trigger during the New Game confirm: the pause menu opens over the loading game and the
   game's own "close all screens" destroys the freshly created intro screen while its movie job still holds a raw pointer to it. A real player would have to press START in the exact frame of the
   confirm; the port ignores such stale completions with a small guard.

Patches (against `tools/PS2Recomp`, branch `drakan` @ `2d5a10f`; `git am` or `git apply`):

| patch | what | needed |
|---|---|---|
| `patches/listener_fix.patch` | the runtime bug (3 files, 30 lines); `PS2X_UNWIND_FIX=0` restores the old behaviour for A/B tests | yes |
| `patches/listener_guard.patch` | `drakan_overrides.cpp`: ignore movie completions for GUI screens that no longer exist | recommended (hardens the game race) |
| `patches/listener_diag.patch` | env-gated tracing aids used for this work (`PS2X_TRACE_PCS`, `PS2X_TRACE_SCHED`, thread ids in the ring) | optional |

Results (boots with START skip: logo, New Game, intro skip with START, level load; `scripts/harness/listener_repro.sh`; dead = main thread lost; raw logs in `build/crash_listener/logs/`):

| runtime | stress mode (`DRAKAN_RES_LATENCY_US=0`) | default latency (2 ms) |
|---|---|---|
| before (old behaviour: `PS2X_UNWIND_FIX=0` on the patched binary, or the old exe) | **10 of 16 dead** (two batches of 8: 7x null JALR 0x12c4d8, 1x listener `0x440624 -> 0`, 2x main thread ended without a report); every dead boot had logged the faulty path; 4 of the 6 healthy boots logged it too (the wrong result is not always fatal) | 2 of 34 (1/8, 1/12, 0/14, + 1 handler-thread null JALR); the pre-perf exe 2 of 6 |
| `listener_fix` only | 2 of 32 (0/8, 1/10, 1/14; both deaths are the listener race, section 1.2) | **0 of 20** |
| `listener_fix` + `listener_guard` | **0 of 14** (the guard fired in 3 of them, each would have been a listener death) | |

## 1. The listener (what object is 0xd60b80, who registers it, who destroys it)

Not a level object: it is a **GUI screen** of the screen manager (`0x4fb600`, factory `FUN_002c4458(mgr, id, parent, args, active)`, table `mgr+0x10+4*id` for ids 0..0x20,
active screen `mgr+0xc`, "close all but one" = `FUN_002c3f38(mgr, keep)`). Layout: GUI window base ctor `FUN_002bb518`, vtable 1 at `+0x18`, **screen vtable at `+0x98`**
(slot +0x44 = message handler).

| screen id | what | size | vtable `+0x98` | ctor / deleting dtor | `+0x7c` (movie end) | close |
|---|---|---|---|---|---|---|
| `0x1e` | logo movie (INTERFCE.VDB id 4), opened by `FUN_0043e438` after the memory-card warning | 0xd8 | `0x54e928` | `0x440550` / `0x440528` | `0x440738` | `0x440960` -> opens the title screen 8 |
| `0x1d` | intro movie "D2 Intro Movie01" (LEVEL0RO.VDB id 6, resource `0xb0006`), opened by `FUN_003a6290`/`0x3a6510` when New Game starts the level | 0xe4 | `0x54e8a0` | `0x4405d8` / `0x440588` | `0x440200` (plays a sfx, flag 1 releases the job) | `0x440a10` |
| state after the dtor body | | | `0x54e9b0`, then the GUI window base `0x5297d8` whose slot `+0x7c` reads **0** | | | |

Common message handler `FUN_00440638` (`msg 3` open, `msg 0xa` per-frame tick -> `FUN_00440080`, `msg 0xb` Back/START -> `+0xa8 = 1` skip request; the logo ignores it).

* **Registration** (`FUN_0043ff00`, from the first tick): `job = movieService(0x4dbed8, vslot 0x8c)(name)` = `FUN_00177800` -> resource-cache object (`FUN_0012b640`, kept in the
  service at `+0x230`), then `job->vslot 0xb4(0x440610, screen)`: callback at `job+0xb8`, **argument = the raw screen pointer at `job+0xbc`** (no reference, no weak handle).
* **Delivery**: the decoder step `FUN_001767a0` (once per engine frame from `FUN_00177898`, after the GUI tick) calls `0x440610(screen, flag)` at `0x1768d4` (flag 0, prefetch)
  and `0x176a40` (flag 1, finished). `0x440610` = `(screen+0x98)->vslot 0x7c(screen+delta, flag)`.
* **Unregistration: none.** The dtors (`0x440500/0x440528/0x440588`) do not touch the job. The protocol is "the screen stays alive until it has *received* flag 1"
  (`0x440738` / `0x440200` -> `FUN_00440760` releases `job->vslot 0x64(0)`, clears `screen+0xac`), then the tick fades out (1 s, `FUN_002be6d8`) and closes the screen
  (`0x440960` / `0x440a10` -> `FUN_002c3f38` -> dtor). START: msg 0xb x3 -> `job->vslot 0x9c` (stop) -> the decoder drains its 6 in-flight frame reads (spin in `0x1767a0`) and stops
  the soundtrack voice (`FUN_001b82a8`) -> flag 1 after ~140 ms (100 ms guest) -> close ~1 s later.
  Healthy timeline (`t` = wall s, `DRAKAN_AUTOPRESS=4000`): logo 1.8, START taps at 3.8/7.8 ignored, natural end 10.7, close 11.7, New Game 11.9, slot screen 0x11 14.7, intro screen 0x1d 15.7,
  START skip 19.8, flag 1 at 19.93, close 21.0.
* **Who destroys it early** (two different ways):
  1. *The runtime bug* (section 3): in the boot that died with the listener signature under the old behaviour (`e0_f0_5`) the faulty path had fired first in the sound manager's voice search
     `FUN_001e6a28`; the menu flow then diverged (pause menu instead of the intro start, intro screen created late, listener `0xd53bb0` in the high heap like the original reports
     `0xd60a00`/`0xd60b80`) and the screen was gone when flag 1 arrived. I could not follow the exact chain from the wrong result to the destroyed screen.
  2. *The game race, reproduced with the fix applied* (`h_5`, `v_stress_4`, `g_2`): the START+CROSS tap lands while the New Game slot screen is up. CROSS confirms (`FUN_002c3f38` from `FUN_002ca030`
     at 15.77 s), START in the same frame reaches `Game_OnInputCommand` (`0x23ce18`, action 0x120) *after* that, the game is now "in level" and opens the **pause menu** (screen 0x17, `0x23d2d4`),
     CROSS then picks "Save Game" (the `[MC] GetDir` spam). When the level start finally runs (19.78 s) the intro screen is created (`0x3a6290` -> `0x2c4458(0x1d)`), its first tick starts the job
     (`0x43ff00`, 19.85 s) and 60 ms later `FUN_00417520` (reached from `FUN_002ca030`/`0x43b1e8`) calls `FUN_002c3f38(mgr, {0x11})`: every screen except 0x11 is destroyed, including
     the new intro screen (dtor `0x440588` at 19.846). Flag 1 at 23.98 s then calls through the recycled object: `0x440624 -> 0` (or a garbage target such as `0xbd3e7095`).
     This is the game's own order of operations and it would equally break on a PS2; it needs START in the frame of the New Game confirm, which `DRAKAN_AUTOPRESS` produces by design.

## 2. Threading model: what the PS2 guarantees, what the port does, and why it is not the culprit

Guest threads of Drakan (`PS2X_TRACE_SCHED=1`):

| tid | prio | entry | role |
|---|---|---|---|
| 1 | 0, lowered to **7** by the game | `0x100008` | main/game thread (also runs the VBlank interrupt handler `FUN_001d8590` as an invocation: it only `iWakeupThread(6)`s) |
| 2 | 1 | `0x1f11e8` (stack `0x56b8a0`, 1 KB) | SIF/RPC loop |
| 3, 4, 5 | 6, 5, 4 | `0x1f11e8` (8 KB stacks) | RPC servers of SID `0x1301` (read completion), `0x1302` (sound), `0x1303` (buffer allocation) |
| 6 | 2 | `0x1d78f0` (1 KB stack) | GS flip/DMA thread, woken once per VBlank |
| 7 | 3 | `0x1e6198` | RPC server SID `0x1310` (sdrdrv remote) |
| -1, -2, ... | the SID's priority (1301 -> 6, 1302 -> 5, 1303 -> 4, 1310 -> 3, client end functions -> 1) | the game's own handler (`0x1f0110`, `0x1f0550`, `0x1eff90`, `0x1e6208`, `0x4af488`) | dispatcher threads that run the HLE-posted EE RPC handlers |

* The EE kernel is priority-preemptive without time slicing. The servers (prio 1..6) are above the main thread (7), so on the PS2 a completion preempts the game at an arbitrary
  instruction exactly like in the port; the game protects its structures with `DI`/`EI` counters and counting-semaphore mutexes (`FUN_004a0208/260`, `FUN_004c4010/58`).
  `EeScheduler` honours the priorities (verified in the traces: a handler runs when it is queued, main resumes when it returns or blocks, tid 6 runs once per VBlank).
* Deviations that exist but are **not** the cause (checked): equal-priority time slicing (nobody shares prio 7), the 4M-cycle bound of the `DI` protection, several same-SID
  dispatcher threads (the HLE worker serialises per SID), interrupt invocations running on the interrupted thread, FIFO instead of priority-ordered semaphore queues.
* What the port needs is a correct **unwind**: a preemption is implemented by returning `false` through every generated call frame up to the scheduler loop and resuming from `ctx->pc`.
  That works only if every frame can tell "my callee finished" from "my callee was unwound". Section 3 shows it could not.
* The previous explanation ("completion overtakes the requester", RECOMP_STATUS 4e.3) described a symptom: each HLE completion that preempts main has a chance to hit a vulnerable function;
  the 2 ms guest-time latency only lowers how many completions land while the main thread polls the resource cache (stress mode: latency 0, almost all do).

## 3. Root cause of the runtime bug

```cpp
// PS2Runtime::dispatchGuestBranch (call case); the PS2_JAL / PS2_JALR macros of the newer emitter have the same test
const uint32_t entryPc = ctx->pc;           // == callee entry
targetFn(rdram, ctx, this);
if (ctx->pc == entryPc) ctx->pc = fallthroughPc;   // "callee left pc untouched: it was a stub that returned"
return ctx->pc == fallthroughPc;
```

A checkpoint (`checkpointDue()` true: queued HLE completion, VBlank, time slice) unwinds the host stack by `return` and leaves `ctx->pc` at the resume point. If the resume point equals
the entry of the *enclosing* callee, that call is taken as completed:

1. **Entry loops**: 35 recompiled functions loop back to their own first instruction (list walks, tail recursion such as the GUI hit tests `0x2bc428/0x2bc618`, the voice search `0x1e6a28`,
   `0x1e6a58`, `0x2cc8e0`, `0x1d68c0`, `0x1a49f0`, ...; list: `python scripts/archive/2026-10-17/find_entry_loops.py build/recomp_out/gen`). Preempted at the back-edge they return with `pc == entry`; the caller
   continues with a half-computed search result.
2. **Recursion through the call chain**: `0x12c3f0` (resource touch/release) -> `0x12b640` (cache lookup, may construct objects) -> ... -> `0x12c3f0`. The nested dispatch is unwound
   *before* it runs with `ctx->pc = 0x12c3f0`; the outer dispatch of the same address sees `pc == entryPc` and declares the outer call finished. The outer function's epilogue never
   runs: `$sp`, `$s0..$s4`, `$ra` stay those of the nested frames, the pending nested call is dropped and the caller carries on with garbage. That is the state seen at the null JALR:
   `s0 = 3`, `s1 = 0x4e0000` (the caller's values), later `pc = 0/1/2` after an `lq $ra` of a frame that was never pushed.

Evidence (`PS2X_UNWIND_FIX=0` prints the first occurrences, same binary):

```
e0_f0_2  callee 0x12c3f0 unwound at its own entry (call from 0x17a644)  -> boot died, null JALR 0x12c4d8
e0_f0_5  callee 0x1e6a28 ... (call from 0x1e5bf0)                        -> boot died, 0x176a40 -> 0x440610 -> 0x440624 -> 0 (listener)
e0_f0_6/7/8, v_old_1/3/7/8: callee 0x12c3f0 (0x17a644)                   -> died (null JALR 0x12c4d8 / ring flush)
e0_f0_1/4: no occurrence; e0_f0_3, v_old_2/4/6: occurrence, no visible harm  -> alive
fixed binary: the same path is hit (5 of 8 boots in e0_f1) and every boot survives
```

Why it looked like a timing/lifetime race: it needs an event to land in a vulnerable spot, so it is rare per boot, rises with the number of completions landing while main polls
(latency 0), the guest cycle counts of the boots are identical (only the injection point of the HLE completions moves), and the effects (wrong list result, skipped epilogue) surface
later as use-after-free style signatures. Seen signatures, all explained: null JALR `0x12c4d8` (release on a bogus object), `0x490bd4` and `0x12d430` (wrong list/record),
`pc = 0/1/2` after a bogus `lq $ra`. (`[guest-branch:missing-target] ... source=0x1/0x2` without a ring flush at `PS2X_MAX_SECONDS` was the same bug firing while the stop request
unwinds the stack; harmless at shutdown and gone with the fix.)

## 4. The fixes

* `listener_fix.patch`: `checkpointDue()` raises a global `g_ps2Unwinding` whenever it asks the guest to unwind; the scheduler clears it right before it (re)enters guest code. The
  dispatch code treats `pc == entry && g_ps2Unwinding` as "unwound" and returns `false` (propagate) instead of faking a return; real stubs that leave `pc` untouched are unaffected.
  The same condition is added to `PS2_JAL`/`PS2_JALR`. No game code, no timing change. `PS2X_UNWIND_FIX=0` = old behaviour + report.
* `listener_guard.patch`: `drakan_overrides.cpp` wraps the completion thunk `0x440610`: the callback is skipped (one log line `[drakan] movie completion (flag N) for destroyed GUI screen 0x... ignored`)
  unless the listener is in the screen manager table. Normal boots pass straight through to the original (verified: flag 0/1 callbacks reach `0x440738`/`0x440200`/`0x440760`).
  Justification: the game has no unregistration and the failing order is the game's own; ignoring a completion for a screen that no longer exists is the weak-reference behaviour it lacks.
  The guard fired in a stress boot (`g_2`) and the boot went on normally.

## 5. Other findings

* Commit `e0a14f1` of branch `drakan` did not compile (string literals with a raw line feed in `ps2_runtime.cpp` / `gs_gsr_bridge.cpp`); `2d5a10f` fixes it.
* `DRAKAN_RES_LATENCY_US` (2 ms guest-time HLE latency) can stay; it is no longer load-bearing.
* No `[bad-resume]` was ever logged: the `switch (ctx->pc)` resume tables are complete for the paths seen.
* `listener_diag.patch`: `PS2X_TRACE_PCS=hex,hex,..` (log selected call targets with tid and a0-a3, e.g. `2c4458,2c3f38,440610,43ff00,23ce18` = screen factory, CloseAll, movie callback/registration,
  input commands), `PS2X_TRACE_SCHED=1` (CreateThread with stack, HLE call queueing, run/preempt with pc/ra/sp/s0, missing function), thread ids and sequence numbers in the `PS2X_TRACE_RING` output.
  Typical session: `DRAKAN_AUTOPRESS=4000 DRAKAN_RES_LATENCY_US=0 PS2X_TRACE_RING=1 PS2X_TRACE_PCS=2c4458,2c3f38,440610,43ff00,23ce18 drakan.exe ...`.
* Facts that came up: tid 6 (GS flip) has a 1 KB stack at `0x6c6060`; `FUN_0012d608` is a constant-3 getter (resource state "loaded"); `FUN_001f0918` (request issue) throttles on
  `GetThreadId() == main` (dispatcher threads with negative ids behave like server threads there).
* A scripted-input reproduction of the game race (`scripts/listener_race.txt`) did not work: the START bit of the script is not delivered to `Game_OnInputCommand` while the slot screen is up;
  use `DRAKAN_AUTOPRESS=4000` + `DRAKAN_RES_LATENCY_US=0` (about 1 boot in 10).

## 6. Finding more of these

`scripts/archive/2026-10-17/find_entry_loops.py` lists the entry-loop functions of a generated tree. Recursion is covered by the same fix.

## 7. Repro and validation

```
scripts/harness/listener_repro.sh <exe> <boots> [tag] [seconds]                      # DRAKAN_AUTOPRESS=4000: logo, New Game, intro skip with START, level load
DRAKAN_RES_LATENCY_US=0 scripts/harness/listener_repro.sh <exe> 10 stress 30          # stress mode: frequent without the fix
PS2X_UNWIND_FIX=0 DRAKAN_RES_LATENCY_US=0 scripts/harness/listener_repro.sh <exe> 8 old 30   # same patched binary, old behaviour (expect ~5 of 8 dead)
```
Success criterion: `0 of N boots lost the main thread`. DEAD = a flushed `[trace-ring]` (main thread ended) or a `missing-target` report with a garbage target >= 0x10 (the listener call through a
recycled screen); a tiny-target report without ring flush is only the shutdown artifact of the unfixed runtime. Scripted-input boots (docs/reference/input.md section 11, START taps only) are unaffected.
