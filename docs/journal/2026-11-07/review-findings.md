# Correctness review of the 2026-11-06/07 commits

Read-only review (no builds, no game runs). Findings are ranked; each was checked against the surrounding code.
Checked and found sound: the PolyClip.h math (homogeneous lerp of x, y, z, fog, plain s/t and colour is right because
the emitter forms st = (s*q, t*q, q) itself; buffers of 24 HVs hold the 8-vertex worst case; non-finite and q == 0 inputs
are left alone), the CPU affinity module (64-bit masks, empty and oversize inputs handled), the loud script parser (inert
unless a line is rejected, opt-out env var), AUTOAIM / ALT / WAYPOINTS / WATCH (every path is gated on its env var,
parseWaypoints accepts null), the null-call recovery gating (exact call site, target 0, identity builds only),
FileService read overlay offsets (cumulative `o.pos + done`, partial overlap handled per range, runs under the file lock).

## 1. HIGH: polyclip hook ignores guest preemption inside the original fan draw

`ps2recomp/ps2xRuntime/src/lib/drakan_polyclip.cpp:107-127` (`hookFanDraw`, clipped path).

The hook calls `g_orig` once per fan triangle and then unconditionally continues the loop, restores `a1`/`a2` and sets
`c->pc = pc` (the entry address). The original 0x146880 calls the GIF emitter through `PS2_JAL`, which starts with
`cpTick(8)`. When a checkpoint is due (vblank, pending event, slice end; `EeScheduler::cpTick` sets `g_ps2Unwinding`) the
frames return upward immediately with `ctx->pc` = the resume address (0x1d9de8 or a label inside 0x146880) and the
partly built frame (sp lowered, ra = address inside 0x146880) still live. The hook does not look at that: it overwrites
`pc`, `a1`, `a2` and may call the original again.

Scenario: a clipped ribbon is drawn while a checkpoint fires. The scheduler later dispatches at pc 0x146880 with the
original (unclipped) a1/a2 and a stale ra/sp: the unclipped polygon is drawn (the starburst returns), the frame is entered
twice (sp leaks), and the return lands inside 0x146880 or the wrong caller. Result: random crash or corrupted guest stack
in effect-heavy levels (Stratos clips about 1.2 percent of fans, thousands per run; vblank checkpoints happen every
frame, so a hit in a long session is likely, not hypothetical). The unclipped fast path is unaffected.

Fix: after every `g_orig` in the clip loop test for completion before touching state. Save `const uint32_t ra =
GPR_U32(c, 31)` at entry and after each call `if (c->pc != ra) return;` (this also covers `g_ps2Unwinding`; do the
`finish()` bookkeeping first). On a normal finish set `c->pc = ra` instead of the entry pc (the PS2_JAL macro tolerates
`pc == target`, but a caller that reaches 0x146880 by tail jump or dispatchGuestBranch would see a bogus unwind). Losing
the remaining triangles of a fan on that rare preempt is acceptable. Add a test that forces `g_ps2Unwinding` inside a
stubbed original.

Related, MEDIUM-LOW: the scratch vertices live at `sp - 4608 .. sp - 4096`; a callee frame chain deeper than 4 KB below
sp (0x146880 -> 0x1d9de8 -> ...) would overwrite vertices mid-draw. The journal says "a few hundred bytes" but nothing
asserts it. Use a reserved RDRAM scratch area instead of the stack.

## 2. MEDIUM: desktop close hook can close before the page has saved, and swallows a failed save

`editor/packages/desktop/src-tauri/src/lib.rs:73-84`, `editor/packages/app/src/platform.ts` (`onCloseRequested`).

a) `state.closing` is set on the first close request and never reset. A second click on the close button (or Alt+F4)
during the up to 3 s wait passes straight through (`swap(true)` returns true) and the window closes while `flushAll()` is
still writing: the loss the hook was added to prevent. Fix: on a repeated request keep `prevent_close()` while a flush is
pending; reset `closing` when the wait ends so a later close is handled again.

b) `fn().catch(() => undefined).finally(() => invoke('close_ack'))` acks even when the autosave or the project write
failed (disk full, folder gone, permission), so the window closes with no message and the work is lost. Fix: do not ack on
error; show it and let a second close request mean "close anyway".

