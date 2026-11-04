# Bow cadence and airborne evidence

User scope correction: aiming and shooting are the problem under investigation.
The temporary damage, gravity, acceleration and lifetime adapters were removed;
they are not part of the retained implementation. No projectile-stat change is
proposed here.

## Measured port behavior

Private run `build/harness/bow-cadence17`, recipe
`build/ootf/bow-cadence17.txt`, used the actually equipped native bow save in
`build/ootf/bow-openfield/mc0`. The run quit normally at frame 2004. Its executable
preceded the source stat reversion, so this is input evidence, not acceptance of
the current deployed executable or projectile statistics.

- A nine-second uninterrupted attack hold consumed one arrow (200 to 199).
  Releasing the button consumed no additional arrow.
- Eight 100 ms attack presses spaced 750 ms apart consumed seven arrows. The
  fourth press was refused. This is an observed acceptance sequence, not a
  measured exact cooldown or an original-game cadence comparison.
- A jump followed 300 ms later by an attack press consumed no arrow. RAM samples
  at 100, 500 and 1000 ms after the jump had airborne flag `flags94=0x10` and
  elevated player Y. After landing, a fresh attack press consumed one arrow.
  This establishes the current port's airborne refusal in this fixture.

Earlier `bow-ootf-input05` and `bow-ootf-controls08` prove press firing before
release, persistent alternate-attack sight, sight FOV 40, and a strafe shot.
They do not establish complete run/jump compatibility. The open-field run above
supersedes the ambiguous merchant-ledge jump attempt in controls08.

## Original-game evidence obtained

The original publisher manual, printed page 15, describes attack press firing
with third-person autoaim and alternate attack toggling the accurate zoomed
sight. The private scan is `build/ootf/manual17.png`. Printed page 14 describes
running/jumping attacks in the melee section; that wording alone does not prove
bow shooting while airborne.

The installed original PC `Dragon.rfl` is imported into the private Ghidra
project `build/ootf/rfl-project/Original`. Private analysis output remains under
`build/ootf/rfl-analysis`; no original executable content or translated function
body belongs in the repository.

The original attack-animation setup at `0x1005d990` dispatches an equipped bow
branch (weapon mode 6) to helper `0x1005e9d0`. The helper reads the equipped bow's
class property 17, identified independently from the original class schema as
Rate of Fire. It writes that value through a caller-supplied float output. The
caller has animation setup parameters rather than a simple seconds-per-shot
timer. This supports tracing animation-rate semantics before changing cadence;
it does not yet establish the full timing equation. In particular, the original
Long Bow property value 1.75 must not be interpreted as a 1.75-second cooldown.

The original `0x1005ce40` includes fall-damage/landing handling and a pending
sight-attack retry. It is not sufficient evidence for bow airborne eligibility.
Likewise, the original animation state-name table contains “Jump Attack”, but
the name is shared character machinery and does not identify bow support.

## Remaining comparison

Trace the original player input/state dispatcher through the bow attack branch,
identify its movement/airborne refusal conditions, and connect Rate of Fire to
the animation playback argument. A new port input adapter should follow that
evidence. No jump-gate bypass or guessed cooldown has been added. The original
retail executable has not yet produced a usable local comparison session.

The next live acceptance run must use the coordinated canonical rebuild that
removes the historical stat adapters from the executable, with the retained
owner guard, press firing, sight toggle and autoaim work intact.

## Rebuilt executable and original jump gate

`build/harness/bow-movement18` used the coordinated rebuild with executable
SHA256 `7ECB43CECF083BF2D9C96DF8AA7BC212513D9D47416B107EAA6C60242D7645C5`.
The run quit normally at frame 1404. Its `ram_report.json` and raw RAM establish:

- Forward movement from X 450247 to 448333 with a shot consuming 200 to 199.
- The released alternate-attack sight remained open (`cam+48=0x220`, FOV 40).
- A sight shot while strafing consumed 199 to 198, with Z moving 343259 to 344988.
- Airborne attack at Y 171963 with `flags94=0x10` consumed no arrow; a fresh
  post-landing attack consumed 198 to 197. Three launch entries match those shots.

