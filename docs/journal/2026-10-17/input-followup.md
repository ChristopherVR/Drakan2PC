# Movement/combat followup handoff

## 2026-10-18 build05: motion-pulse hold passes bounded incoming-hit replay

Full input CPU suite: **3,050 checks, zero failures** after the key-only90c0
mask. Combat command diagnostics also pass all nine named cases; inventory
projection diagnostics pass44/44. No production GS changes belong to this fix.

Combined build05 was frozen at08:02:06 before subsequent scale/Map/NR edits.
Private binary `build/camera-pitch-build05-private/bin/drakan.exe` SHA256:
`59438C4D0C339BAE8E8896C44AB7E66BECEC98705ACC31AECA78747C07ED7371`.
Outer HEAD ef1981f3f75268cbab14ae56ac47e983748b872d; nested HEAD
0c8a82f0e26dac87fd2b4e80b375a23aa0c3b295, with uncommitted input mask/tests.
Frozen DrakanInput.cpp SHA256:
`6B71FA14DCC2CD4098C28820F61516610BC2D587E10AB446D0B85190B197E650`.
Provenance JSON, source diff, build log and exact private recipes accompany it.
This candidate does not include the parent's later live scale fix.

Original on-01/on-02 runs hold exactly through all21 neutral samples but HP
stays100. Off-01 drifts .059989213943481445rad, also without damage in that
window. On-02's first observed damage is after hold20, before target release.
Therefore a private derived recipe delays commands49..100 by exactly20 ticks,
retaining equip/target acquisition, the same existing hostile675 fixture and
all actions. No scene search, forced native flags or original save writes.

`aim-underattack-build05-on-delayed20` then holds exactly
-.20045363903045654rad at hold00/10/20 and all21 per-frame pitch log samples
(logs round to four decimals). HP100/100/90 proves an incoming hit in the hold;
audit frame683 has flags90=660c0 and flags94=100, surrounded by66000.
All21 mouse samples are neutral and pad0000. Native target83/a5f000 remains
active, aim true, camera mode6/flags10200, control1/scripted0. Target anchor
changes -.2659020 ->-.3236047 ->-.3733344; all three sampled native pitch
vetoes are false. Enemy83 remains alive at HP40 throughout; native rate9c
evolves while pitch stays fixed. Matching delayed off control drifts
.04454508423805237rad but has HP100 throughout its hold, so this is not a
matched incoming-hit on/off comparison. All five aim runs quit normally,
exit0, with unchanged executable, original, source and private saves.

This proves this bounded hit/motion-pulse regression, not every combat camera
transition. Actual veto during an incoming hit was not observed live: CPU
cases establish veto stops writing and ownership does not automatically
recover. RAM snapshots are not an exhaustive per-hook veto trace. Subsequent
combined scale/Map/NR integration needs a new frozen binary and validation.
Parent then paused additional build05 launches: the already in-flight
native32 helper finished23/23 (categories7/7, Equipment7/7, movement/bow9/9),
script quit/exit0 after94.4224541seconds, with unchanged binary and all three
save roots. GPU was released immediately afterward; no game process remains.
Neural21 and release-level23+23 checks wait for Map source freeze and a new
combined candidate. Build05's23-check result excludes those later fixes.
No deployment or push.

## 2026-10-18 second-hit cause: native motion pulse resets host key

Source-only review during the quiet performance slot establishes a sufficient
cause for the build04 failure. No compile, build, diagnostic launch or game run
was performed during this review.

- Audit f647..665 has flags90=66000; f666 has660c0; f667 returns66000.
  Flags94 is100 at666/667. Frame665/666 logged pitch is-.1868347;
  frame667 is-.3134988. Mouse remains neutral. Sparse hold RAM retains the
  same player be4680, controlled handle a5b6a0, camera bba4e0, target83/a5f000,
  camera flags10200/mode6, control1/scripted0; sampled native veto is false.
- `readAimPitchState` formerly copied all flags90 into the identity key.
  The pulse therefore makes `beforeCameraUpdateLook` clear aimPitchOwned.
  Restoring66000 does not re-arm it because pendingDy is zero: ordinary native
  target soft-limit/rate integration is then allowed to keep changing pitch.
  This is a host bookkeeping reset, not evidence that the native pitch-veto
  predicate rejected the hit. Existing logs are not a per-hook identity trace;
  do not claim every other possible transient was observed directly.
- Native331d60 is the Controller C bit94.8 response update. Instructions
  331e98/331e9c/331ea0 OR flags90 withc0 while updating +f0 from +b64.
  Native32cc00 clears bits6/7 and +f0 (32cc1c..32cc30). Other motion paths
  similarly pairc0 with current height+e8 and motion+f0 (315c80..315c90,
  331d10..331d28). Controller C uses bit6 to suppress the vertical displacement
  component before applying motion. These are native motion bookkeeping bits;
  they are not individually named as "matrix dirty" without further evidence.