c) The 3 s cap (`ACK_WAIT`) closes the window during a long write (large level autosave in IndexedDB, project folder with
many assets). Fix: keep waiting while the page sends a heartbeat; use the cap only for a page that never answers.

## 3. MEDIUM: Play pack restore can overwrite a newer mods.ini or interfere with another instance

`editor/packages/desktop/src-tauri/src/playpack.rs:26-53`, `commands.rs` (`watch_play_pack`, `restore_play_pack_at_start`,
`game_launch`).

a) Restore writes the saved `mods.ini` text back wholesale. Anything changed between Play and the game closing (the player
toggling a pack in the game's Mods menu, which writes mods.ini; a second editor window installing a pack; the game's own
"new pack found" bookkeeping) is silently reverted. Fix: restore only the keys Play changed (the Play pack entry and the
`ootf*` entries it switched off) with line edits on the current file, or skip the restore when the file is no longer the
text Play wrote.

b) Two editor instances: starting a second editor runs `restore_play_pack_at_start`, which rewrites `mods.ini` and tries to
delete `editor-play-ootf.zip` while the first editor's game is running. On Windows the delete fails because the zip is
open, but mods.ini is already reverted and `?` returns before the backup file is removed. Fix: a lock or pid file in the
config dir; restore at start only when no recorded game process is alive.

c) `game_launch` can fail after the zip and backup were written (`cmd.spawn()` error): no watcher is started, the player's
own `ootf*` packs stay switched off and the Play pack stays installed until the editor restarts. Fix: on spawn error call
`playpack::restore`.

d) `save_backup` keeps the first backup forever. If it belongs to a different game folder than the current one, the new
Play zip in the new folder is never removed and the new mods.ini is never restored (restore targets the old `mods` path).
Fix: compare `backup.mods` with the current mods folder; restore the old one first, then take a fresh backup.

e) When there was no `mods.ini` before Play, restore writes an empty file instead of removing it (`Backup.ini` should be an
`Option<String>`).

f) `std::fs::write` of mods.ini is not atomic (kill during restore truncates it); use temp file plus rename.

## 4. MEDIUM-LOW: persisted converted levels are replaced, not merged, and not tied to the disc

`editor/packages/app/src/worker.ts:393-397` and `:295-297`, `editor/packages/app/src/virtualstore.ts`.

a) `addVirtualLevels` accumulates into `convertedPack` / `virtualFiles` in memory, but `saveConverted({ levels, files })`
stores only the arguments of this call and clears the store first. A second import in one visit (another language tree,
re-import after a fix) is complete in memory but after a reload only the last batch is restored. Fix: persist the merged
state or merge inside `saveConverted`.

b) The restore in `open(file)` is not keyed by the disc (volume id / language). Opening a different ISO applies the old
files (`patchedContainers(baseIso, language, ...)`) to the wrong base, and Play would install a pack converted against
another disc. Store volume id and language in META and discard on mismatch.

c) `loadConverted` uses `getAll()` / `getAllKeys()` for a pack described as hundreds of megabytes: the whole store is
materialised at once (about 2x peak memory in the worker). Use a cursor and apply each file as it arrives. A write that
exceeds the quota is reported only at commit and is swallowed by `.catch(() => undefined)` with no message; a toast
("kept for this visit only") would avoid surprise after the next reload.

## 5. LOW: stale `missingFunctionReported` turns a normal main-thread exit into a "[fatal]" stop

`ps2recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp:306-314`, `ps2_runtime.cpp:1601-1617`.

`reportMissingFunction` sets the report-once flag before the recovery in `dispatchGuestBranch` runs, and also for every
benign missing target that a skip policy handled earlier. If the main thread later reaches pc 0 for any other reason (a
legitimate return from main) it logs "[fatal] ... missing branch target" and calls `requestStop`. Today that only mislabels
an exit, but a future restart-main-thread path would be killed. The flag itself is atomic (acquire/release), so there is no
race. Fix: record the last unhandled missing target (set only when no recovery or skip policy handled it) and test that.

The recovery cannot mask other bugs (exact PAL call site, target 0, logged at 1, 2, 3, 4, 8, ...), but it only helps the
PAL build (`identityAddresses`); other regions still die at the same site. The skip leaves `$v0` at its pre-call value; the
follow-up code at 0x10aa74 does not read it per the journal, so keep a regression test.

## 6. LOW: alloc RPC wait has no visible signal for a hung host call

