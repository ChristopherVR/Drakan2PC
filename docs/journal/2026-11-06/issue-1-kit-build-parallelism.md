# Issue 1: builder kit verification, compiler parallelism and memory

Follow-up to [issue-1-kit-build-crash.md](issue-1-kit-build-crash.md). Reporter: Ryzen 9900X3D, 24 logical cores,
63 GB, `-CpuAffinity 0xFF`; the builder printed `2 project(s) x 4 compiler process(es)`.

## Kit verification with the real embeddable Python

A kit snapshot of 23882fdb was staged in `build/kit-verify/stage` with `package_builder_kit.py`'s own functions and the
pinned `python-3.12.10-embed-amd64.zip`, its `python312._pth` untouched (`python312.zip`, `.`, `#import site`). Every
script the two builders run passed `--help`, and `prepare_iso.py` (with `--detect`), `prepare_local_runtime.py`,
`prepare_launch.py`, `stage_texture_pack.py`, `character_previews.py` and `install_private_mods.py` ran with real
arguments (the PAL ISO, outputs under `build/kit-verify`). The fix in 23882fdb holds.

Step 6 then failed in the next script, `character_motion.py`, for two reasons the reporter would have hit right after:

1. `git archive` leaves submodules out, so the kit had no `editor/packages/formats` and
   `character_motion_retarget.ts` failed with `ERR_MODULE_NOT_FOUND` (`ootfanim.ts`).
2. The script needs Node.js 22.7+. The installer did not ship it and `Build-Drakan.ps1` did not check for it; without
   Node it stops with "Node.js 22.7+ is required".

Fixes: `package_builder_kit.py` adds each submodule's files at the pinned commit (still through the repository guard),
the installer bundles Node.js v24.22.1 (`installer.lock.json`, SHA-256 from nodejs.org), `Build-Drakan.ps1` puts
`tools/node` on PATH and lists Node.js as a missing prerequisite otherwise. With only the bundled Node on PATH, step 6
ran to the end (previews 1 s, motions 17 s, `drakan-character-motion.zip` 28.5 MB installed and activated).

The installer build now runs `smoke_test()`: every builder `.py` with the bundled Python and `--help`, plus
`character_motion_retarget.ts` under the bundled Node (with no arguments it stops after its imports). It refuses to
package when anything fails; with the submodule removed it reports the `ERR_MODULE_NOT_FOUND` line.

## What the reporter saw, measured

Local samples (16 logical cores, `build/final-game`, `_CL_=/MP4`, `/m:2 /p:CL_MPCount=4`, as the builder runs it), every
`cl.exe` with parent, threads, CPU and working set once a second.

**(a) 5 cl.exe, one at 0 % CPU.** That is one project compiling with `/MP4`: the `cl.exe` MSBuild starts is a driver
(10 MB, 0 % CPU) that starts 4 worker `cl.exe`, one per file. The second MSBuild project (`/m:2`) only helps early on:
`ps2_generated` (the 82 generated files) links `ps2_runtime`, so it starts after it, and nothing else is left to build
beside it. 4 workers still fill 8 threads because each worker's code generator (`c2.dll`) is multi-threaded: workers
ran 8 to 12 threads and up to 6.6 cores each (10 files: mean 5.5, peak 12.6 busy cores for 4 workers).

**(b) One cl.exe on the VU1 slices with all 8 threads busy.** Expected. With `-SerialVuSlices` each slice is its own
MSBuild batch and a batch of one file is compiled by a single `cl.exe` (parent `Tracker.exe`, no `/MP` children).
`ps2_vu1_aot_0.cpp` alone: 11.7 min, 9 to 11 threads, up to 8.1 cores, mean 4.2. Per-thread times while it ran: the main
thread idle, five worker threads at 100 %, `c1xx.dll` and `c2.dll` loaded, so the busy part is the multi-threaded back
end (code generation of the large per-pair functions), not single-threaded constant evaluation. `ps2_runtime` is not
built with `/GL` in the Visual Studio generator (`EnableFastReleaseMode` only runs when `CMAKE_BUILD_TYPE` is set), so
this is compile-time code generation, not LTCG.

**(c) Memory.** VU1 slice: 12.4 GB peak working set for slice 0 here; slice 3 peaked at 16.6 GB earlier
(2026-10-24/setup-installer.md), the reporter's 17.6 GB is the same. Generated files: the 8 largest
`ps2_recomp_*.cpp` (1.9 to 2.9 MB) peaked at 1.3 GB per worker and 3.9 GB for all four; 10 typical ones at 1.0 GB and
2.8 GB. The reporter's ~3.5 GB for the small jobs matches. The "6 GB per compiler process" estimate came with the first
commit and was never measured; it is about 4.5 times too high.

