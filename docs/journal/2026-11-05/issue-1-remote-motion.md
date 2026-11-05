# Issue 1: remote player lag and apparent teleportation

## Findings

The follow-up reports other players appearing delayed and jumping to positions or spawn points. Review started from
`f67c0b4` and [the previous position investigation](../2026-11-04/coop-position-inventory.md). That change already
corrects two demonstrated defects: receiver-callback-dependent playback could outrun the sender, and the native
inventory preview published its temporary position and pi-radian facing change. Current code uses elapsed time,
estimates sender cadence, bounds playback by the newest received tick, and disables network extrapolation. The
inventory filter retains the last gameplay transform through the first close boundary.

The prior two-instance acceptance establishes inventory isolation and settled remote positions. It explicitly does
not measure simultaneous movement synchronization or certify WAN/loss or mounted-dragon behavior. Those limits
remain; the follow-up alone does not establish a new defect in the corrected implementation.

[Arena avatar deletion](../2026-11-04/arena-avatar-respawn-loop.md) is another relevant existing correction: campaign
cleanup used to delete NPC-class cosmetic bodies repeatedly. Current cleanup protects registered local and remote
body handles before classifying campaign actors. Its focused live acceptance establishes local Monk body stability
across cleanup passes, not remote motion quality under network load. Actual network Respawn messages and gameplay
level transitions can still legitimately change positions.

## Additional regression

`world_pose_test` now exercises playback together with a network-configured PoseBuffer while the sender changes from
240 Hz gameplay to 30 Hz, loses a 1.5-second burst of snapshots, returns to 240 Hz, and stops advancing while the
server continues publishing snapshots. It checks monotonic playback, no position beyond the latest received state
even with a deliberately large wire velocity, and final convergence. Existing tests already cover fixed sender /
receiver cadence combinations, inventory entry/exit and long receiver stalls. No production code was changed.

The parent integration task owns compilation and execution of this new regression. No additional live game or shared
build was launched for this review. An unresolved report on the rebuilt executable needs paired sender/receiver
timestamps and RAM poses, packet-gap/rejection logs, assigned-spawn events, and ghost deletion/recreation logs to
distinguish stale network state, intentional respawn, and actor lifetime faults. Adding generic smoothing without
that evidence would hide the distinction. Ordinary interpolation still adds approximately 100 ms presentation delay.


## Maintained paired-motion capture prepared

`scripts/harness/remote_motion_check.py` launches **directly from `dist/DrakanPC`** with private copied memory
cards, settings, recipes and evidence below `build/harness/<name>`. It does not copy binaries or deployed assets.
The moving host walks, stops, strafes, opens/closes native inventory, moves again and settles. The client watches;
both collect filtered outgoing poses, replayed poses, periodic actual native ghost-body positions, lifecycle /
respawn / rejection messages and stage RAM dumps. The executable SHA-256 and relay settings are retained.

Each log reader records a common Python monotonic receipt clock. Process-relative trace epochs are estimated using
the minimum receipt-minus-trace offset. These offsets retain pipe/thread scheduling uncertainty; this is a diagnostic
movement-delay fit, not a synchronized-clock latency benchmark. `summary.json` retains all candidate steps over
500 game units without discarding them as presumed respawns. Compare them against sender history, explicit Respawn
and body removal/recreation events before assigning a cause. Native body logs have coarse `%g` precision and are
periodic; stage RAM dumps retain full transforms for handle inspection. Script success only checks capture completeness
and clean exits, and must not be described as a motion-quality pass.

The optional seeded UDP relay controls one-way latency, bounded jitter and whole-datagram loss (including reliable
traffic). Unlike the existing in-process LoopbackTransport tests, it can exercise deployed native UDP instances.
A no-impairment relay baseline is needed before comparing adverse conditions. Runnable commands after the parent's
deploy and exclusive native slot grant:

```powershell
python scripts/harness/remote_motion_check.py --name remote-motion-baseline
python scripts/harness/remote_motion_check.py --name remote-motion-adverse --delay-ms 40 --jitter-ms 15 --loss .05
```

Preparation checks passed: ruff lint/format; `--help`; synthetic paired 100 ms source/replay histories recover a
100 ms fitted delay; a local UDP server/client confirms both relay directions and at least 18 ms delivery for a
configured 20 ms delay. These are tooling checks, **not deployed game acceptance**. No game was launched, shared
runtime source edited, or new production defect claimed during this preparation. Live paired/native acceptance remains
outstanding until the integration owner grants the native test slot.


### Pre-live isolation review correction

