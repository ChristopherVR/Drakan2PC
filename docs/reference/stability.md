# Stability audit: crash signatures, instruction-level differential tester, race stress (branch `drakan-stab`)

Status 2026-10-17. Scope: investigate the reported intermittent intro crash / main thread death in intro-with-START and Level 0 runs. Finite boot runs and an incomplete software reference cannot prove that all crash classes are eliminated.
Work is committed as **`7f4a650f1cd0ec3964ce5617a00f415e102dc234`** on branch `drakan-stab` of `tools/PS2Recomp` (worktree `tools/PS2Recomp-stab`, build dir `build/recomp_stab`), based on `drakan` @ `990f30c`. Not merged.

## 0. Summary

* **Mass run of the current `drakan` HEAD** (50 boots, 5 scenarios, 4 in parallel, `PS2X_TRACE_RING=1`): see section 2. The two known crash classes of the earlier reports
  (`docs/journal/2026-10-17/crash-listener.md` unwind-at-entry, `docs/journal/2026-10-17/crash-memcpy.md` `cvt.w.s`) are fixed in HEAD and did not show up again. No new crash signature was observed in the mass run
  (section 2). This does not identify which binary produced the reports or bound the crash rate: only 13 of these boots reached scripted quit; 37 ended before the script completed.
* **Instruction-level differential tester** (section 3): the generator enumerates 836 shapes, including invalid encodings. The saved final run models 358 shapes; 478 have no executed reference cases.
  Saved run 2 reported **81 shapes / 5,406 mismatching pairs**; the reference and emitter both changed during development. Final saved run: **136,640 state/case pairs, 0 mismatches**.
  The Drakan binary uses 155 distinct instruction shapes (991k instructions); all of them except `MFC0`/`BC0` (host state) are covered.
* **Runtime audit** (section 4): removed an asynchronous write through a borrowed `R5900Context` pointer (a lifetime/race hazard established by source inspection), initialized new contexts' `vf0`, and bounded string operations. These are not measured causes of the original crash report. The saved claim that `rand()` was fixed was incorrect; it remains unchanged.
* **Stress tooling** (section 5/6): `PS2X_FUZZ_UNWIND=N` requests a host-stack unwind at pseudorandom safe points,
  plus A/B runs for VU1 thread, async GS, HLE latency 0.
* A **crash reporter** is built into the exe (section 1): minidump + faulting module offset + live guest context + trace ring + dispatch history, also for `std::terminate`.

## 1. Tools

| tool | what | how |
|---|---|---|
| crash reporter | `ps2xRuntime/src/lib/drakan_crash.cpp`: `SetUnhandledExceptionFilter` -> `[crash]` lines in the log (exception code, faulting address, module+offset, access type, host registers, live guest thread/pc/ra/sp, all 32 GPRs, the thread-local dispatch history, `PS2X_TRACE_RING` ring), `crash_<pid>.dmp` (MiniDumpNormal) next to the exe; installed terminate handlers use `drakan_crash::fatal` (exit code 3). Other worker threads can retain their CRT default terminate handler. | `DRAKAN_CRASH_DUMP=0` skips the dump; self test `DRAKAN_CRASH_TEST=<seconds>[:abort\|:throw]` (default: null write on a helper thread). `:abort` exercises CRT abort and is not guaranteed to produce a report. |
| matrix runner | `scripts/harness/stability/stab_matrix2.sh <exe> <tag> "<scenarios>" <rounds> [VAR=val ...]`: N boots in private slot dirs (hard-linked DLLs, private ELF copy so `mc0` is per slot), one summary line per boot. `STAB_PAR` defaults to 4, `STAB_SLOT_FIRST` to 1. | verdicts by `scripts/harness/stability/stab_verdict.sh`: `OK`, `CRASH`, `DEAD` / `DEAD_EXC`, `MISSING` (target >= 0x10), `HANG`, `EXITn`, `NOEXIT`, `NOLOG`, `NOPROGRESS`, `INCOMPLETE`. Standalone verdict takes `<log> [rc] [input-log]`; input-log is required to certify script completion. |
| scenario scripts | `scripts/harness/stability/stab_genscript.py <a..e> <seed>` -> `DRAKAN_INPUT_SCRIPT` file (docs/reference/input.md section 11) | a: logo, New Game, intro plays fully, walk; b: START-skip, short gameplay; c: START-skip + 3 minutes of Level 0 with random walking / strafing / jumping / attacks / mouse look; d: START-skip + 25x pause menu + inventory open/close; e: START-skip, Save Game, Load Game (twice) |
| resume fuzzer | `PS2X_FUZZ_UNWIND=<N>`: `EeScheduler::checkpointDue` asks for an unwind at about one of N safe points (every call / back edge), chosen at random (not inside `DI` sections) | every function must continue correctly from any call or back edge; a wrong or missing resume label shows up as `[bad-resume]`, a null JALR, ... |
| differential tester | `ps2xRecomp/difftest/` (generator + runner), driver `build/recomp_stab/difftest.ps1` | section 3 |
| misc | `PS2X_FTZ=0` (EE thread without DAZ/FTZ), `PS2X_VU1_CTXCHECK=1` (count VIF1 kicks from invocation contexts), `PS2X_UNWIND_FIX=0` (old behaviour, positive control) | |