## Sizing against the affinity mask

`Build-Drakan.ps1` sized from `[Environment]::ProcessorCount` before `Build-LocalGame.ps1` applied the mask, so the mask
never entered the sizing. For the reporter it did no harm by luck (min(4, 24/2) = 4, times 2 projects = 8 = the mask),
but `0xF` would also have given 2 x 4 on 4 processors, and `-Cores 12` with `0xFF` 2 x 6 on 8.

Now: `Get-BuildAffinityCount` (Build-Affinity.ps1) counts the mask's processors and `Get-BuildParallelism`
(new Build-Policy.ps1) sizes from that count: automatic is half the usable processors, at most 4; `-Cores` is capped at
the count; 2 GB per generated-code process instead of 6 (2 x 4 now needs 31 GB instead of 47). The console says e.g.
`CPU: automatic (8 of 24 logical cores, from the affinity mask)`. `Build-LocalGame.ps1`'s default `-CompilerProcesses`
also uses the mask's count when a mask is given. For the reporter's setup the result is unchanged, 2 x 4.

Not changed: the generated-code project could use more `/MP` workers while the second MSBuild project has nothing to
build (machines with many free cores and no mask stay at 4 workers, about 9 busy cores of 24). Worth trying later.

## Tests

- `scripts/tests/test_kit_tools.py`: the pinned embeddable Python with its shipped `._pth` (unpacked by
  `package_builder_kit.py --fetch-tools build/kit-tools`, skipped otherwise) runs the same `smoke_test()` as the
  installer build. Fails with `ModuleNotFoundError` when the 23882fdb fix is removed.
- `scripts/tests/test_build_drakan.py`: every `scripts/buildtool/*.ps1` parses; the retry policy (moved into
  `Invoke-BuildWithRetry`) against a stub `Build-LocalGame.ps1`: a play-folder, translate or runtime-file failure is not
  retried, a compile failure is retried once with 1 project and half the compiler processes, a second one stops;
  `Build-Drakan.ps1 -CheckOnly` from a temporary copy with a synthetic ISO; `Build-LocalGame.ps1 -PrepareOnly` with a
  synthetic ISO gets through all preflight and is refused by the ISO reader.
- `scripts/tests/test_build_affinity.py`: mask counting and the sizing table, including the reporter's machine.
- CI (`build-tools.yml`, Windows): submodules checked out, Node 22, kit tools unpacked before `run_tests.py`.

The measurement rebuilt `ps2_runtime.lib` and 17 generated objects in `build/final-game` (inputs touched, no other
change); the next incremental build relinks `drakan.exe`.

## Draft reply (not posted)

> Thanks for the log and the detailed notes, they helped a lot.
>
> **Build crash.** Fixed: `character_previews.py` could not find its helper modules under the bundled Python. While
> testing the fix with the kit's own Python I found the next step would have failed too: the multiplayer motion step
> needs Node.js and a folder the kit was missing. The next kit includes both, and the builder now runs every build
> script with the bundled tools before a kit can be packaged. A failure outside the compile step also no longer starts
> a second "less parallel" attempt, so a script error stops right away instead of after another pass.
>
> **5 compiler processes, one at 0 %.** Expected. With 4 compiler processes per project, Windows shows one extra
> "Microsoft C/C++ Compiler Driver" that only hands out files and waits, so it sits at 0 %. The second project slot
> has nothing to do at that point because the big generated-code project has to wait for the runtime library. Each
> of the 4 compilers uses several threads while generating code, which is why your 8 threads were still full.
>
> **One process on ps2_vu1_aot_0 to 3.** Also expected. Those four files compile one after another on purpose, because
> each needs up to about 17 GB; side by side they would need about 70 GB. A single compiler still keeps around 8
> threads busy in its code generation phase, which matches what you saw.
>
> **Memory.** You were right: 6 GB per compiler process was far too high. I measured about 1.3 GB per process on the
> largest generated files (your 3.5 GB for the group matches). The 17 GB for the VU1 files is real. The estimate is now
> 2 GB per process.
>
> **CPU affinity.** The builder sized the number of compilers from all 24 logical cores, not from your mask. With
> 0xFF it happened to pick the right amount (2 x 4 = 8), but a smaller mask would have started too many. It now sizes
> from the processors in the mask and says so in the "CPU:" line.
>
> These changes will be in the next release.