Further original RFL analysis found the actual attack entry `0x1006f9d0`, which
was not initially recognized as a function. Its refusal mask is `0x960`, or
`0x160` in an alternate actor state. These masks contain falling bit `0x20` but
exclude jumping bit `0x10`. The original jump transition at `0x1005a830`, case 6,
sets bit `0x10` and explicitly sets up a bow upper-body animation channel using
actor properties `0x72` and `0x2c`. This is substantially stronger evidence for
jumping bow attacks than the generic manual sentence or state-name string.

Native PS2 attack entry `0x31da90` instead chooses refusal mask `0x104b70` for a
bow, including jump bit `0x10`. Before applying that mask it invokes the actor
reset callback `0x307198`, returning to `0x31de5c`, with the mask in saved register
`s2`. The native actor vtable in movement18 confirms that callback address.

The proposed fix removes only jump bit `0x10` from this known bow mask in that
callback. It preserves the actor's real airborne flags, falling refusal and all
other native gates. It is limited to the local on-foot bow in normal gameplay,
default OOTF behavior, the exact callback/caller and recognized native mask.
Modern mode, NPCs, other weapons, unknown masks and scripted play stay native.
Installation requires PAL identity addresses and AdvancedInput capability.
The focused bow test now passes 71 checks, including these negative guards and
verification that airborne actor flags are unchanged. Live acceptance of this
new jump-gate fix awaits its coordinated build; movement18 is the before case.

## Jump-gate live acceptance

`build/harness/bow-jump19` used executable SHA256
`25FBE3463740488A596552AFF0463596B0F5B511001E13D28563C57FD0D717A6`.
It quit normally at frame 1401. The callback diagnostic records mask
`104b70 -> 104b60`. The airborne shot consumes 198 to 197 while `flags94=0x90`
and Y is 172194; a later sample remains airborne (`0x10`, Y 172594) with 197.
The screenshot `jump_attack.png` visibly shows airborne Rynn holding the bow
and ammo 197. The forward shot and sight-strafe shot still consume 200 to 199
to 198. A later fresh shot consumes 197 to 196 after landing. No projectile
statistics are changed by this build.

`build/harness/bow-modern20` repeats with explicit `bow_aim=modern` and a held
attack released during the jump. It records `modern=1`, no adjusted-gate log,
and ammo remains 198 throughout the airborne/landing samples. A fresh ground
shot consumes 198 to 197. This is a live negative check for preserving the
existing modern option; non-bow and foreign-player guards are focused tests.

`build/harness/bow-combined21` deliberately opens the sight, holds forward,
steers with the mouse, jumps and shoots in one sequence. It identifies a
separate remaining restriction: sight remains open at FOV 40, mouse pitch
changes from zero to -0.628 radians and heading from 1.524 to 1.368 radians,
and a shot consumes 200 to 199, but the position remains unchanged and no
airborne flag appears. Forward and jump were refused before the attack gate.
This is **not** a passing combined running/jumping sight test. Native sight
strafe works in jump19/movement18; sight-forward and sight-jump require their
own original/native comparison before changing another gate.

## Sight jump command investigation

Native Controller C (`0x311220`) clears the jump command at player `+0x50c`
when the sight is open. Its callback at `0x458090`, return address `0x311314`,
runs immediately after that clear. The normal jump-consumption check is outside
the separate sight-dependent ground animation branch. Native jump start
`0x2f9520` also calls sight-close `0x421418`, return address `0x2f9610`.

The original PC main controller `0x10069500` has no corresponding sight-only
jump-command clear. It runs dedicated sight routine `0x100761a0` and retains
the normal jump-consumption path (`0x1005a6c0`). Its jump transition
`0x1005a830` does not close the sight. This supports preserving the command
and sight through a jump, independently of the remaining forward-profile issue.

The next narrow adapter snapshots only a genuine grounded jump command at the
native controller entry, restores it once at the exact post-clear callback,
and suppresses only the jump-start sight-close call for the local default-mode
bow and its current camera. It does not set physics flags, call a forced jump,
or clear camera flags. A new controller entry invalidates the snapshot; a newer
native command is preserved. Installation is PAL/AdvancedInput only. Focused
tests pass 90 checks; live results are still required for this adapter.