Build notes: `build/recomp_stab/{setup,setup_rt,gen,build,regen_build}.ps1`. The shared `build/ps2recomp_try/rt/_deps` (raylib build tree) is rewritten by every agent's configure and broke my build once
("source directory ...PS2Recomp-style does not exist"): `build/recomp_stab/deps` is a private copy (`-DFETCHCONTENT_SOURCE_DIR_RAYLIB=... -DFETCHCONTENT_FULLY_DISCONNECTED=ON`).
`decomp/runtime/{gs,settings,input,...}` are shared with the other agents and are compiled from the live tree: a half-edited file of another agent breaks the link of every build (happened with `runtime/gs/src/d3d11_backend.cpp`).

## 2. Mass run of the unmodified HEAD

Saved executable `build/recomp_stab/stab/drakan_base.exe`; logs `build/recomp_stab/stab/logs/base_*`, summary `base_summary.txt`.
There are 50 completed boots (10 each of a..e), rc=0, nonzero GS frames (3,854..31,134), and no recorded crash, trace-ring death, bad resume, size-clamp, or unwind-at-entry markers in the saved summary.
Only **13/50** input logs contain `script quit`. The other **37/50** are **INCOMPLETE**, rather than passes under the corrected runner.
The original baseline used shorter caps and the original scenario-a timing, so its results do not validate the later scenario generator.
Scenario intent (save/load, pause, inventory, gameplay) must be confirmed in input/state logs; sending buttons alone does not establish that each intended action occurred.

The executable predates this continuation and cannot be rebuilt byte-for-byte from a recorded source manifest. Treat it as historical evidence, not a validation of the final commit.

## 3. Differential tester

### 3.1 How it works

```
ps2_difftest_gen <outdir> [--variants K] [--seed S] [--files N] [--elf SCES_500.06]   (CMake target in the rc tree; links ps2_recomp_lib)
difftest_run [--states N] [--seed S] [--filter substr] [--show K] [--verbose]          (compiled by difftest.ps1 against ps2xRuntime/include)
powershell -File scripts/harness/stability/stab_difftest.ps1 -Variants 12 -States 32 -Show 3            (generate + compile + run; -NoGen reuses the generated cases)
```
* The generator enumerates **every instruction shape** (primary opcode, SPECIAL funct, REGIMM rt, COP0/1/2 fmt+funct(+sa), MMI funct+sa; 836 shapes), draws K random encodings per shape
  (random registers and immediates, e.g. `rd == 0`, `rs == rd`) and wraps each in a small function: branches get a delay slot (nop / marker / an instruction that overwrites the compared register),
  fall-through and target markers, JAL/JALR/JR/J variants. It then runs the **real emitter** (`CodeGenerator::generateFunction`) on that function. Shapes the emitter turns into `throw` are listed
  in `difftest_unhandled.txt` together with the number of times the game's `.text` uses them (`--elf`); the game uses none of them.
* The runner compiles the emitted C++ against the real runtime headers (`ps2_runtime_macros.h`, `R5900Context`; the `PS2Runtime` members the code references are stubbed, calls/jumps are events) and runs each
  case on randomized machine states: 64-bit registers from a mix of random, sign-extended, small and edge values (0, -1, 0x7fffffff, 0x80000000, 2^63, ...), upper 64 bits random, HI/LO/HI1/LO1, SA,
  FPRs with exponent 255 / denormals / +-0 / saturated values, VU0 registers in the normal range, a 8 KB memory window at random alignment through KSEG0/KSEG1/uncached aliases.