- Camera_UpdateLook44e4a8 reads player90 bit14, not bits6/7. Its native Rynn
  pitch veto32cda8 reads player94 masks c000/80/10 plus ab0/af0, not flags90c0.
  Actual hit-state94=100 does not satisfy that predicate. Mode/scripted/control,
  target/identity and other guarded flag transitions remain separately relevant.

Prepared narrow source correction: mask ONLY90 bits6/7 in the ownership key,
in addition to the already justified94 bit8. Guest flags, movement, animation,
native pitch veto and all other identity/control checks remain untouched.
No permission-veto recovery policy change. The CPU regression replays
the exact66000/660c0/66000 pulse through21 neutral samples, verifies motion/rate
fields remain native, tests all30 other90 bits cancelling ownership, and checks
that a real native veto still prevents writes and does not auto-recover.

**Validation:** the released-slot full suite passes3,050 checks for this90-key
correction; the earlier2,870 checks belong to the prior anchor/94-bit8 candidate.
No compile or launch was performed during the quiet performance slot.
No opt-in ownership diagnostic was added
because the source/audit now identifies this concrete reset path. No whole-hold
or full combat-control parity claim.

## 2026-10-18 build04 under-attack hold: partial improvement, full hold FAIL

Frozen private candidate: `build/camera-pitch-build04-private/bin/drakan.exe`,
SHA256 `A31156A75FC585F6BE7E40B25BB6230FC9B26C50A289C77F72B6F571E2D31455`.
Provenance outer ef1981f3 / nested0c8a82f0 matched the copied executable.
Both runs use the new100-frame equipped recipe and private on/off INIs whose
only effective difference is pitch_persist. Original/source/private cards stay
unchanged; both finish normal script quit/exit0. GPU was released immediately
after off completed. No source fix, build or further launch followed.

| Mode | Hold0 /10 /20 pitch (rad) | HP | Outcome |
| --- | --- | --- | --- |
| on | -.1868347377 /-.1868347377 /-.3134988248 | 90/90/80 | First10 ticks hold exactly; full20-tick span .1266640872 **fails**. |
| off | -.1980849952 /-.2448889762 /-.2602030635 | 100/100/100 | Native pitch drift .0621180683; no incoming damage in this interval, so not a matched under-attack comparison. |

Both have actual target83/handle a5f000, aim bit14, camera mode6 and all21
audited mouse samples dx=dy=0 through the20-tick hold. All three sampled
native veto results are false. On holds camera pitch while target anchor moves
for the first10 ticks, confirming the anchor/94-bit8 correction has a live effect.
The second incoming hit coincides with audit flags90 **66000 ->660c0 ->66000**
at f666/667; flags94 goes to100. Thus90 bits6/7 are another transient ownership
key difference despite unchanged sampled target/mode. This is a concrete
remaining failure, not a full live pitch-persistence pass or a known-safe reason
to broaden the mask. Native meaning of90 bits6/7 still needs review before
changing it; needed combat-camera transitions must remain intact.

Per-run raw reports/manifests and compact `live-summary.json` are under ignored
`build/camera-pitch-build04-private/`. Queue native-code tracing of90 bits6/7,
CPU regression replay of the observed pulse, then a bounded source correction
only if justified. Parent reclaimed GPU and requested quiet CPU for Averroes's
four-run profile: **no more launches, builds or CPU test compilations** until
released. Documentation/evidence only in this handoff.

## 2026-10-18 moving-target ownership correction (source + CPU)

The exact build02 `ram_aim_moved` -> `ram_aim_neutral` delta is:

| Field | Moved | Neutral | XOR / interpretation |
| --- | --- | --- | --- |
| player90 | 66000 | 66000 | 0, no bit change |
| player94 | 100 | 0 | 100, **only bit8** changed |
| camera48 | 10200 | 10200 | 0, no bit change |
| target | slot83/handle a5f000 | slot83/handle a5f000 | same identity |
| anchor114 | -.2258444875 | -.3077032864 | native moving-target value |
| pitch84 | -.1215199083 | -.2992094159 | failed prior live hold |

Both native32cda8 veto results decode false from these RAM states. Read-only
headless decompilation of native460670 establishes that it resolves target4da,
reads target/player positions, transforms the normalized relative direction
through player heading, computes vertical/distance terms, and writes player114.
Player90 bit17 optionally smooths that update; vslot9c clamps it. This is a
normal changing target anchor, not camera identity. Camera_SoftLimitAxis
44e2d8 adds it to its axis1 target whenever player90 bit14 is set.

Native331ec0 clears player94 bit8 alongside movement/animation cleanup and
ab0 bit1. Controller C invokes camera look before handling its bit8 state.
Neither44e4a8 nor its32cda8 pitch predicate treats bit8 as a pitch veto. The
earlier ownership key's exact anchor/full94 equality therefore discarded
ownership on normal native updates, reproducing a concrete CPU failure.

