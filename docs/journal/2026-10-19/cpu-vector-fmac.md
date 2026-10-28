# Vector FMAC compiler analysis — 2026-10-19

This continues the raw-FPS CPU investigation from the installed `cd25328dbe3e3ba8d962636216667d5990636731f7ffc9c76404aa1efca3e1cb` executable. The 144 FPS target remains open. Evidence is under `build/vu-noalias-20261019`.

## Critical path

The installed build's sampled vector thread spends 76.3% of its samples in VU functions, with another 5.0% in PS2Memory. The main game thread spends much of its time waiting. The GS worker is also active, so a VU improvement may eventually expose GS throughput as the next limit.

These are sampled program-counter/wall-time shares, not retired-instruction or hardware-cycle measurements. The sample count for each thread includes parked threads. See `build/vu-normalize-20261019/final-all-threads.txt` and its matching map/CSV.

## Candidate

The private `Vu1Step::fmacSlow` declaration gains an MSVC `__declspec(noalias)` annotation, with an empty macro on other compilers. The function reads its direct array arguments and immutable constants, writes the result and flag arrays, and has no access to interpreter or mutable global state. Parameters are still permitted to overlap; no `restrict` qualifier is added. See [Microsoft's contract](https://learn.microsoft.com/en-us/cpp/cpp/noalias?view=msvc-170).

This lets the compiler retain knowledge about interpreter state across the rare fallback call. No arithmetic, normalization, guest scheduling, public interpreter layout, graphics setting or frame-rate mode changes.

An isolated compilation of generated block 47 changes from 3,839 to 3,791 instructions and from 17,336 to 16,502 bytes. Native-kernel code changes very little. These code-generation numbers are preliminary evidence, not an FPS result. A complete vector rebuild and controlled comparison determine whether the candidate is retained.

## Separate flag-batching prototype

The fixed image has two FMAND observers (pairs 450 and 1336), eight FCGET observers, and no internal status-flag reader. A separate prototype accumulates ordinary-result negative lane masks in existing flag batches and reduces them only at publication. It passes 270,532,608 comparisons against the original batch behavior, covering mixed writers, every sign-mask/status/sticky combination, and every initial 12-bit status.

However, its UV kernel increases from 660 to 669 instructions in the code-generation probe. It remains private in `build/vu-sign-batch-20261019` and is excluded from the FMAC candidate. Global dead-flag elimination would need to preserve the two MAC observers, external state, suspended-run pipelines and exact sticky flags; absence of an internal status reader alone does not authorize dropping the state.

## Validation and disposition

The full Release vector build completed, including all four generated image slices. The exact-state checks pass: 131,072 frozen-reference arithmetic cases across four caller rounding modes, 114,688 packet cases, 114,688 UV cases and 8,192 timed-scalar cases. Four clean ABBA runs measured **100.291 FPS installed versus 99.909 FPS candidate (-0.38%)**. All saves remain unchanged and every run has no audited competing game/compiler process. The annotation is rejected and remains private. No executable, settings, save or canonical runtime-source/patch deployment change was made. See `docs/journal/2026-10-19/data/vector-fmac-20261019.json`.

The sign-batch prototype also increases generated block 47 from 3,791 instructions / 16,502 bytes (with the annotation) to 3,930 instructions / 17,424 bytes. It is excluded.

Windows reports six available hardware counter slots, but starting the named `DrakanCpuReview` trace returns `0x80070005` (Access is denied). No trace was started or left recording; no privilege or system configuration change was attempted. The ordinary sampled-PC profile remains available.