* The **reference interpreter** (`difftest_run.cpp`, ~600 lines) is written from the R5900 architecture (and the PCSX2 interpreter semantics where the manual is vague), not from the emitter: SPECIAL (incl. MULT/DIV
  edge cases, HI/LO, DADD*/DSUB*, 64-bit shifts, traps, MOVZ/MOVN), REGIMM, all loads/stores (LWL/LWR/SWL/SWR/LDL/LDR/SDL/SDR byte-wise, LQ/SQ with the low 4 address bits ignored), branches with delay slots
  and branch-likely nullification, link registers, COP1 (PS2 float model: no NaN/Inf, exponent 255 is a number, denormals are zero, overflow saturates to 0x7FFFFFFF; ADD/SUB/MUL/DIV/SQRT/RSQRT/ABS/NEG/MOV/
  MAX/MIN/CVT.W.S/CVT.S.W/C.F/C.EQ/C.LT/C.LE/MADD*/MSUB*, BC1T/F(L)), MMI (all of MMI0/1/2/3 except PHMADH/PHMSBH/PDIVBW, MADD/MADDU/MULT1/DIV1..., PLZCW, PMFHL/PMTHL all formats, QFSRV with SA,
  PS*H/W shifts), COP2 macro mode (QMFC2/QMTC2/CFC2/CTC2 for VI, all VADD/VSUB/VMUL/VMAX/VMINI/VMADD/VMSUB families with dest mask and bc/q/i operands, ADDA..MSUBA, OPMULA/OPMSUB, ITOF/FTOI, ABS, MOVE,
  MR32, MTIR/MFIR, VIADD..., VDIV/VSQRT/VRSQRT, LQC2/SQC2) and COP0 EI/DI.
* Compared per case: all 32 GPRs (128 bit), HI/LO/HI1/LO1, SA, FPR bits, accumulator, FCR31 (condition bit; the whole register for CTC1), VF/ACC/VI/Q/I, COP0 Status, the memory window, the final pc and the sequence of events
  (CALL target/return, TRAP, EXC integer overflow, SYSCALL, BREAK). Tolerances (counted as "tolerated" in `--verbose`): 1 ulp for DIV/SQRT/RSQRT/CVT.S.W (hardware rounds differently from the host), sign of zero
  in VU results, MAX/MIN ties with different bit patterns.

### 3.2 Findings (emitter / macro changes, checked against the software reference)

| # | shape(s) | bug | effect on Drakan | game uses (static) |
|---|---|---|---|---|
| 1 | BLEZ/BGTZ/BLTZ/BGEZ + likely + AL forms | branch condition was evaluated on the **low 32 bits** (`GPR_S32`); the R5900 compares the full 64-bit register | wrong branch whenever the register is not a sign-extended 32-bit value (64-bit arithmetic, `lwu`, `dsrl32` zero-extension idiom, `daddu` counters) | 5,000 sites |
| 2 | BLTZALL/BGEZALL | link register only written when the branch is taken (reference: always) | none expected | 0 in the saved shape scan |
| 3 | C.EQ/C.LT/C.LE, MAX.S/MIN.S | host float compare treats the PS2 values with exponent 255 (`0x7FFFFFFF`, the result of x/0 and of overflow) as NaN: C.LT false, `std::max/min` keep the first operand | any comparison / clamp against a division-by-zero or overflow result takes the wrong branch | 5,800 compares, 230 min/max |
| 4 | denormal operands (ADD/SUB/MUL/...) | host floats compute with denormals; the EE FPU treats an operand with exponent 0 as zero and flushes results below the smallest normal number | tiny garbage instead of 0 when a float register holds raw integer bits | all FPU ops |
| 5 | PROT3W | rotated all four words instead of the low three | wrong vector shuffle | 4 sites |
| 6 | PEXEH/PEXEW/PEXCH/PEXCW/PREVH/PINTH/PINTEH | wrong lane permutations; corrected exchanges, reversal within 64-bit halves, and interleaving | - | 0 |
| 7 | PABSW/PABSH | source taken from `rs` (the field is `rt`), most negative value not saturated | - | 0 |
| 8 | PADDUH/PSUBUH | not saturating | - | 0 |
| 9 | PMADDW/PMSUBW/PMULTW/PMADDUW/PMULTUW, PDIVW/PDIVUW, PMADDH/PMSUBH/PMULTH | only one 64-bit lane accumulated; products of the wrong lanes; HI1/LO1 ignored | - | 0 |
| 10 | PMFHI/PMFLO/PMTHI/PMTLO/PMFHL/PMTHL, PSLLVW/PSRLVW/PSRAVW | HI/LO treated as 64-bit (the MMI view is 128-bit: HI/HI1, LO/LO1); PSxxVW shifted four lanes instead of words 0 and 2 (sign extended to 64 bits) | - | 0 (PMFHL once in the static scan: shape `PMFHL` covered) |
| 11 | PSLLH/PSRLH/PSRAH | shift counts >= 16 not masked | - | 4 |
| 12 | MOVZ/MOVN | copied all 128 bits (the upper half of `rd` must be kept) | none in practice | 1,480 |
| 13 | LQ/SQ/LQC2/SQC2 | the low 4 address bits were not ignored | an unaligned `lq` read/wrote the wrong 16 bytes | 37k + 33k (all aligned in practice) |
| 14 | CFC1 $fcr0 | returned 0 (hardware: 0x2E00) | none | 0 |
| 15 | VU0 macro: writes to vf0 / vi0 | the register file was overwritten (vf0 must stay (0,0,0,1), vi0 0); OPMULA/OPMSUB honoured the dest field (hardware: always xyz); LQC2 into vf0 | corrupts every later use of vf0 as the constant 1.0 | few |
| 16 | VU0 VDIV/VSQRT/VRSQRT | EE-FPU saturation (0x7FFFFFFF) instead of the VU maximum (0x7F7FFFFF) | wrong Q after a division by zero | 2 |