`ps2recomp/ps2xRuntime/src/lib/drakan_overrides.cpp:493-513`.

While `isExecutingGuest()` is true and no cycles are published (EE thread blocked in a host call: deadlocked GS worker,
driver hang) nothing is charged, so the only bound is the 600 s `kBackstop`; a real hang looks like a 10 minute frozen boot
with a single log line at the end. Fix: log once after 30 wall seconds of "EE blocked in a host call" (with the sampled
chain from ps2_perf when enabled). The 250 ms per-slice charge is fixed even when `wait_for` overshoots; negligible.

## 7. LOW (debug only, PS2X_PERF): stall dumper can deadlock or crash the perf thread

`ps2recomp/ps2xRuntime/src/lib/ps2_perf.cpp:66-110`.

`sampleThreadChain` suspends a thread and then calls `GetModuleHandleExW`, `GetModuleFileNameA`, `std::string` appends and
`snprintf` while it is suspended: if the suspended thread holds the loader lock or the process heap lock the perf thread
blocks forever. The fallback `*reinterpret_cast<const DWORD64*>(c.Rsp)` dereferences an unchecked stack pointer and would
fault in the perf thread, taking the process down. Fix: capture only raw RIPs into a fixed array while suspended
(`RtlLookupFunctionEntry` / `RtlVirtualUnwind` take no loader lock), resume, then resolve module names; validate Rsp with
`VirtualQuery`.

## 8. LOW: converter, game and tooling edge cases

* `editor/packages/formats/src/ootfmodel.ts` `remapObbPolygons` (f5d3c6b): the per-shape polygon count is written with
  `setUint16(o + 48, length)`. Indices are range checked (`> 0xffff` returns null) but the list length is not; a shape with
  more than 65535 entries after quads are split would be truncated silently. Add `lists.some(l => l.length > 0xffff)` to
  the null condition.
* `editor/packages/formats/src/terrainblob.ts` (b4deb22): a single cell over the 8-texture limit keeps its own texture plus
  the neighbours' most used ones and drops the others from the blend. Visual only; add a converter report counter so
  authors can see how many cells were reduced.
* `ps2recomp/ps2xRuntime/src/lib/drakan_levelchange.cpp:445-473` (`bringDragon`): the target (4000 beside, 3000 above
  Rynn) is not collision checked, so at an exit near geometry the dragon can end up in a wall; the transition then adds the
  maintain-position offset on top. The 100000 unit stray test applies to every campaign, including custom editor levels
  where an author deliberately parks the dragon far away; `DRAKAN_LEVELCHANGE_KEEP_DRAGON` is the only escape. Suggest a
  per-level opt-out or limiting it to levels whose dragon is on the stage.
* `runtime/input/src/CpuAffinity.cpp`: `cores:N` takes the lowest N allowed bits (N/2 physical cores on SMT systems), as
  documented, but worker pools sized by `std::thread::hardware_concurrency()` (GS tile workers, texture upload, FMV
  decoder) ignore the mask and over-subscribe it. Size them from the process affinity popcount.
* `ps2recomp/ps2xRuntime/src/lib/drakan_input_glue.cpp` (rejected script line): `std::exit(2)` from inside game thread
  startup with other threads live; prefer the existing stop path so logs and audio shut down cleanly.
* `ps2recomp/ps2xRuntime/src/lib/drakan_playtest.cpp` autoSteer: `DRAKAN_PLAYTEST_ALT=""` counts as set and pins the height
  to 0 (`hasAlt` tests only `getenv != nullptr`); `ix > nClasses` should be `>=`. Only matters with the env vars set.
* `runtime/hle/src/ResourceOverride.cpp` `patchRead`: logs one line per patched read; the credits are read in many small
  chunks, so cap the log to the first few.

## Resolved

Runtime findings fixed in a follow-up (editor items 2 to 4 follow below).

