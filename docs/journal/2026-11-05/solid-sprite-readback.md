# Removing a recurring scratch-sprite readback stall

## Cause and scope

A bounded GPU draw/reader trace (`build/perf-pacing-fix/bench-123422.json`) found a 4x4 sprite followed by a
32x32 CT32 CPU read on the same target. The texture request is a depth format unsupported by the existing
color texture resolver, so the current shader actually uses uniform vertex color. Equal blend A/B terms and
D=source also make the result independent of the destination. This fix does not add depth-texture support;
if that texture becomes supported in the future the effective `haveTexture` check automatically rejects the mirror.
The Catacombs trace (`bench-125136.json`) showed five sprites submitted together, explaining why the initial
single-sprite optimization helped the Isles but not Catacombs. No map, target address or guest PC is hard-coded.

## Exact CPU mirror

The normal GPU draw still runs. `gs_solid_mirror.h` proves the coverage and RGBA of every rectangle in bounded
batches (up to 64 sprites, 4096 total pixels). The D3D11 path requires CT32, full writes, integer geometry,
uniform integral RGBA, no dependent texture/test/blend, and a clean small target without a dirty overlapping
owner, pending upload, temporal transform or jitter. Scissor, FBA and overlapping rectangles are handled in
submission order. Only proven pixels are written in local memory; their block and target-row stamps are advanced
together so subsequent reads and texture caches see the current draw. All uncertain cases retain normal readback.
A final audit excluded allocations crossing the end of VRAM and potentially wrapping dirty aliases, because
normal target upload does not initialize wrapped rows. No previous-frame result is reused.

`GSR_RB_SOLID=0` disables this path for same-executable comparisons. `GSR_RB_VERIFY=1` checks CPU results
against a real GPU read. `GSR_RB_DRAW_TRACE` logs up to 64 small-target draw and reader events of each type;
leave diagnostics off for ordinary play. An earlier speculative early-Flush experiment was discarded.

## Matched timing runs

Private frozen graphics/native settings: 4x, DLSS Quality, ray effects, audio, uncapped world and display,
vsync off. Only the mirror switch changed within each pair; no second game process was sampled. Measurements
are between the scripted still captures, excluding warmup and end margins. Actual rates vary; compare within
each pair. Receipts below are in `build/perf-pacing-fix/`.

| Executable and order | Map | FPS off / on | p99 ms off / on | GPU Map wait ms/present off / on |
| --- | --- | ---: | ---: | ---: |
| D80A2BEB, off first | Catacombs | 55.59 / 77.94 | 27.84 / 16.52 | 6.737 / no sampled Map |
| D80A2BEB, off first | Andrellian Isles | 46.08 / 66.92 | 38.16 / 23.34 | 7.862 / 0.0074 |
| 6F8ACB44, on first | Catacombs | 58.49 / 100.32 | 27.72 / 13.06 | 6.409 / no sampled Map |
| 6F8ACB44, on first | Andrellian Isles | 54.53 / 104.23 | 32.07 / 16.52 | 8.109 / no sampled Map |

Receipts: `bench-125613.json` (batch-capable build) and `bench-133817.json` (final VRAM-boundary guard).
The final disabled runs had one Map per present. Both configurations advanced the engine clock close to real
time; all final ratios were between 0.996 and 1.000. Isles still showed a 135 ms maximum gap in the first pair;
this fixes a specific recurring wait, not every hitch. The earlier exploratory single-sprite runs in
`bench-124407.json` / `bench-124648.json` are not the final benchmark. Two Catacombs cases in the former
never launched (missing scratch recipe); they were excluded.

## GPU correctness and map coverage

The synthetic GPU oracle verifies native and 4x output including alpha, scissor, FBA, overlapping sprite batches,
untouched pixels, and a real forced Map after a GPU-only predecessor. Pure checks reject invalid geometry,
texture, masks, nonuniform color and state dependencies. All seven regression suites passed on the batch path
(world readback, partial upload, image upload, RT readback, RT sampling pixels, full synthetic and pacing bridge;
`solid-regress-tests.log`). The final focused suite added the wrapping-allocation regression: **95 checks, 0 failures**.