Fix: remove anchor from the identity key (still require finite), mask ONLY
player94 bit8, and require a real resolved target. Keep every other player90,
player94 and camera flag in the key, along with mode/identity/vtable checks.
Keep native pitch veto, cutscene/control, focus/menu/suspend, real/digital look
takeover, target replacement/loss and camera transition guards. No native
anchor/rate/yaw/target state is rewritten. No new nested glue change is needed.

CPU evidence: full suite **2,870 checks / zero failures**. Added410 checks
replay actual acquired/moved/neutral values,60 neutral ticks with anchors
changing both between frames and within hooks, bit8 toggles, unchanged rates
and anchors, all31 other94 bits cancelling ownership, and veto/cutscene/target/
camera-transition rejection. A private copy restoring the old exact anchor/94
key fails **122 of410** new checks; current source passes. Combat input's nine
named checks and inventory mouse44/44 also pass. No game launched for this
correction. Read-only native exports, field delta and enhanced predicate report
are in ignored `build/camera-aim-source-review/`.

Next live recipe: `scripts/archive/2026-10-18/combat_camera_pitch_under_attack.txt`, existing
hostile675 cards only, rebuilt frozen candidate and private on/off INIs. Q at2,
equipment10, R2 down30/up32, acquired34, baseline49, mouse(0,-2) each50..59,
hold62/72/82 (**20 neutral ticks**), R2 down90/up92, release94, quit100.
No outgoing attack, movement or save commands. Check HP100/alive and resolved
dagger/target before the hold; require the SAME live target/aim/control/mode
through hold62..82, no native veto, finite changing anchors, and actual incoming
HP loss to establish the under-attack case. Require on-config pitch span<=1e-5
over those three neutral dumps; report off-config span separately. Unknown
state, loss/replacement, veto, missing incoming damage or death invalidates the
case; do not turn it into a hold pass. Audit every frame to confirm dy0 throughout
the hold, normal R2 releases/script quit, frozen executable and save hashes.
The enhanced camera report emits the recognized predicate, signed this-adjust
and decoded native veto. This new recipe has not yet been launched.

## 2026-10-18 frozen build02 live result (GPU released)

Three bounded runs used the private frozen executable
`build/camera-pitch-build02-prepared/bin/drakan.exe`, SHA256
`1A964FC65436C8041F1824A3D93DBF360EBB92AB3ABCEE470ACEC8CAC9573FD8`
(source candidate mtime 07:33:23). Build03 was not substituted. Private on/off
INIs preserve the staged bindings, .002 X/Y sensitivities, signs -1 and inversion
zero; their only effective difference is pitch_persist. Staged input hash stayed
unchanged. Preparation/config hashes and exact commands are in the ignored
`build/camera-pitch-build02-prepared/preparation.json`, `run-live.ps1`, per-run
manifests and `live-summary.json`. Source script is
`scripts/archive/2026-10-18/combat_camera_pitch_followup.txt`; the equipped combined camera/melee
recipe is retained privately as `camera-melee-equipped.txt`.

| Run under private `t/` | Proven result | Limit |
| --- | --- | --- |
| camera-pitch-build02-on-01 | Raw free pitch +.0400000028, heading -.0800000067; opposite counts return within 1.3e-8 rad. All three free-camera checks pass. | All samples target0/aim false: **not live proof of stable aim branch**. |
| camera-pitch-build02-off-01 | Same raw counts delivered; neutral samples return pitch near zero with native persistence disabled. | Also target0/aim false; cannot serve as an equipped-aim comparison. |
| camera-melee-build02-on-02 | Existing fixture plus established Q equip reaches ED0 dagger class1000e, target83/handle a5f000, aim bit14 and camera mode6. Same enemy83 HP40 ->36 by hit2. Rynn takes actual damage and is HP0 at strafe/final. | Aim pitch **does not stay fixed**: moved -.1215199083 ->neutral -.2992094159. Anchor changes -.2258444875 ->-.3077032864 and player flags change. No stable-state preservation pass, successful parry, combo or live strafe verdict. |

The equipped run repeated the same free-camera checks successfully before
targeting. Its camera flags are10200, control1, scripted0, target83 during the
aim interval. The changing anchor/flags are observations, not an isolated cause
or permission to relax the bounded stability guards. No equipped-off repeat
was launched after the parent closed the slot. Further stable-aim work needs
before/after state/guard-rejection evidence on the exact linked candidate,
not a claim transferred from CPU simulations.

All runs finish script quit/exit0 (46.38s,44.28s,52.30s), retain the frozen
executable hash, and preserve original/source/private cards. Melee Square
down/up frames781/787,801/805,821/825; Circle846/906; final pad0 at966.
These prove release delivery, not defensive mitigation. No screenshots,
framebuffer/performance verdict, scene hunt, dist edit, commit or push.
**GPU released immediately when the final run exited; no further launches.**

## 2026-10-18 bounded camera implementation (CPU validated; live pending)