Sight-jump candidate runs `bow-sightjump22` and `bow-sight25` preserve the sight
at FOV 40 through a real native jump and fire an arrow while airborne. However,
`bow-runrelease29` finds an unacceptable locomotion defect: after W release at
4200 ms, X continues from 447262 at 4300 ms to 442426 at 6200 ms. The native
sight path skips the run/stand animation selection, leaving a run clip active.
Therefore the new sight-jump hooks are experimental only, installed with exact
`DRAKAN_BOW_SIGHT_JUMP_TEST=1`; they are not enabled by default. The separately
verified third-person jumping attack remains enabled normally.

Isolated mouse runs `bow-pitch26` and `bow-yaw27` establish that one mouse count
changes pitch or heading by 0.002 radians and remains stable with no W input.
The earlier sight pitch clamp is caused by W, not by a mouse sensitivity error.
Native `Camera_UpdateLook` explicitly replaces pitch axis 1 with forward axis 2
while the sight is open. Its axis-2 getter call is `0x1e4c70`, returning to
`0x44e680`. A separate default-mode direct-mouse adapter suppresses only that
camera read for the local bow, leaving the movement command/global axis and
physical controller sticks intact. Focused tests pass 98 checks. This camera
read fix is independent of the opt-in sight-jump experiment and awaits live
validation in a rebuilt executable.

## Experimental locomotion and torso ownership

`bow-axis30` validates the isolated camera-read correction: with W held and one
vertical mouse count, camera pitch remains +0.002 radians instead of drifting
to -0.628. Its experimental jumping view still follows the native animation
bones: camera back-vector Y grows from about -0.006 while stationary to 0.811
after the jump, despite unchanged camera pitch. This is a separate view-pose
problem; stable mouse pitch does not prove a stable sight image.

`bow-forward31` correctly selects native idle/run clips but initially does not
move. Native Controller C also applies model root-motion callbacks after the
clip selection. Normal native motion has model flags `0x980058`, the initial
adapter `0x980008`, and the earlier runaway after jumping `0x980048`. The
adapter now invokes the exact native model leaf setters for bits `0x10/0x40`
and clears `0x20`; its idle transition uses the native clear-`0x70` callback.
It does not synthesize velocity or change movement speeds.

`bow-forward33`, executable SHA256
`52769DC937347260B95A632A2C722C76EEB4417AC740739055ECDC8241DEDEAF`, then proves
forward travel and stopping: X 450247 becomes 449407 at 400 ms and 448066 at
900 ms; after W release, X remains exactly 447223 at 300, 800 and 1300 ms.
A shot consumes 200 to 199; a new W press moves again and its release stops.
The sight's bone-derived orientation remains wrong while moving. All this
locomotion code remains under the experimental environment gate.

The original PC class schema corrects an earlier informal label: property
`0x2c` is **Torso chan**, value 2; property `0x72` is **Archer Medium**. The
original jump transition pins that pose on torso channel 2 at frame zero and
speed zero, then marks the upper-body animation active. The native Rynn schema
also identifies torso channel 2 (property `0x52`). Native weapon motion mapping
resolves cached Stand to Archer Idle `0x20895`, Run to `0x20893`, and Walk to
`0x208ff`.

The next gated candidate retains an independently allocated torso channel with
the native resolved bow idle pose, uses the native layered base-motion helper
to preserve it through locomotion, and merges an owned pose back to the base
motion at its current phase/rate when the sight closes. It preserves genuine
attack/reload ownership. Focused checks pass 124 tests, including foreign-callback
ownership and phase-preserving release. Live camera-pose acceptance is pending.

`bow-transitions34` did not reach gameplay: it remained in native load screen 4
and ended at the harness time limit without a scripted quit. It supplies no
combat acceptance evidence and must be retried separately.

### Gated torso independence checkpoint (build4, bow-torso37 / bow-transitions38)

Canonical incremental build `build/ootf/bow-torso-build4.log` completed successfully. Private implementation executable SHA256 `764F1A23EDAF998E23535DE60AD43D6F14D43991F61CEED7B1BBEC8AFE71C093`. All candidate sight-jump/locomotion/torso hooks remain behind `DRAKAN_BOW_SIGHT_JUMP_TEST=1`; this is not default delivery. Focused bow tests: 134 checks, zero failures; formatting and diff checks passed. Both live runs used D3D11 with ray effects off to isolate gameplay from concurrent unfinished WebGPU changes, and exited by scripted quit.