Everything that still differs is in the "not modelled" bucket: COP0 registers other than EI/DI (Count is wall-clock driven, TLB ops are host state), `MTSA` (semantics of the upper bits unverified),
`PHMADH/PHMSBH/PDIVBW` (the reference would be a guess; the emitter now follows the EE manual layout but this is unverified, the game does not use them), `VCLIP/VRNEXT/VRGET/VRINIT/VRXOR/VLQI/VSQI/...`
(not used by the game), `CACHE`/`PREF`/`SYNC` (no-ops), `LL/SC` (not on the R5900), FPU ROUND/CEIL/FLOOR/TRUNC.W (not on the R5900; emitted but untested), `SYSCALL/BREAK/TRAP` (event only).

Known remaining semantic approximations (documented, not changed): FPU ADD/SUB/MUL/DIV round to nearest (hardware: toward zero, <= 1 ulp); MADD.S/MSUB.S round twice (hardware: once, <= 1 ulp
except under cancellation; the game has no MADD.S); VU0 macro arithmetic uses the EE-FPU saturation model (0x7FFFFFFF) instead of the VU's 0x7F7FFFFF on overflow (only the divide unit is VU-exact now);
`SQRT.S` of a negative number uses |x| (as the hardware).

## 4. Runtime findings (not instruction semantics)

| File(s) | Change | Evidence and limit |
|---|---|---|
| `ps2xRuntime/src/lib/ps2_runtime.cpp`, `include/ps2_runtime.h`, `ps2xRecomp/src/lib/vu_translator.cpp` | VU1 completion stores D/T stop bits in runtime-owned atomic storage; CFC2 merges them. Reset clears the stored bits. | Old MSCAL/MSCNT callbacks wrote into the context captured when work was queued. Invocation contexts can be popped/reallocated before worker completion. Source establishes a hazard; no captured heap-corruption dump proves this caused a reported boot crash. Diagnostics are opt-in with `PS2X_VU1_CTXCHECK=1`; absence of a diagnostic is not proof of absence of the hazard. |
| `ps2xRuntime/include/ps2_runtime.h` | Constructor initializes vf0 to (0,0,0,1). | Direct constructor check in `runtime_safety.cpp`; instruction difftest also checks constant-register writes. |
| `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` | EE executor enables DAZ/FTZ; opt-out `PS2X_FTZ=0`. Adds opt-in unwind requests. | Difftest uses the same MXCSR setting; results do not validate the opt-out path or exact hardware rounding. |
| `ps2xRuntime/src/lib/Kernel/Stubs/LibC.cpp` | Bounds for `strncpy`/`strncmp`; bounded `strncat` destination scan, append and NUL. | The saved source-only `strncat` clamp did not bound the destination. Continuation fixes that gap. Four focused tests compile the current function body against a fake 64-byte guest window with canaries. This is not a complete LibC audit; `strncpy` clips padding to available source bytes as well as destination bytes. |
| `ps2xRuntime/include/ps2_runtime_macros.h` | MMI accumulators use unsigned wraparound; horizontal sums cast before addition. | Signed host overflow was undefined in the saved helpers. Focused MMI differential and explicit overflow boundary checks exercise the revised helpers. PHMADH arithmetic wrapping is tested, but its instruction layout remains unverified. |
| `ps2xRuntime/src/lib/drakan_crash.{cpp,h}`, `src/main.cpp`, runtime CMake | Windows exception/terminate diagnostics and minidumps. | Dump is attempted before dereferencing the live guest context; reports success only when MiniDumpWriteDump succeeds. Guest snapshots remain racy and formatting/logging allocates memory, so this is best-effort reporting rather than an async-safe crash service. The exception path prints x64 host registers. |