After the parent explicitly released CPU compilation, the complete new input
suite compiled and passed **2,460 checks / zero failures**. Combat CPU diagnostics
also passed: rapid-click/command/focus diagnostic (all nine named cases), inventory
mouse **44/44**, native Quick Inventory bridge **10/10**, and dragon-context
diagnostic (18 synthetic checks plus four read/identity assertions on two existing
RAM files). The dragon files resolve pre-mount as dragon=0/gameplay=0 and final
as dragon=1/gameplay=1. No fixes were required after compilation/testing.

The new camera report helper was executed read only on the eleven existing
`build/t/input-followup-melee-01` RAM files. All eleven actor/camera reports are
available; ten show mode6/aim-bit14/target handle a5f000 and the pre-equip sample
has no aim bit/target. This verifies helper operation and that the historical
fixture reaches the relevant aim state; it does NOT validate the new camera
behavior live. Source diff whitespace checks pass. Standalone diagnostic builds
emit existing fopen deprecation warnings only.

CPU commands used from the decomp root:

```powershell
./runtime/input/tests/run_tests.ps1 -OutputDir "$env:TEMP/drakan-camera-input-tests"
./scripts/harness/combat_input_diagnostic.ps1
./scripts/harness/combat_inventory_mouse_check.ps1
./scripts/archive/2026-10-17/combat_quick_bridge_check.ps1 -BridgeHeaderDir './ps2recomp/ps2xRuntime/src/lib' -OutputDir "$env:TEMP/drakan-camera-quick-check"
./scripts/archive/2026-10-17/combat_dragon_context_check.ps1 -HeaderDir './ps2recomp/ps2xRuntime/src/lib' -SourceRoot '.' -OutputDir "$env:TEMP/drakan-camera-dragon-check" -RamFiles './build/t/srnr-final-dragon-context/ram_dragon_start.ram','./build/t/srnr-final-dragon-context/ram_final_check.ram'
```

Source is implemented in `src/DrakanInput.cpp`, its header, and the single
camera-before call in `ps2recomp/ps2xRuntime/src/lib/drakan_input_glue.cpp`.
The glue now supplies the actual callback player to the before hook. Legacy
two-argument callers keep position-mode suppression but cannot preserve aim
pitch without that player. README wiring is updated.

Direct pitch rejects camera bit0x1000, player94 bit2, vslot17c veto and the
alternate pitch channel. Rejected counts are consumed, not deferred. Yaw's
existing native gates remain separate. `423790` was resolved from generated
instructions at `build/recomp_out/gen/ps2_recomp_0064.cpp`: it returns zero
when camera bit12 is set, otherwise camera bit11. No speculative guest call is
needed to identify the alternate channel.

Aim persistence is on-foot/direct/pitch_persist only, normal camera mode6,
player90 bit14, native control enabled, unscripted, finite pitch/anchor, no
native pitch veto, no physical/digital look takeover, no alternate/first-person
or alternate-smoothed channel. It acquires ownership only after vertical mouse
motion; opening a target without moving the mouse keeps native acquisition.
Within one before/after pair, matching player/context/handle/inner/camera,
heading, target slot/handle, anchor bits, player/camera flags and vtable predicate
identity permit restoration of pre-call pitch plus clamped raw displacement.
Neutral frames retain pitch only while that ownership remains valid. Any
observed transition drops the snapshot/ownership; a transition within the pair
also discards its old vertical sample. Rates9c/a0/a4, yaw90, alternate pitch/yaw,
collision, targeting and controller lerps are not written by this preservation.

Mutex/re-entry audit: two existing RAM snapshots (`input-followup-melee-01`
before_hit and `encounter-hostile675` begin) resolve Rynn vtable52d8e8,
slot17c ->32cda8. Generated instructions in `ps2_recomp_0046.cpp` show a leaf
reading +94/+ab0/+af0 and returning, with no callee or input-hook path. Existing
dragon-context final RAM resolves vtable5268d8, slot17c ->276e40; generated
`ps2_recomp_0033.cpp` shows return-zero only. The new pitch call under m_ is
allowlisted to those two native leaf addresses, preserving signed this-adjust.
Unknown vtable overrides fail closed without being called. Existing yaw's
older guest-call pattern is unchanged; this is not a claim about arbitrary
replacement functions or all runtime hooks.

CPU regression source now explicitly covers five pitch veto/unavailable cases,
discard/recovery, independent yaw, signed this-adjust, no unknown-vtable call,
native acquisition before mouse ownership, **30 constant-neutral frames after
valid ownership**, once-only snapshot consumption, unchanged rates/yaw/alternate
fields, target replacement, focus/suspend/menu transitions, config opt-out,
alternate mode, both dragon mouse modes, pad/digital pitch takeover, and 21
between-hook state mutations including unload and vtable changes. Native
updates are simulated writes in FakeGuest; these are not actual guest execution.

Compilation was initially paused during Averroes's exclusive performance matrix.
That pause is now superseded by the successful CPU runs above. No GPU/game
launch or full runtime executable rebuild occurred in this input task. Parent
retains GPU ownership for its build/viewport test; the historical 1,998-check
result below predates this camera candidate.