The bounded post-controller reapply proved independent at the animation-record level: `bow-torso37/drakan.log` records four `independent=1` transitions with unchanged base resource, phase and rate. W moved X450247.25 to448091.84; after release X447249.5 was identical in the +300/+800/+1300ms RAM snapshots. The subsequent shot reduced ammo200 to199; post-shot W moved and stopped. This corrects the previous candidate's persistent-run regression. It does not establish camera correctness: with camera pitch zero, rendered camera back-axis Y changes from approximately -0.00342 standing to +0.27035 running (+0.26372 later). `forward_view.png` visibly looks down at the ground. A pinned independent torso resource alone does not solve native animated camera orientation.

`bow-transitions38` exercised forward, strafe, backward, genuine jump, mouse aim, fire, landing, release, sight exit and ordinary running. RAM confirms backward position stable after S release, actual jump flags94=0x10, mouse pitch +0.002, ammo200 to199, and subsequent unsighted movement. Combat audit frame1116 received the fire press while flags94=0x10; frame1117 had flags94=0x90, preserving real airborne state while firing. This is control-path evidence, not acceptance of the entire combined sight experience: view orientation remains wrong and lateral/exit stopping needs finer snapshots before promotion. No projectile statistics or flight behavior were changed.

Next investigation is the native sight camera's use of animation-derived aim transforms; do not infer a missing channel ownership bit or keep adding guessed layer flags. Original/native pitch multiplier evidence remains exactly -1.5, and any camera adaptation must preserve normal/native shot handling and be tested under the experimental gate first.

### Actual bow-sight camera path and shot alignment investigation

The first orientation probes (`bow-basis39`, `bow-basis40`) selected callbacks too early or outside the actual sight path. `bow-basis41` proved a post-controller basis write occurred but a later native writer replaced it. `bow-watch42` produced no write-watch evidence because `PS2X_WRITE_WATCH` is a compile-time feature absent from the canonical build; the environment variable alone does not enable it. These are failed timing probes, not accepted behavior.

Read-only PAL exports and live dispatch trace corrected the path identification: camera dispatcher218350 case6 selects normal follow41f848 without sight and **32a608** with sight. Generic first-person4227e8 belongs to mode1 and was the wrong branch for bow sight. `bow-dispatch45` on the existing executable records370 calls to32a608 from21878c with return218794 and the current camera DATA as a0. Native32a608 derives its view from player558/55c/560, whose local vector shifts from approximately(-.0013,+.00345,-.99999) standing to(-.261,-.270,-.927) running. That is the source of the animation-driven sight tilt.

Build9 (`build/ootf/bow-torso-build9.log`, SHA25626315508540066B322BC3B619DB5069FB7A96EE5A4013616D6530A8F21AC4FB4) applies the experimental basis adaptation after the actual32a608 completion/return218794. `bow-basis46` cleanly exited and proves final rendered RAM back=(.998935,0,.046136) consistently through standing/running/stopping/post-shot movement at camera pitch0; the PNG shows a forward horizon rather than ground. Native positional bob/eye are preserved. W release position is identical at300/800/1300ms. All superseded orientation timing writes have been removed from source.

`bow-combined47` confirms native airborne fire (audit1111 flags10 at press,1112 flags90), ammo200 to199, stable sight orientation with mouse pitch+.002, landing and exact stopped position across300/700/1000ms after W release. It also exposes why camera-only acceptance is insufficient: the native arrow pose still points away from the corrected sight. The ordinary Short Arrow resource3379d/engine1005f is verified through the resource registry, and the spawned projectile uses the copied pose; early release direction is approximately(-.964,-.193,+.181) against sight forward(-.99874,+.003,-.05013). No flight-stat adjustment follows from this observation.

Original PC launch evidence was obtained from the installed Dragon.rfl: Bow constructor100b0040 installs vtable1016bd10; slot+20 is launch100b0360. Its ordinary manual-sight branch(bit20) bypasses third-person auto-targeting and emits the projectile pose. Standard Long/Heavy/Speed Bow data has Accuracy100, missile offsetsY60/Z100 and sightFOV40. This establishes manual shot direction semantics, not a license to invent a finite convergence distance. The next experimental adapter rotates only a locally-owned ordinary Short Arrow's basis parallel to the actual sight ray, preserving every axis magnitude and native origin; no velocity/damage/gravity/lifetime/acceleration change is present. Special/tracking projectiles, NPC ownership, explicit native targets, third person, modern input and physical-stick input remain native.

