# Native timed-parry investigation — 18 October 2026

**No production MKB behavior change is justified. Successful timed damage
reduction remains unverified.** Holding RMB accepted one native dagger parry;
its short window ended before the enemy attacked. The later tap run was stopped
for the GPU handoff and is explicitly incomplete. These observations do not
establish continuous protection, full weapon coverage or subjective OotF parity.

## Native conditions and observed timing

Local generated code and read-only native decompilation establish:

- `0x3016e8` selects the equipped weapon/context branch. The tested existing
  save has ED0 resource `0x1000e` (dagger), EAC empty and no shield `0x10090`.
  The weapon branch selects action `b28=0x41`, sets AB0 bit `0x04000000`, and
  **consumes request flag90 bit 0x20**. Holding the physical button does not
  itself create another request after consumption. No host autorepeat was added.
- Native state/animation gates still apply. The request can defer under flags94
  mask `0x210`, guarded state `flags94 & 0xc00 == 0xc00`, or AB0 `0x800000`.
  Other equipment/resource branches exist and are not weapon-wide passes.
- Update `0x303ff8` uses the current animation record's **+0x58 time**, not a
  fixed count of host input frames. Standing timing properties `0x155/0x156`
  supply start/duration; the alternate state uses `0x157/0x158`. In both saved
  RAM baselines these values are **0.0 / 0.8**. The window includes its start
  and excludes its end. The observed weapon animation speed is **1.5**.
- The separate shield/hold branch uses action `0x40` and AB0 `0x80`; do not
  interpret it as the weapon's timed action. This branch was source inspected,
  not validated live with a shield.
- Reducer `0x314578` requires AB0 `0x4000`, a supported attacker, and facing
  dot **strictly greater than 0.4**. Type-10 damage objects resolve their owner
  through data+0x60; this is the actual enemy used for facing. Native attacker
  types 7/9 have further contextual gates, and resource `0x1003b` is excluded.
- With these gates met, the native reducer uses Rynn property `0x159` (observed
  **100%**) or shield `0x3faac8`, and emits event `0x10d3`. Damage handler
  `0x314ab0` subtracts the returned amount via HP setter `0x3d5710`.

The observed held run presses RMB at native frame **557**. The accepted action
has time 0/speed 1.5; complete update samples show the reduction bit at
**558–571**, animation times **0.0585–0.7665**. It is clear at **572**, time
0.8235. The first incoming hit is **640**. RMB remains held until **821** and
the native request bit is already consumed by frame 571. Thus the failure of
that long hold has a demonstrated native timing explanation. Fourteen sampled
frames in this run are not a universal frame-count or latency guarantee.

## Bounded existing-save runs

Private observer executable SHA-256:
`5B3333E2A13FFE38E177163F3FEC9EDB3269CF31641958EA38612E3C2647CE68`.
It was linked from private snapshots of existing integration libraries plus the
instrumented glue. It is **not deployed 54F082 or main's final sky candidate**.
Private provenance records every copied link input and the exact observer
source hashes. No existing build objects/libraries, deployed executable,
graphics source, public runtime headers, public patch or dist were changed here.

| Run | Completion | Reducer calls | Ready/end RAM HP | Verdict |
| --- | --- | --- | --- | --- |
| `parry01-neutral` | Exit 0, script quit f839, 46.346 s | Five, all 10→10 | 100→60 | Incoming damage; no parry request |
| `parry01-held` | Exit 0, script quit f841, 44.210 s | Five, all 10→10 | 100→50 | Accepted short native window, no mitigated hit |
| `parry01-tap` | Stopped for GPU handoff, no script quit | Zero | No completed comparison | **Incomplete; no pass** |

Neutral's fifth hit is after its end RAM snapshot: do not compare the table's
snapshot HP as a controlled difference in protection. Durations include boot
and diagnostics and are completion receipts, not performance comparisons.
Both completed runs end with released input. Original, source-fixture and
private card hashes remain unchanged in **all three** runs, including the
interrupted one. Original starting-save SHA-256 remains
`b1fec620193ff5c04bbd00b4328ea9d69b1b3683a28a91b7a0ec40d0d7eeba95`.

Detailed evidence stays ignored/local under
`build/parry_20261018/observer-build02/t/`, with final per-run
`parry-report-final.json`. `build/parry_20261018/final-receipt.json` records
evidence hashes. Native generated/decompiled code, ELF, RAM and card payloads
remain local. [Sanitized aggregate](data/combat-parry-20261018.json).

## Diagnostic source and CPU validation

Nested runtime commit **35443440fec23c6fab551e8978ee7929777d2be5** changes
only `drakan_input_glue.cpp` and private `drakan_parry_audit.h`. It is an
**opt-in observer**, enabled only by `DRAKAN_PARRY_AUDIT=1`. It calls the original
native functions, reads memory, and caps output at 10,000 records. It does not
modify guest state, input semantics, native timing or damage. Main owns patch
export and the exact final build. Runtime sources are frozen after this commit.

The live build above used observer **version 1**. Native damage handling can
yield and resume through aliases, so its enclosing C++ return is often
incomplete even though HP has changed. The report rejects these incomplete
parent records; it does not turn a reducer return alone into verified HP loss.

The committed **version 2** correlates completed reducer calls with the native
leaf HP setter at return site `0x314ed0`. It requires the same EE thread,
guest stack, Rynn, native s1 damage, s5 damage-pointer value, stats object and
requested new HP. The report additionally checks actual before/after HP and
rejects unrelated/missing/truncated/mismatched records. This version has
**CPU validation only; no live damage/parry pass is claimed for it**.

Validation: actual integrating-glue syntax check and **34 C++ CPU checks** pass,
including native property/animation decoding against older and 54F082 private
RAM, unchanged memory, and HP-setter correlation rejection cases. **Eight
Python test methods** pass, covering incomplete returns, HP mismatches,
cross-call attribution, distinct shield/weapon verdicts, trace limits and
bounded/released recipes. Logs: `build/parry_20261018/cpu-final.log` and
`build/parry_20261018/report-tests-final.log`.

## Next bounded check, after another explicit GPU handoff

`combat_parry_recipe.py` generates neutral, single held, or four-frame taps
24 frames apart. The tap phase covers different enemy attack times while
leaving releases between requests; it changes only the test script. Every
recipe releases RMB by relative frame 310 and quits at 320, with no LMB,
movement, save edit, health edit or damage injection. The interrupted tap
sequence remains a hypothesis until a completed run demonstrates mitigation.

`combat_parry_run.ps1` defaults to **one tap case**, creates fresh private
cards from the existing hostile675 fixture, records opt-in configuration and
hashes, and enforces a deadline. For an observer-enabled private binary:

```powershell
& scripts/harness/combat_parry_run.ps1 -BinDir <private-bin> -Name NEW_NAME -Cases tap
python scripts/combat_parry_report.py <completed-tap-run> --require-parry
```

A same-executable neutral control can additionally be supplied with
`--control <completed-neutral-run>`. It requires matching attacker identity
and source fixture. Do not reuse the old observer's control as if it belonged
to a different final executable. No further game runs are queued here.

## Later combined-build validation

The final executable now has completed version2 observer runs, including facing and tap-phase comparisons. See [the final follow-up](followup-depth.md#final-combat-follow-up). These validate native HP correlation but still do not prove a successful timed parry.
