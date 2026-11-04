# Actual imported Long: third-person input acceptance

This follows converter commits 2792701 (referenced projectiles) and 2b9ae8a
(rigid Bow template selection). It uses the same private native-created actual
original Long/Arrows fixture as run 76, not a native Bow renamed in a save.
No runtime cadence adapter or projectile-stat override is enabled.

`build/harness/bow-source-pair77-third` is a bounded third-person control. It
launches on the standing press and a later forward press; the delayed Space
request happens after walking off the small spawn platform and its fire press
lands after touchdown. It is not a commanded jump-shot acceptance run.

`build/harness/bow-source-pair78-third-jump` presses Space while grounded at
audit 1486, enters real airborne flags 10 at 1487, receives a fresh mouse press
at 1494 and launches at 1495 with flags 90. RAM Y rises from 160978.8125 at the
press sample to 161078.84375 at the hold sample. Native jump type is 1, a
standing jump with forward input. This proves commanded jumping fire but not a
native running jump. The earlier run 76 proves running airborne fire, whose
initial airborne state could also arise from stepping off the nearby platform.

## Running jump, real launch and release

`build/harness/bow-source-pair79-running-jump` uses the broad Surdana ground
region already exercised by the earlier native Bow fixtures. Its private map
is `build/ootf/bow-source-pair/broad-floor.ini`; recipe is `broad-third.txt`.
No campaign save or canonical pack is modified.

The standing mouse press launches at frame 1294, reducing external quiver count
10 to 9. The mouse remains held for two seconds; no second launch occurs.
Releasing it leaves count 9. Forward movement then precedes a fresh Space press
at frame 1522 while grounded (flags94 0). A fresh mouse press launches at frame
1530 while airborne and attacking (flags94 90). Native RAM jump type is 2,
with forward speed 2775.75 at the shot sample and 2955.97 at the hold sample.
Y rises from 171307.125 before the jump to 171795.28125 at shot and
172227.546875 during hold. External quiver count becomes 8 and remains 8
through holding, release, landing and stop.

A later sequence of four presses spaced 300 ms apart accepts one native shot
at frame 1697, reducing count to 7. Later presses occur while the existing
attack is active and do not cause additional launches in this sample. Stopped
position [444823.8125, 171335.28125, 343289.90625] is identical in stopped and
rapid-end RAM samples. Logs identify actual Long resource 343e2, rate 1.75;
`cadence_metrics.json` and `jump_metrics.json` preserve the measurements. The
run cleanly quits. This characterizes current input acceptance, not original
reattack parity or universal rejection of queued presses.

## Original timing comparison boundary

A new launch of the locally extracted original `cab_disc/Drakan.exe` through
the Windows computer-use API timed out; refreshing the window list exposes no
original game window. No running user game was closed, and no system registry,
compatibility or security setting was changed. Thus no original runtime timing
comparison is claimed.

Read-only original RFL exports continue under
`build/ootf/rfl-cadence-followup`. The reattack path reads Rynn property 165
(Re-attack Window), and original Bow rate is used as Archer playback speed;
the original state machine also handles phase/hold transitions. These facts
do not justify dividing the native reload deadline by the source rate. Exact
original launch-event/press-queue timing, special aim metadata and source
sound/effect fidelity remain open. Held Long visual representation still needs
a separate model-only check: a shooting pose alone does not prove a drawn bow.