`rand()`/`srand()` still delegate to the host CRT and mask to 15 bits. No newlib-compatible algorithm was implemented or validated here. The previous placeholder summary claiming a fix has been removed.

## 5. Race / resume stress

Historical runs against the saved fixed build (`drakan_fix2.exe` for the running experiments), reclassified by actual script completion:

| Tag | Completed boots inspected | Script-complete OK | Other results |
|---|---:|---:|---|
| `fz64` | 6 | 4 | 2 INCOMPLETE |
| `fz8` | 3 | 2 | 1 INCOMPLETE |
| `lat0` | 9 by the later continuation audit | 0 | 7 INCOMPLETE, 2 NOPROGRESS (frames=0) |
| `t0` | 2 | 2 | Historical smoke boots; not an established complete A/B study |

The saved `stab_experiments.sh` process was still running during the continuation. It and slots 1..4 were left alone. Its later results are reported by the read-only audit script, and must not be assumed to match this snapshot. No completed VU1-inline or GS-inline experiment was present at the initial audit. The stress plan is not evidence that those A/B runs passed.

The corrected runner requires nonzero frames and a scripted quit for OK, detects all missing targets (including a large target following target=0), recognizes crash markers after unterminated log lines, preserves dump names, propagates child failures, rejects reused tags, and supports reserved slot ranges (`STAB_SLOT_FIRST`). A `.stab-running` directory prevents concurrent revised runners from using one slot; legacy runners do not honor it. Prepare isolated slots before launch and remove a stale lock only after confirming its process has exited.

## 6. Validation of the fixed build

Saved full differential run (`difftest_run5.out`): 136,640 pairs, zero mismatches, 478 shapes not modelled. No full matrix was repeated.
Focused continuation checks use the existing 12-variant generated cases, freshly compiled against the audited runtime headers and runner:

| Check | Seed / states | Executed pairs | Mismatches | Local evidence under `build/recomp_stab/` |
|---|---|---:|---:|---|
| MMI | 20261017 / 16 | 19,008 | 0 | `resume_diff_mmi.log` |
| COP1 | 20261017 / 16 | 6,048 | 0 | `resume_diff_fpu.log` |
| COP2 | 170026 / 32 | 42,336 | 0 | `resume_diff_vu.log` |
| signed branches, REGIMM, LQ/SQ/LQC2/SQC2 | 170026 / 32 | 9,152 | 0 | `resume_diff_branches_memory.log` |

Total focused instruction pairs: **76,544**. Skipped shapes remain excluded; this is agreement with a software reference, not hardware conformance.
The empty-filter negative control exits 2 (`resume_diff_empty.log`). An initial attempt to filter by mnemonic BLEZ selected zero cases because shapes use numeric names; the guard rejected it and the correct OP.06/07/16/17 filters were run.

`python scripts/harness/stability/stab_check_tools.py`: 13 verdict fixtures and 15 scenario/seed generation checks pass (`resume_tools.log`).
`powershell -File scripts/harness/stability/stab_check_runtime.ps1`: focused vf0, MMI overflow and actual extracted strncat checks; evidence in `resume_runtime_check.log`.
The extraction test mocks pointer resolution and does not link the full runtime.

A one-state coverage audit (`resume_coverage.log`, seed 170026) executes 4,270 pairs with zero mismatches. Of the 155 statically used shapes, only COP0.00 (18 uses) and COP0.08 (one use) have no executed reference. This confirms the stated host-state coverage gap without repeating the large randomized matrix.