Source simplification now removes the failed torso pin/reapply/ownership experiment entirely. Native cached Stand/Run/Walk selection and audited root-motion callbacks remain because native sight skips their start/stop transitions. Direct keyboard/mouse eligibility is shared across experimental jump, keep-sight, locomotion, camera and shot adjustments; all experimental hooks install together or not at all. Loading clears the new jump/run ownership so same-address quickload reuse cannot inherit it. Focused checks at this source checkpoint:143 pass after removing torso-only checks and adding scope/magnitude/reset checks. Live acceptance for this simplified checkpoint is still pending; the canonical build script rejected build10 because a separate performance session owns the shared build tree. No lock was bypassed.

### Simplified live acceptance checkpoint (build10)

Build10 succeeded after the other canonical build released its lock; staging executable SHA256 **56B895B45C6CC065779747B831F98D9F0336E4299A38C7C6BBFFB24F6737943C**. There is no torso pin/reapply/layer-ownership code in this checkpoint. All gameplay runs below used this executable, explicit D3D11/ray-off isolation and scripted clean quit.

- `build/harness/bow-simplified48`: complete sight-forward/strafe/back/jump/mouse/fire/landing/release/RMB-exit/ordinary-run sequence. Jump flags10 were real; fire entered flags90 without clearing jump. Sight pitch+.002 yielded stable backY-.003 throughout the jump/shot and ground movement. After W release, X440790.21875 was identical in the300/700/1000ms snapshots. Backward release was also stable. Arrow3356d at the early release snapshot points exactly along sight(-.998738,+.003,-.050131). Native camera position/bob remains untouched. Later unsighted running returns the original native animated camera.
- `build/harness/bow-directions49`: separate standing, running and airborne presses consumed ammo200→199→198→197. Audit1163 pressed while flags10,1164 entered flags90; `ram_air_launch.ram` still has flags10. `shot_metrics.json` identifies **spawned projectile resource3356d** through the actual native resource registry, rather than relying on held/nocked Short Arrow transforms. Measured launch direction versus camera ray differs by0°,0°,approximately0.0000012° respectively; first-axis magnitude remains approximately1 in all three. The adapter log independently records all three original axis magnitudes(1,1,1), and native spawn position is preserved. This is launch-direction evidence, not a change to speed or projectile flight parameters.
- `build/harness/bow-modern50`: explicit existing modern configuration with experiment env enabled; clean quit and no new sight-motion/jump/orientation/shot-basis adapter logs. Existing modern look/aim behavior remains active. This does not claim the modern mode has original sight mechanics.
- `build/harness/bow-gateoff51`: default OotF configuration with experiment env **unset**; clean quit, no candidate adapter logs, native first-person forward/jump restriction persists. This confirms the incomplete-development gate has not silently changed default delivery yet.

Final focused checks: **147 passed,0 failed**. These include preserving each arrow basis magnitude and origin; special ammunition/NPC ownership/explicit native target/third-person/modern exclusions; physical left/right stick, emulated stick-look and an explicit keyboard native-look binding all retain the whole native sight policy; loading invalidates jump/run ownership before same-address reuse. Physical-controller and NPC exclusions are focused guest-state checks, not falsely described as physical-hardware or NPC live-play comparisons. Formatting and scoped diff checks pass. Only tests were extended after build10; gameplay source is the exact validated build10 source.

This is ready for review as a scoped control/aiming checkpoint. It does not establish complete1:1 original combat: original per-bow cadence/properties and special arrows remain separate evidence/implementation questions, and an actual original-PC side-by-side runtime remains unavailable. The new combined sight controls are still explicitly gated in source pending default-delivery review. No projectile statistics or original owner/default-press/third-person airborne/committed axis fixes were reverted or changed by this work.

### Final default and ordinary-arrow-family acceptance

**Evidence-label correction:** the earlier `shot_metrics.json` files selected resource3356d, whose native name is **Wood Arrow Blur** (class60). Those files demonstrate the spawned flight-visual proxy's direction, not primary-projectile identity. Their wording above must not be read as identifying a primary arrow. They are superseded by `primary_shot_metrics.json`, which selects the exact actor logged at the native launch hook, validates native engine class1005f/resource identity through the resource registry, and confirms it is **not** the bow's current nocked actor. The numerical ray alignment remains true for the primary arrows themselves.