### Exact live recipe, only after CPU validation and parent GPU grant

Use `scripts/archive/2026-10-18/combat_camera_pitch_followup.txt` on a private copy of the existing
`build/recomp_combat/encounter/fixture-hostile675` cards. Parent must first
incrementally rebuild its candidate with this outer input source AND the nested
one-line camera glue change; set `$CandidateBin` to that newly built directory.
Use a private copy of the candidate's INI via `$PrivateInputIni`, keeping all
bindings/other sections. Set direct mode, pitch_persist=1, sensitivity_x/y=.002,
yaw_sign=pitch_sign=-1 and invert_x/y=0 only in that private copy. Never edit
the deployed/staged user INI. For a second comparison use another private INI
with only pitch_persist=0 and a distinct fresh run name on the same executable.

```powershell
./scripts/harness/combat_encounter_run.ps1 -DecompRoot 'C:/Games/Drakan - The Ancients Gates/decomp' -BinDir $CandidateBin -SaveRoot './build/recomp_combat/encounter/fixture-hostile675' -Script './scripts/archive/2026-10-18/combat_camera_pitch_followup.txt' -Name camera-pitch-target-on-01 -TimeoutSeconds 160 -InputConfig $PrivateInputIni -AuditEvery 1 -NoCapture
python ./scripts/harness/combat_movement_followup_check.py $EvidenceDir --camera-only --sens-x .002 --sens-y .002
python ./scripts/archive/2026-10-18/combat_camera_pitch_report.py $EvidenceDir
```

`$EvidenceDir` is the runner's `t/camera-pitch-target-on-01` directory under the
parent of `$CandidateBin`. The new report is read only and emits raw camera
flags/mode/rates, anchor, native control/scripted state and target handle in
addition to existing actor/HP evidence. It emits unavailable fields on errors
and makes no automatic gameplay-pass assertion. It has been checked against
existing RAM only, as described above.

Script offsets after sync-play90: baseline10; (4,-2) per frame20..29; moved32;
(-4,2)40..49; return52; direct native R2 down60/up62; acquired64;
(0,-2)70..79; moved82; neutral112; R2 down120/up122; released124;
(0,2)130..139; moved142; neutral172; quit180. No attack/movement/save command.
Expected free-look increments are yaw-.08 and pitch+.04, with the return near
baseline. Target phase has +.04 vertical counts; only classify persistence if
mode6, player90 bit14, matching target/anchor/identity, no veto and live Rynn are
actually present. Compare moved82 -> neutral112 (+30 ticks) separately from the
initial increment, and compare off-config control without asserting it must
drift (native smoothing/anchor may be inactive). Missing target, death, mode
changes or target replacement make that phase unavailable, not a pass. Require
native R2 releases, normal scripted quit, identical executable hashes between
config comparisons, and unchanged original/source card hashes. No framebuffer,
performance, parry, combo or subjective control-feel verdict is implied.

## 2026-10-18 inventory fixture correction and camera proposal

The 11/43 inventory diagnostic failures recorded below are resolved by fixture
corrections only (`scripts/harness/combat_inventory_mouse_check.cpp`). Three failures
were caused by publishing geometry before the first screen-change update;
`MenuMouse::update` correctly resets that capture. The diagnostic now explicitly
asserts rejection on that tick, then captures the next draw before clicking.
Eight failures used the old centered page map, including the headless cursor.
Current `runtime/gs/src/hud_page.h::inventoryMap` returns `{0,aspectScale}`;
`d3d11_backend.cpp` applies it to inventory page vertices and clip rectangles,
and `drakan_settings_host.cpp` publishes the page factor separately from HUD
scale. `updateInventory` correctly inverts it with anchor zero.

The fixture now calls the production renderer's forward-map helper (read only)
to place scaled cell clicks, checks the unused RIGHT margin, and uses virtual
cursor (56,126): physical (334,500), native (43,125) at the existing synthetic
3818x2032 client/3612-wide viewport. Resize expects (336,531). This retains the
same native cell and selection assertions rather than accepting a different
item. Final diagnostic: **44 checks, zero failures**. No production projection,
GS, native selection, configuration, save or dist changes.

Camera inspection (proposal only; no camera production edits this turn):

- `analysis/input/0044e4a8_Camera_UpdateLook.c` integrates pitch rate at +9c
  using `min(1,dt*player38)` and yaw/orbit rates at +a0/+a4 using
  `min(1,dt*player3c)`. The standalone direct hook adds raw displacement after
  native integration; mouse itself does not feed those rate accumulators.
- Clearing the position-mode flag in `beforeCameraUpdateLook` does NOT disable
  all native pitch return. The independent player90 bit14 branch still calls
  `Camera_SoftLimitAxis`. Its axis1 target includes player114; with neutral
  axis input, it can pull an existing pitch toward that anchor. Thus direct
  pitch persistence can conflict with this native aim/target state. Exact live
  occurrence in the current target fixture is not yet measured.