Focused fresh runtime boots (`stab/logs/`):

| Tag | Settings | Result |
|---|---|---|
| `resume_fixed_b_1` | slot 6, fuzz=8, HLE latency=0, VU1 context diagnostic enabled, cap=180 s | **OK**, rc=0, 6,569 frames, script quit at input frame 2,920; no bad-resume, size-clamp or unwind-at-entry markers. GUI transitions reach gameplay=1 and pause-screen toggles. This proves script execution, not that every movement/action had the intended visual effect. |
| `resume_control_b_1` | slot 7, unwind fix disabled, fuzz=16, latency=0, cap=120 s | **INCOMPLETE**, rc=0, 5,559 frames, no scripted quit or unwind-at-entry marker. The old crash was **not reproduced**; this is not a successful sensitivity control. |

The initial helper-thread `:throw` self-test exited 3 with no crash marker or dump (`resume_crash.stderr.log`), exposing that exit code alone was insufficient. The test thread now explicitly installs its terminate handler. Retest (`resume_crash_fixed.stderr.log`) recorded `[crash] fatal`, a live guest sample, and `slot8/crash_25720.dmp` (**1,556,373 bytes**), exit 3. Reporting of arbitrary worker-thread terminate/abort remains limited. This fresh test validates the fatal reporter; it does not validate a new SEH access-violation run. The final incremental rebuild changed only self-test setup after the successful stress boot.

Binary provenance (`resume_binaries.json`): stress/control exe SHA-256 `8c7a1732abdaf2aef3e12938fcbac148092355b6f0173941b702a9813fb365fb`; final exe/fatal-test SHA-256 `f1c20a5c7ec44e358b41184718a77af40df8028da4d5aa68500ade76823eb5bc`. Both are 66,863,104 bytes. Differential runner SHA-256 `ab62c7dc520c74a0412bb138c7522695a6624b5fdf1836587e30ec40340f272c`.

Release runtime and differential generator builds completed (`resume_final_build.log`, `resume_gen_build.log`). Runtime uses the existing private `runtime_snap` of GS/settings and private raylib source, rather than validating other agents' current shared edits. At that time the link printed ShowCursor duplicate-definition / FORCE warnings (LNK4006/LNK4088), both fixed since (strict link); the focused boot results below establish actual startup for this binary.

Reproduce evidence aggregation without rerunning boots:

```
python scripts/harness/stability/stab_audit.py --output build/recomp_stab/resume_evidence.json
```

Logs/builds/game data are ignored local artifacts. The checked-in text records measurements; retain the local artifacts if another person needs to inspect raw evidence.

## 7. Merge notes

1. Tool worktree commit: `7f4a650` on `drakan-stab` (parent `990f30c`), 24 files. No sibling worktree or shared runtime file is included. The recompiler and runtime changes belong together: regenerated code uses new helpers and `vu1VpuStatBits()`.
2. Commit only `scripts/stab*` and this document in the outer `decomp` repository. Its ignored nested worktree is not a submodule; an outer commit does not carry the tool changes. Preserve the separate tool commit/branch.
3. Integrator: first integrate the tool commit into the intended `drakan` tool branch using a clean checkout; then bring in the outer scripts/document commit. No merge was performed here. Review concurrent edits in `ps2_runtime.cpp`, scheduler, LibC and main at integration time.
4. Rebuild the recompiler, regenerate the game translation, and rebuild the runtime as one sequence in a private build directory. Header-only rebuilds cannot update already-generated branch/link/load/store code. The local existing `build/recomp_stab/{setup,setup_snap,gen,build}.ps1` and config are ignored machine-specific helpers, not committed build products. The tracked differential driver records its tool/worktree paths and expects Visual Studio 2022 Community.
5. After integrating the separately owned GS/settings changes, perform a focused smoke/START-skip and runtime check against that integrated build. Private-snapshot validation does not establish compatibility with an in-progress shared renderer. Keep dist unchanged until the user authorizes its update.

Owned changes: `ps2xRecomp/CMakeLists.txt`, nine translator/emitter sources including `code_generator.cpp`, `ps2xRecomp/difftest/*`; runtime CMake, context/macros/scheduler headers, scheduler/LibC/runtime/main sources, and `drakan_crash.{cpp,h}`. Outer ownership is `scripts/stab*` and `docs/reference/stability.md` only. Exact committed paths are available with `git show --stat` in each repository.