Default promotion was built by root into implementation SHA256 **6A9B068D1F38B383A1B03527455CE9C436B4F9A13478BF36D0CC621F7E9FBCBC**. `build/harness/bow-default52` ran without a test environment flag and cleanly quit. It confirms actual jump10 plus airborne press→attack90, stable mouse sight, shot200→199, landing, exactly stopped X440827.53125 across300/700/1000ms after W release, RMB exit and native ordinary running. The private gate is removed from source, rather than enabled through a delivered environment setting.

The native family audit (`build/ootf/native-bow-family.ts/.jsonl`) establishes that ordinary player bows use multiple arrows, not only Short3379d: Long337af, Composite337b0 and Battle/War/Rune families are native class95 with SeekType field0x10=0. Bow of the Order37ec intentionally has SeekType1. The final guard therefore uses class1005f plus the audited flat reader192e48 and an in-bounds SeekType0 field; it does not hardcode resource3379d. The field reader with default=-1 returns native field storage+index*4 and has no side effects. Unknown reader layouts, other engine classes and tracking/homing values are excluded. Shared properties are read only. This keeps the newly stable sight and ordinary-arrow shot direction coherent across the native family, without making camera eligibility depend on whether a replacement arrow exists during reload.

Family build `build/ootf/bow-family-build.log` passed; implementation-next executable SHA256 **6EAD220AE0756AFCD40C7F7D696D24E516DDCF85AD9CD6449A5CADB2CCAC4C05**. **152 focused checks pass**, including Long/Composite acceptance, intentional tracking exclusion, foreign class, unknown reader and field-storage bounds; all earlier mode/ownership/reset/magnitude checks remain. Root independently reran the152-check executable and reviewed the four source/test files.

`build/harness/bow-long53` is a preserved **failed fixture/editor regression**, not Long Bow acceptance: replacing the equipped Short Bow using real ground-template691 retained its equipped marker but native Bow DATA owner(+0x60,16-bit handle) and nock(+0x158,16-bit handle) were0. The launch could not consume ammo. Generator `build/ootf/make-longbow-fixture.ts` and original replacement card `build/ootf/bow-longbow/mc0` remain unchanged. Native inventory E unequip/re-equip in `bow-long-equip54` restored ownership; its shot300→299 and native F5 Slot33 save provide an independent initialized Long Bow fixture. Root is investigating replacement serialization separately. `bow-long55` accidentally toggled that save's persisted open sight closed; it is a native-mode check, not sight acceptance.

**Long Bow acceptance is `build/harness/bow-long56`**, using the native re-equipped save and the final family binary, default mode with no test env. It cleanly quit after sight-forward/strafe/back/jump/mouse/fire/landing/release/RMB-exit/ordinary-run. Audit1114 pressed with real flags10 and1115 entered flags90; ammo299→298. Camera backY is0 at pitch0 and-.003 at pitch+.002 throughout movement/jump. Stopped X440776.25 is identical in the300/700/1000ms samples. `primary_shot_metrics.json` identifies launched **Long Arrow337af/class1005f**, distinct from the current nocked actor, and measures0° versus the sight ray with axis magnitude1.00000012. `bow-directions49/primary_shot_metrics.json` similarly confirms primary **Short Arrow3379d/class1005f**, not its nocked replacement, at standing/run/air launches:0°,0°,approximately0.0000012°; magnitudes approximately1. The native20e6c8 launch routine copies this basis into its launch message before dispatch210ea0; the ordinary manual-sight path does not overwrite it through target selection. This bounds the claim to actual launch direction and control behavior; it does not claim an impact/hit-box or full original flight-stat comparison.

Ready source checkpoint: `runtime/input/include/input/DrakanInput.h`, `runtime/input/src/BowAim.cpp`, `runtime/input/tests/bow_aim_tests.cpp`, and `ps2recomp/ps2xRuntime/src/lib/drakan_input_glue.cpp`. Reference update: `docs/reference/input.md`; this journal is the detailed evidence. No other source files are part of this bow batch. All failed timing hooks and torso ownership code are removed. Default direct-MKB control/aiming acceptance is complete for the tested native Short/Long cases; exact original per-bow cadence, special-bow metadata and side-by-side original runtime fidelity remain explicitly unclaimed. No projectile velocity/stat adjustments were introduced.