Root review caught that the initial helper inherited unrelated DRAKAN/PS2X/GSR flags and did not isolate the main,
input or multiplayer INIs. The maintained driver now builds a sanitized environment, copies private main/input/native
settings, writes a private deterministic multiplayer profile through `DRAKAN_MULTIPLAYER_INI`, and explicitly selects
D2 English, Surdana and the requested character. Its default source card is the existing
`build/coop2-fixture-si/mc0`, replacing the absent historical gw-net card. It records the effective game overrides.

The verdict now requires native `[in] script quit`, no guest missing-target marker, settled RAM on both peers,
unchanged pre/post executable SHA and unchanged deployed INIs, in addition to trace completeness and clean exits.
An owned process still running at the driver deadline receives a full process/RAM dump via
`sp_mp_transition.capture_process` before termination. A failed dump leaves the process for diagnostics. Rechecked
ruff and CLI loading; no native process launched during this correction.

## Final native baseline and impaired UDP capture

Both `build/harness/issue1-remote-motion-baseline2` and
`build/harness/issue1-remote-motion-adverse` used final deployed SHA-256
`fce25c698b06cc77a233aea741c3f46c318a3c06fa40b565df9813002ed02503`.
All capture-completeness checks passed: four native processes completed scripted
quit and exited 0, both peers retained their stage RAM, no missing guest target
was logged, executable hashes matched, deployed INIs were unchanged and source
fixture card hashes matched before/after the pair. No game processes remained
when the GPU slot was returned. No production networking change was made.

The adverse relay used seed 20261105, 40 ms one-way delay, bounded +/-15 ms jitter
and 5% requested whole-datagram loss (including reliable traffic). It received
5151 datagrams, dropped 253 (4.91%) and forwarded 4898. Its 206 closed-peer UDP
reset notifications were handled during teardown; both games still quit natively.
The baseline forwarded all 4993 received datagrams.

### Measured movement, lifecycle and inventory

`motion-review.json` in each run retains stage clock estimates, lifecycle events
and the following analysis. The stable window begins after **both** near captures
and ends before either peer closes. Delay fits use the moving sender's intervals
above 100 horizontal units/s, rather than diluting error with idle samples.

| Measure | Baseline | Adverse |
| --- | --- | --- |
| Stable interval | 32.67 s | 31.40 s |
| Moving host replay samples | 2612 | 2620 |
| Steps over 500 units in stable interval | 0 | 0 |
| Largest replay step / interval | 239.0 units / 73.9 ms | 221.8 units / 74.2 ms |
| Moving-only fitted presentation delay | 190 ms | 230 ms |
| Fit mean horizontal error / samples | 14.77 units / 520 | 11.16 units / 515 |

The roughly 40 ms fit increase is consistent with the configured one-way relay
addition. Receipt-derived process epochs include pipe/thread scheduling error;
the lag grid is 10 ms and the sample histories are discrete. These numbers are
not synchronized-clock latency measurements or a generic motion-quality pass.
The stationary client's return direction has insufficient movement for a useful
moving-only latency fit. Both directions had zero stable steps over 500 units.

The baseline's **660.8-unit** candidate spans **7.1157 seconds during startup**,
not the inventory interval. Receiver B explicitly removed player 1 for another
level at native log time 42.652, queued destruction of body `dfb280`, then spawned
new ghost id 2/body `dfd680` at 49.191. Sender A returned through title/load during
that setup. Both near captures and steady movement followed this recreation.
Consequently the two non-adjacent poses cannot be classified as one instantaneous
in-game warp. The adverse run had no candidate over 500 units at all. No body was
removed/recreated within either stable movement window; removals at peer closure
are separately visible in lifecycle logs.

Native inventory was open for approximately 6.79 / 6.73 seconds. Excluding a
0.5-second margin at each boundary, outgoing filtered sender positions and replayed
receiver positions were **exactly constant**: baseline 435 / 454 samples, adverse
527 / 487 samples. Actual native ghost transforms, skeleton clip/time and mesh
slots were also read from RAM with the established world/scene layouts. This
supports inventory-preview isolation in these runs.

Recipes begin independently after native readiness, so equal stage names do not
imply simultaneous peer captures. In baseline2, the startup reload shifted B's
`ram_partner_inventory` before A's real inventory interval. The inventory verdict
above therefore uses actual log-aligned entry/exit times and paired traces, not
that label. The full captures remain available for narrower timing review.

These bounded native UDP checks cover default Rynn, Surdana, walking/strafing,
stopping and inventory entry/exit, with the listed impairment. They do not cover
WAN clock synchronization, mounted motion, all roster characters, every frame
rate, bursts longer than this loss pattern or general campaign transitions.
There is no measured new steady-motion defect here justifying another smoothing
or extrapolation change. Near mesh/appearance results are in
[the LOD journal](issue-1-remote-lod.md).