| Finding | Resolved | How |
| --- | --- | --- |
| 1 polyclip preemption | yes | The clip loop (`drakan_polyclip_fan.h`) stops at the first original call that did not return normally and the hook returns without touching `pc`, `a1`, `a2` (the scheduler re-enters the live frame later; the rest of that fan is dropped). Normal finish restores the entry `pc` as before. Scratch vertices moved from `sp - 4 KB` to 64 slots of reserved low RDRAM (0xf0000, below the ELF; checked zero once, else clipping is off), keyed by the caller's stack. Test `polyclip_fan_policy` forces a mid-draw unwind in a stub. |
| 5 stale missing-function flag | yes | `missingFunctionReported()` now reads a flag set only when no recovery or skip policy handled the target. |
| 6 alloc RPC hang signal | yes | One log line after 30 s of "EE blocked in a host call" (no cycles published while executing guest code). |
| 7 stall dumper | yes | Raw RIPs captured into a fixed array while suspended; the stack fallback read uses `ReadProcessMemory`; names resolved after resume. |
| 8 remapObbPolygons | yes | Lists over 65535 entries return null (shape lists kept as authored) and a one-time warning is logged. Formats commit b0f7cdd. |
| 8 terrain over 8 textures | yes | `TerrainBlobOptions.stats.reducedChunks` counts them; the converter warns once. |
| 8 bringDragon | partly | Skipped for campaign pack and editor levels (`isCampaignPackLevel`), the 100000 unit rule is documented in `drakan_dragon_arrival.h`. No collision query is available there; the dragon is placed 3000 units above Rynn so he drops onto open ground. |
| 8 worker pools vs affinity | yes | `gsr::usableCpus()` (affinity popcount on Windows and Linux) sizes the GS tile workers, texture upload pools, ray workers and the FMV decoder. |
| 8 rejected script line | yes | `requestStop()` instead of `std::exit(2)`. |
| 8 playtest | yes | Empty `DRAKAN_PLAYTEST_ALT` is unset; `ix >= nClasses`. |
| 8 patchRead log | yes | First 8 patched reads are logged. |
| 2 desktop close hook | yes | `closing::CloseGate`: a repeated close request is swallowed while the page is saving (no early close, nothing stacks) and the gate resets when the attempt ends. The page (`closeflow.ts`) sends a heartbeat (`close_beat`) every 500 ms; the shell gives up only on 3 s of silence or 180 s of work, and a question to the user has no limit. A failed autosave or project write is not acknowledged: a dialog offers Try again, Close anyway or Keep editing (`close_cancel`). Rust unit tests for the gate, `closeflow.test.ts`. Done in the desktop app (debug build, WebView2 remote debugging, window closed with WM_CLOSE): an 8 s flush survives two extra close clicks and the old 3 s cap, closes after the flush; a failing flush shows the dialog, extra clicks are ignored, Keep editing keeps the window, the next close asks again, Try again then closes, Close anyway closes. Found by doing it: the new commands must be listed in `build.rs` and `capabilities/default.json` (otherwise the page cannot call them), and the dialog must not wait for its own close event (not delivered while the window is in the background). |
| 3 Play pack restore | yes | Backup keeps the text before Play (`None` when there was no file) and the text Play wrote; restore reverts only the keys that differ, and only if the key still holds Play's value, as line edits on the current file (CRLF kept; an untouched file returns byte for byte). A lock file held open without sharing by the editor that started Play stops a second editor from restoring at start. A spawn error restores at once; a locked zip leaves a pending backup that is retried every 2 s for 3 minutes; a backup of another mods folder is restored there before a new one is taken; a mods.ini Play created is removed; all writes go through temp file plus rename. 14 Rust tests. Done in the desktop app with a stand-in game and two editor instances: the second instance left mods.ini, zip and backup alone while the first one's game ran; a pack added meanwhile survived the restore after the game closed; a failing launch restored within seconds; a Play-created mods.ini was removed. |
| 4 converted levels persistence | yes | `saveConverted` merges levels (by directory) and files into the store of the same disc (volume id + language in META; a different disc or language is replaced on the next save and never loaded), `loadConverted` reads in transactions of about 48 MB through a cursor instead of getAll/getAllKeys, and the worker keeps the failure (`convertedStoreWarning`) so the page shows a plain-language toast (no room left / blocked). Tests with fake-indexeddb (`virtualstore.test.ts`) and the same calls in real Chromium via Playwright (accumulation, disc switch, 30 files in 3 MB batches). Not exercised for real: an actual quota overflow (a 2 GB allocation crashes the tab); the message is unit tested. Switching the language after opening does not re-derive an already restored pack; reopening the disc does. |
| 8 editor items | already fixed | `remapObbPolygons` and the terrain counter were handled above; nothing else in the findings concerns the editor. |