- A separate concrete mismatch: the direct after-hook writes +84 without the
  native camera48 bit0x1000 gate, player94 bit2 axis suppression, or normal
  pitch vslot17c veto. A temporary CPU probe using the existing FakeGuest and
  production module measures +0.04 pitch for dy=-20 under each restriction.
  The probe executes only the host hooks; native control flow is established
  from the decompilation, not claimed as a live native test.
- Controller C calls camera look before `Player_GatherInput`, then retains its
  own target/controller turn smoothing and target validity gates. A nonzero
  target reference alone does not prove a camera mode or authorize disabling
  those gates. Existing follow/collision smoothing is not a diagnosed defect.

Recommended bounded implementation before a camera GPU run:

1. Honor the native pitch vetoes for direct mouse, independently of yaw, and
   drop rejected pitch counts so blocked motion cannot replay later. Do not
   equate player94 bit2 input suppression with zero native residual rate.
   Add CPU cases for the three vetoes, allowed pitch/yaw, and discarded counts.
2. Separately, for on-foot direct look with pitch_persist enabled and neutral
   physical/digital pitch controls, preserve the pre-call +84 around the native
   aim soft-limit step, then apply/clamp the raw mouse increment. Restrict this
   to a stable player/camera/target/aim state across the call, no native veto,
   and normal (non-alternate) pitch channel. On transitions discard the saved
   pitch. Leave yaw/target selection, collision, alternate channels and dragon
   control native. Do not globally zero +9c/+a0/+a4 or disable controller lerps.
   This needs CPU callback-order/state-transition tests before implementation
   is considered ready for a live candidate; alternate-mode predicate 423790
   must be resolved explicitly, not guessed from target!=0.

No GPU reservation or game launch made. Parent inventory diagnostics retain
their slot. A later camera run should use the existing hostile save fixture:
record camera84/9c, player90/94/114, camera48/14c and target identity before
F/R2, immediately after target acquisition, after ten (4,-2) mouse samples,
and after 30 neutral ticks; repeat after releasing targeting. Require the
relevant aim branch/state to be present before attributing pitch return to it.
This is a proposed measurement, not an executed gameplay or parity verdict.

## 2026-10-18 CPU ownership follow-up (uncommitted)

`setSuspended(true)` previously only changed an atomic flag. A true/false
transition without an intervening guest hook retained both the cached (or
not-yet-synthesized) attack/movement sample and accumulated direct mouse counts.
The new regression reproduced eight failures: attack and D movement remained
active, and delayed camera pitch moved +0.04 radians after closing suspension.
This is a CPU API sequence defect; occurrence in a live overlay session has
not been established.

Suspension now invalidates input at the transition under the module mutex:
keyboard/mouse/gamepad snapshots, pad cache, accumulated mouse counts and host
edge queue are cleared. Pending command presses are discarded and held-command
releases remain queued for the EE hook. Explicit host taps retain their existing
exemption. Fresh input on the next frame remains usable. No config, binding,
native combat gate, nested runtime, dist or save edits were made.

CPU evidence: `runtime/input/tests/run_tests.ps1` passes 1,998 checks, including
cached/uncached suspension transitions, no stale camera movement, fresh attack
recovery, and command releases with/without prior delivery. The combat input
diagnostic passes, including three rapid click transitions and held-command
release. No GPU/game launch; no controller-feel or combat parity verdict.

`scripts/harness/combat_inventory_mouse_check.ps1` reports 43 checks / 11 failures.
An isolated temporary source copy with both production files restored from
HEAD reproduces the identical 11 failures (selection/projection/page-scale
cases). These predate this suspension change; the older 43-pass statement
below is historical. Category/hit-geometry work remains with its existing owner.

Direct on-foot displacement accumulation/one-time consumption is covered by the
existing 30/60/120/240-sample CPU cases, including inversion and skipped camera
ticks. Native target-camera constraints, collision, timed parry, combo progression
and repeat-cast acceptance still require their own live evidence. Parent retains
the exclusive GPU slot and framebuffer ownership. If a camera comparison is
scheduled, the precise existing recipe is `scripts/archive/2026-10-17/combat_camera_followup.txt`:
sync play 90, baseline +10, mouse (4,-2) each frame +20..29, dump +32, reverse
(-4,2) +40..49, return dump +62, quit +80. Use a private copy of the existing
bow fixture and INI, audit every frame, and the camera-only checker documented
below. That recipe establishes angle displacement/return only, not combat feel.

Fresh bases: outer `7a3625b`, nested `2c863bd`. No inherited probe code copied.
Source-only changes. GPU was subsequently assigned for three bounded runs on exact staged candidate `12919E3F3D17CFDED8101D55E2D7B3E8E12EA98F9E4C4ABE619B4E43A722708B`; all quit normally. GPU released after the camera comparison; no further launches planned.

## Live results on the combined candidate

Evidence directories under main `build/t/`:

