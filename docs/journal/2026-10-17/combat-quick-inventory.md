# Quick Inventory stall: reproduced and fixed

## Cause and fix

The combined stability executable still stalls on keyboard 6 (ACT 0x0e,
command 0x403a). The native Q/R1 path completes the same command. In the stalled
run the watchdog repeatedly reports pc=0x1d86a0, ra=0x1469b0, sp=0x1fbfd30.
This is the completion-flag loop in FUN_001d8660, called by FUN_00146968 during
graphics/image work, after WaitSema/SignalSema. The downstream producer of the
flag has not been independently traced.

The keyboard callback runs synchronously from InputModule::afterPadMgrUpdate
through RecompGuest::call, on a copied EE context with scheduler checkpoints
disabled by ps2xNoPreemptEnter. Quick Inventory requires scheduler progress;
this invocation route cannot provide it. The native PadMgr route can yield.
The earlier mutex-reentry theory was not observed and is not the finding.

`drakan_quick_inventory.h` intercepts only command 0x403a at callback 0x131fa8.
It resolves the command's button from the live controller keymap and overlays
that button on subsequent real scePadRead samples. Quick Inventory consequently
runs through the original PadMgr scheduler path. Press/release and multiple
source actions are tracked. Focus loss, menu ownership, suspension and binding
changes clear the overlay. A scheme without a native binding suppresses the
command with a log message; it never retries the unsafe synchronous path.
Ordinary attacks retain their existing route. Standard scheme resolves R1;
the helper also checks an alternative L1 binding.

The glue additionally provides optional DRAKAN_COMBAT_CALL_TRACE entry/return
and native-overlay tracing. No shared runtime/input or runtime/settings edits
are required. Cached combat audit pad samples precede this wire overlay; use
its transition logs and native command traces to inspect actual delivery.

## Bounded evidence (17 October 2026)

Evidence is under `build/recomp_combat`, outside committed source.

| Run | Result |
| --- | --- |
| quick-merged/t/quick-merged-direct-01 | Combined stability baseline: 6 stalls; no after dump or script quit; bounded runner fails. Watchdog identifies the loop above. |
| quick-merged/t/quick-merged-pad-01 | Same baseline, native Q: exit 0, script quit frame 650, equipped item ED0=0x120d (dagger). |
| t/quick-fixed-direct-01 | Fixed 6: exit 0, script quit frame 649, ED0=0x120d. |
| t/quick-fixed-equipped-01 | Inventory Tab/Enter/E/Tab equips dagger; three mouse swings; held 6 and release; Q reselects dagger; fourth mouse swing. Exit 0, script quit frame 1342. |

Every run used private copies of both memory cards. Recorded user-save hashes
match before/after, including the baseline timeout. No original save was written.
The equipped run's RAM dumps show ED0=0x120d before attacks, ED0=0 after held 6,
and ED0=0x120d after Q. Held 6 overlays R1 at frames 1163..1182, releasing at
1183; both native command DOWN/up are present. Final audit frame 1335 is neutral:
pad=0000, sticks=128, flags94=0, move holds=0, ED0=120d, state1040=0.

Actual dagger swing evidence, beyond callback delivery:

| Mouse DOWN/up frames | Sound manager event | Following nonzero SPU key-on |
| --- | --- | --- |
| 1012/1018 | 45.527s, id 0x301c2, Short Swish 011, instance 0x8000006e | 45.560s c0.v22, SSA159040, vol375a/375b |
| 1072/1078 | 47.527s, id 0x301c2, Short Swish 011, instance 0x80000079 | 47.561s c1.v19, SSA159040, vol375a/375b |
| 1132/1138 | 49.528s, id 0x301c3, Short Swish 02, instance 0x8000007f | 49.562s c1.v22, SSA170840, vol2aeb/2aeb |
| 1302/1308 | 55.195s, id 0x301c5, Short Swish 03, instance 0x8000008a | 55.229s c1.v22, SSA18e840, vol325d/325e |

Sound names come from the original RESOURES.SDB 0x302 descriptors (low 16 bits
of sound ID); successful `[play]` events have ra=0x49a63c. Rynn attack vocal
id=0x20191 also creates instances at 47.562s and 55.230s (SYSTEM.SDB descriptor
Rynn Attack Long 01). Logs and the captured 48kHz stereo `dagger.wav` are retained.
These establish equipped swing events and sound playback, not hit damage or
visual animation correctness. No listening/visual review is claimed.

The standalone bridge diagnostic passes 10/10: ordinary attack exclusion,
native down/up, multiple sources, ownership clear without replay, disabled
press, binding-change clear, no-binding suppression and alternative scheme.
Log: `build/recomp_combat/quick-bridge-check.log`.

## Build provenance and preserved work

Stability 34455b5 was applied to the isolated tools tree as bee2c9e. The inherited
117-line uncommitted combat probe diff was preserved; it is not part of this
fix commit. Its saved patch is `quick-inventory-saved-probes.patch` in the combat
build directory. Earlier input buffering/audit changes are already integrated.

Baseline combined executable SHA256:
`43C7C30123F053E2C93EA85B18DF80E7FD4B2FDD57A9F93AE10F9D68F1C110F2`.
Fixed isolated executable SHA256:
`0e00ca3cf2ec43e09033ce8ac46dfe2204ed25cb246e040d14b28e80baf050c6`.
The combat runtime was rebuilt and linked successfully. To avoid another full
compile, all 83 regenerated C++ files and generated header were compared with
the combined build and found identical; matching Release settings/ABI were
checked before reusing its ps2_generated.lib. The library SHA256 is
`64f7a60e3ee40a0b948e89c6d24287b1827a14230f26b7caf1921cb508c25bdd`.
Receipt: `quick-link-provenance.json`; link log: `quick-link.log`.
Existing LNK4006/LNK4088 warnings remain. No shared build outputs were modified.

A previous competing combat build was stopped on request, selecting only its
verified process tree. `/m` alone did not limit /MP compiler children.
`combat_build.ps1` (now `scripts/harness/build_game.ps1`) defaults to an explicit two-compiler limit via appended
_CL_ /MP2 and CL_MPCount, restoring the caller environment afterward.

## Integration and remaining limits

Tools fix commit: `d70a149eda4936812d294fa6b8e27da243c954a2` (two owned files).

Cherry-pick the new tools fix onto the current tools main (which already has
34455b5 and the earlier audit commit), then the combat docs/scripts commit onto
outer master. Do not cherry-pick bee2c9e or the inherited probe diff. Rebuild
only the changed runtime and link against main's own matching generated output;
no guest regeneration is needed for this glue/header change. Main owns combined
validation and deployment; this worker has not updated dist.

After integration, coordinate a GPU slot and run the narrow checks:

```powershell
./scripts/harness/combat_quick_inventory_run.ps1 -DecompRoot <decomp> -Mode direct -Name quick-integrated-direct
./scripts/harness/combat_quick_inventory_run.ps1 -DecompRoot <decomp> -Mode equipped -Name quick-integrated-equipped
```

Do not run simultaneously with another game. The helper check accepts
`-BridgeHeaderDir <tools-tree>/ps2xRuntime/src/lib -OutputDir <private-build-dir>`.

Remaining unverified: enemy hits/damage, parry timing/held block against an
attacker, combo timing/chaining, strafing/lock-on during an encounter, bow,
magic, alternate-weapon combat and inaccessible Arokh actions. These checks
cover one accessible dagger save and Standard scheme in real gameplay; focus
loss, simultaneous aliases and alternative schemes are helper checks only.
DLSS/NR integration and validation belong to the main worker; none is claimed
by this combat result.