All 13 playable fixtures exited successfully on unchanged D80A2BEB (`bench-130932.json`). The last cumulative
verifier summaries total **136,200 checks, 937,511,872 pixel comparisons and 0 mismatches**. This includes
existing readback-skip checks as well as the sprite mirror; summaries print every 200 calls. Verification
forces GPU reads and is not an FPS measurement. The subsequent boundary guard only narrows eligibility.

| Map | Compared pixels | Mismatches |
| --- | ---: | ---: |
| surdana | 75,915,712 | 0 |
| kragmor | 68,400,320 | 0 |
| catacombs | 146,666,432 | 0 |
| city | 30,071,744 | 0 |
| inquisitor | 84,425,472 | 0 |
| shadowmire | 43,258,944 | 0 |
| isles | 75,965,824 | 0 |
| crystal | 52,175,040 | 0 |
| ravenshold | 73,728,512 | 0 |
| tundra | 77,231,616 | 0 |
| shiverbane | 61,691,456 | 0 |
| valley | 81,310,848 | 0 |
| stratos | 66,669,952 | 0 |

End captures reviewed (`maps-all.jpg`); Valley was in its scripted cutscene, and the Tundra fixture retains its
previously observed missing/stretched avatar. This is coverage of map fixtures, not an entire campaign replay.
The missing Transition to Desert asset is excluded. The final deployed executable SHA-256 is
`6f8acb44b2c5c176adcbe74428de2b6ce7e700c519d7fe9f40698b4fd35c058a`.

## Combat and caps

Isles (`bench-133451.json`, image sheet `bench-133451.jpg`): the same two three-second primary-fire holds under
enemy fire completed at every cap. Private fixture health aids keep the dragon alive. RAM dumps perturb maximum
gaps, so use the still-view pairs above for throughput. Game speed remained close to real time.

| Cap | Observed FPS | p99 ms | Engine time / wall time |
| --- | ---: | ---: | ---: |
| Uncapped | 95.47 | 18.81 | 0.9962 |
| 30 | 29.77 | 33.56 | 0.9961 |
| 60 | 58.98 | 18.72 | 0.9947 |

Native hostile encounter (`hostile-caps-134226.json`): private Surdana card relocated with `combat_save_fixture.py`;
885 other saved resources unchanged. Nearby NPC slot 84 went from 40 to 33 HP after the first attack in every run;
after the third attack it had 19 / 21 / 21 HP at 0 / 30 / 60. Rynn took damage and survived (final HP 34 / 54 / 44).
Source cards remained unchanged. This establishes damage during combat, not identical AI or damage per second.
The early `ram_begin` dump was missing and `ram_target` showed no explicit lock; the composite lock/block check
did not pass. The damage readings above compare `ram_target` before the first attack to `ram_hit1` and `ram_hit3`.

First directional run (`native-caps-132705.json`): 30 passed 59/59; uncapped passed all attack checks but failed
the final movement-settle sample (58/59). Elevation fell from 160481 to 160441 between the two idle samples.
The 60 FPS attempt returned to the title without entering gameplay and supplied no combat samples. An input-sample
count gives different observation times across FPS, so the follow-up uses identical wall-clock action and sample
times (original recipe times multiplied by 1000/30 in milliseconds) and the same native assertions.

Final wall-clock directional runs, all on 6F8ACB44 and native private copies of the same card:

| Display cap | Native assertions passed | Median shown-world present interval during checks (ms) | Run directory under `build/harness/` |
| --- | ---: | ---: | --- |
| Uncapped | 59/59 | 5.69 | `perf-native-wall-caps-134452-0` |
| 30 | 59/59 | 33.33 | `perf-native-wall-caps-134452-30` |
| 60 | 59/59 | 16.67 | `perf-native-wall-d2-60-final` |

The second 60 FPS attempt again returned to the title before combat. Explicit `DRAKAN_GAME=d2`,
`DRAKAN_LANG=english` in the final 60 FPS run loaded the save successfully, allowing the same wall-clock recipe
to finish. This distinguishes completed gameplay from a failed launch; it does not establish the cause of those
two menu returns. Each passing run checked neutral and four directional arrow/WASD attack clips, advancement,
release, stable idle and ordinary movement afterward. All 177 checks passed, and original cards were unchanged.