| Run | Native engine evidence | Limits |
|---|---|---|
| `input-followup-movement-01` | 7/7 native Equipment checks; 9/9 movement/bow checks. Heading delta -0.08000002 rad, pitch +0.04000000 rad; return residuals below 1.3e-8 rad. Forward X/Z displacement (-74.5, +4.53125), D displacement (+41.90625, -594.5), unchanged heading through D. Bow resource1004f in EAC; ammo 200,199,199,198,198,198 at baseline/first-active/three releases/settled. Quick Inventory inactive with EAC=ED0=0. | Different travel distances are fixture observations, not a speed-equivalence claim. No uniquely identified new projectile caught. Target slot0 here: no lock-on pass from this fixture. |
| `input-followup-melee-01` | Native dagger1000e in ED0 by target/before-hit snapshots (initial immediate post-Q dump was too early). Same enemy slot83 HP 40 before hits,36 at hit3,32 during held RMB,28 by strafe/neutral. Slot84 fell40→38. Target slot83 selected. Native strafe displacement observed after RMB release. | Rynn100→90→80→60; no successful parry/damage mitigation established, reduction-window bit false at all scheduled dumps. Combo progression not established from delayed health changes. Native target/camera behavior not visually verified. |
| `input-followup-camera-inverted-01` | Private INI copied from stage, sensitivity X/Y .004 and both inversions1. Same 40/-20 raw counts gave heading +0.15999999 rad, pitch -0.08000001 rad. Return residuals <7.2e-9 rad. | Tests combined X/Y inversion plus doubled sensitivity; independent axis toggles covered by CPU tests, not separate live launches. |

All three `encounter-manifest.json` files report original/source saves unchanged. Stage exe hash remained identical. No dist or staged configuration edited. No automatic PNG capture and no performance verdict; legacy encounter runner did enable SND_DBG1.

`release-audit.json` in movement/melee evidence extracts per-frame input edges. Bow Square down/up pairs were f1242/1262,1292/1296,1312/1316, with pad0 after each release. Thus all three attempts reached the pad, but only two arrows were consumed; native cooldown/attack acceptance was not traced, so the last non-shot is not attributed to a dropped click. Melee Square pairs f673/679,698/702,718/722; Circle held continuously f748..867 and pad0 at release f868. This proves input release delivery, not successful parrying.

Replay camera assertions without GPU:

```powershell
python ./scripts/harness/combat_movement_followup_check.py <movement-directory> --camera-only
python ./scripts/harness/combat_movement_followup_check.py <inverted-directory> --camera-only --sens-x .004 --sens-y .004 --invert-x --invert-y
```

Read-only shadow-repro handoff: copy `build/recomp_combat/encounter/fixture-hostile675` (hash `8cf48540...`) or run card directory `cards/input-followup-melee-01`, then use `scripts/archive/2026-10-17/combat_melee_followup.txt`. Pose snapshots are `ram_before_hit.ram`, `ram_block_held.ram`, `ram_strafe.ram`. No edits to the source cards. Main owns the next GPU reservation.

## Concrete defects and fixes

- Focus restoration: LMB used to recapture was also synthesized as Square, including its held drag. Consume that gameplay click until release; the next click attacks. Preserve real menu clicks.
- Overlay ownership: repeated pad reads returned a cached attack after suspension. Replace the cached sample with neutral input immediately; explicit injected host taps keep their documented exemption.
- Numeric configuration: `strtof` accepted NaN/infinity/overflow, allowing invalid angles and stick conversion. Reject nonfinite values and retain the previous/default setting with a warning.
- Legacy migration: explicit positive `pitch_sign` was unconditionally reversed both in memory and on disk. Preserve original bytes and user values; append only the existing version/default marker. An absent sign inherits the current -1 default. Already migrated files cannot have historical intent reconstructed automatically.
- Add `[mouse] invert_x=0|1` for both direct yaw and mouse-to-stick X. Existing `invert_y`, sensitivity, sign and controller settings remain effective. No automatic binding or inversion override.

CPU evidence: regression tests first reproduced four failures in recapture/held-drag and cached suspension. After fixes: 781 input checks pass. Additional rapid-click diagnostic, 43 Equipment mouse checks and 10 native Quick Inventory bridge checks pass. These are CPU observations, not enemy damage or gameplay feel verdicts.

## Build and CPU tests

From this outer worktree:

```powershell
./runtime/input/tests/run_tests.ps1
./scripts/harness/combat_input_diagnostic.ps1
./scripts/harness/combat_inventory_mouse_check.ps1
./scripts/archive/2026-10-17/combat_quick_bridge_check.ps1 -BridgeHeaderDir ../PS2Recomp-input2/ps2xRuntime/src/lib -OutputDir "$env:TEMP/input-followup-quick"
```

Main should cherry-pick the outer source commit and rebuild its combined runtime with `PS2X_INPUT_DIR` pointing at the integrated `runtime/input`. No nested production change or regeneration required. Standalone CMake alternative is in README. Preserve actual dist INIs: do not deploy freshly generated defaults over them.

## Bounded live checks (only after GPU assignment)

Use main's staged `build/integration_followup` as BinDir, separate run names, copied cards, 160-second timeout. Existing `combat_encounter_run.ps1` copies fixture cards again and records source/original hashes. `-AuditEvery 1` records every input frame; `-NoCapture` disables automatic PNG writes. This followup makes no pacing claim.

1. `scripts/harness/recipes/combat_movement_followup.txt` uses existing `build/recomp_combat/encounter/fixture-bow-item`, 850 gameplay frames after `sync play 90`. It repeats the proven 7 Equipment click snapshots, verifies dagger ED0 then bow EAC, closes inventory, records mouse look/return and native positions for W/D, toggles targeting, holds/drags/releases bow attack, attempts two rapid attacks, and releases Quick Inventory. Fixed cursor positions use the current virtual640x512 calibration; if rendering geometry changes, regenerate these from the existing grid recipe rather than guessing.
2. `scripts/archive/2026-10-17/combat_melee_followup.txt` uses existing `fixture-hostile675`, 380 gameplay frames. It snapshots equipment before the encounter, target, enemy HP before/after three attack attempts, held block windows/release and strafe/neutral. This is a private relocation fixture, not a claim of normal quest progression.

Example main invocation (after the exclusive GPU slot is granted):

```powershell
./scripts/harness/combat_encounter_run.ps1 -DecompRoot 'C:/Games/Drakan - The Ancients Gates/decomp' -BinDir 'C:/Games/Drakan - The Ancients Gates/decomp/build/integration_followup' -SaveRoot 'C:/Games/Drakan - The Ancients Gates/decomp/build/recomp_combat/encounter/cards/inventory-phase-calibration' -Script ./scripts/harness/recipes/combat_movement_followup.txt -Name input-followup-movement -TimeoutSeconds 160 -AuditEvery 1 -NoCapture
python ./scripts/harness/combat_inventory_mouse_ram_check.py <output-directory>
python ./scripts/harness/combat_movement_followup_check.py <output-directory> --sens-x .002 --sens-y .002
python ./scripts/harness/combat_ram_report.py <output-directory>
```

For sensitivity/inversion, use separate temporary launch INI copies via `-InputConfig <private-ini>`, never edit dist/staged configs or source cards. Baseline direct settings are `controls_version=2`, `sensitivity_x/y=.002`, `yaw_sign=pitch_sign=-1`, `invert_x/y=0`. Repeat the same recipe with `.004` sensitivity, then independently `invert_x=1` and `invert_y=1`; preserve all other sections/bindings. The runner copies that INI into the evidence directory and pins `DRAKAN_INPUT_INI` there. The inspected inventory-phase-calibration card hash equals fixture-bow-item (`a9705823...`), so the same pickup/Equipment recipe applies.

The 40 horizontal/-20 vertical counts should yield heading -.08 rad and pitch +.04 rad at baseline; doubled sensitivity doubles these. Invert X reverses heading, invert Y reverses pitch. The checker accepts sensitivity, `--invert-x`, `--invert-y`, and `--camera-only` for `combat_camera_followup.txt`. The raw 9-float heading matrix is retained. Native clamps, turn gates, collision and target controllers can invalidate a case; report a failed or inaccessible case rather than treating callback delivery as an outcome.

Required live evidence:

- Equipment checker must pass all seven native selection/preview/channel assertions (dagger resource1000e through ED0, bow1004f through EAC).
- W and D must change native X/Z position in distinct directions; inspect heading to distinguish lateral displacement from turning. A wall/collision invalidates the fixture case.
- Report arrow counts separately at baseline, active first shot, each release and settled. Rapid press attempts do not guarantee three shots through native cooldown. Require actual ammo reduction and, when captured, projectile records. Audit input press/up samples separately; zero native attack state alone does not establish host release delivery.
- For hostile melee compare the same NPC slot/handle's HP, not unrelated actors or audio. Check ED0 resolves to dagger before classifying equipped melee. Held block/parry requires actual damage mitigation/timing evidence; a block bit alone is not a pass. Report Rynn HP and `damage_reduction_window4000` before/during/after.
- Lock-on requires a real target slot/handle and observed camera/strafe behavior, not merely F/R2 input. Missing target is unverified.
- Quick Inventory must leave native flow inactive after release, with no stale input in the audit; report both equip channels and controller fields.
- Normal script quit and unchanged original/source card hashes required. Missing/unloaded RAM remains unavailable, never zero HP or a fabricated pass.

## Remaining limits

Exact Order of the Flame feel is not established. Native mouse-to-stick flight remains smoothed/rate based and can saturate; no unproven dt rewrite. Arokh flight/attack, magic, successful parry timing and combo progression remain unverified in these followup runs. Equipment's seven checks do not cover clickable category icons, Spells/Skills/Map/Journal: Galileo separately owns category-hit testing in MenuMouse.cpp/.h. No fixed icon coordinates or forced remaps added in this worktree without native hit geometry and live evidence.
