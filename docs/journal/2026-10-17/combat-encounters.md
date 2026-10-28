# Combat encounter evidence (2026-10-17)

Follow-up: [combat-parry.md](../2026-10-18/combat-parry.md) isolates the native
weapon-parry window on a private observer build. The held attempt opened once,
then expired before incoming hits; successful timed mitigation remains unverified.

This checklist records actual guest state from bounded runs, not callback or audio
coverage. It supplements `docs/reference/combat-controls.md` and `docs/journal/2026-10-17/combat-quick-inventory.md`.
It is not an all-combat pass.

## Baseline and preservation

- Tested executable: copied deployed `drakan.exe`, SHA-256
  `1081C498705511A1CAE4547C1996B42212529A60E128C2479B70EF5BD2733568`.
  These results do not validate subsequent NR/SR/GS integration builds.
- Tools checkout `PS2Recomp-combat` is on `combat-encounters`, based on
  `4897134`. The inherited 117-line probe diff remains uncommitted and excluded
  from this handoff. No full regeneration or compilation was needed.
- Each run copies both cards into a new directory and points `DRAKAN_ELF` there.
  Manifests compare original and source card hashes before and after, including
  failed runs. No original save was edited.
- Seven distinct local save payloads were inspected. They were starting-region
  variants; none established a later quest or mounted Arokh state. Existing saves
  remain the source for further fixtures; a later save is not a prerequisite.
- `combat_save_fixture.py` relocates Rynn and two nearby cameras only. In the
  observed source, 813 of 816 resource payloads remain unchanged. Health,
  inventory, enemy records and quest flags are unchanged. These are relocation
  fixtures, not evidence of natural quest progression.

## Checklist

| Case | Actual evidence | Status / limitation |
| --- | --- | --- |
| Equipped dagger hits an enemy | `encounter-hostile675`: hostile slot 83 HP 40, 37, 29, 25, 25, 23, 18; second hostile also loses HP. Renderer shows two axe-wielding hostiles engaging equipped Rynn. | Damage established for this encounter; no kill or weapon-wide claim. |
| Enemy damages Rynn | Same run: Rynn HP 100, 100, 90, 90, 70, 60, 50, 40. | Actual hostile damage established. |
| Lock-on and strafe | Target slot 83 persists through D strafe. Position changes from (675995,159714,523748) to (676534,159714,523395), about 645 units; heading changes with target. | One target and direction established; cycling, target death and target loss unverified. |
| Block / release / parry | `encounter-block01`: damage reducer 0x314578 receives 10 on six calls, returning 0 twice and 10 four times. Held RMB down f513, release f698; later taps also have matching releases. | Actual defensive damage reduction and native release delivery established. Continuous held protection, exact timed-parry behavior and animation/release completion remain unverified. |
| Attack chaining / special combos | Repeated dagger attacks cause damage. | Exact combo progression / directional special animations **unverified**. Repeated damage is not a combo pass. |
| Bow pickup / equip | `encounter-bow02`: native interaction picks up Short Wood Bow; inventory renderer shows equipped bow and 200/200 arrows. | Established. |
| Bow draw / release / ammunition / projectile | Selected bow ammunition 200 -> 199 -> 198. New type-13 actor with factory 0x41a1e8 moves from (413620.72,160791.73,441259.66) to (411570.69,160613.94,441167.69) across successive dumps. | Two shots and projectile movement established; bow enemy damage unverified. |
| Magic unlock / selection | Native shaman cheat changes ten spell records from level 0 to level 1; Fireball inventory shows level 1, damage 8, mana cost 15%, and flame in Rynn's hand. | Controlled fixture/cheat setup only. |
| Magic cast / mana / projectile | `magic04-trace` closes inventory and consumes mana 100 -> 85 -> 73. `magic05` consumes 100 -> 85 and creates a class 0x10008 spell projectile, factory 0x3971f8, roughly 10,500 units from Rynn. | One cast with mana and actual projectile established. A subsequent magic05 click did not establish a second cast; enemy spell damage unverified. Earlier recipes remain failures. |
| Weapon switching | Dagger and bow equipped through their native inventory interactions. | Full melee/bow/magic switching sequence and rapid switching unverified. |
| Arokh mount / flight / attacks | `encounter-dragon01`: relocation reaches the existing dragon resource area, but Rynn stays controlled, dragon position is unchanged, and renderer shows no visible mountable dragon. | **All Arokh actions unverified**, including melee, breath, selection, takeoff and landing. Existing actor association alone is not a mounted state. |

## Encounter fixtures and evidence locations

Evidence is local and ignored, under
`build/recomp_combat/encounter/t/<run>/`. Each run contains guest RAM dumps,
stdout/runtime logs, renderer captures and card-hash manifests. JSON reports are
generated with the read-only `scripts/harness/combat_ram_report.py`.

- Hostiles: `fixture-hostile675`, Rynn relocated to
  (675929,159648,523349), near hostiles originally at
  (675129,159648,523349) and (677123,159648,523009).
  Run `encounter-hostile675`, script `combat_encounter_melee.txt`.
- Bow: `fixture-bow-item`, Rynn at (425406,160500,441806), beside bow pickup
  at (425126,160500,441806). Run `encounter-bow02`, script
  `combat_encounter_bow_pickup.txt`.
- Friendly NPC fixture at (440401,160403,450978), run `encounter-melee439`,
  is **not** an enemy-damage test. Early exploration and bow01 were discovery
  runs and do not add combat passes.
- `encounter-magic01` exits by script with code 0 but fails the casting check.
  `encounter-magic02` exits on the 100-second deadline without script quit and
  is rejected by the runner. Exit 0 alone never establishes a pass.
- Script `shot` BMPs capture an occluding desktop window in these hidden runs.
  They are invalid gameplay evidence. Use `render*.png` from `GSR_WINSHOT`.

## State interpretation

The ELF and its decompilation establish these observations for SCES_500.06:

- Player stats at 0x4fbac8, key at 0x4fe86c: decode a word as
  `rotr32(raw ^ key, 8)`. Current/max HP are +0x0c/+0x10; mana +0x1c/+0x20.
  Relevant getters/setters include 0x3d56b0, 0x3d56e0, 0x3d57c8, 0x3d5838.
- NPC identity requires data+0x23c equal its owner handle. NPC damage handler
  0x4738e8 uses current HP +0x24c and max +0x250. Do not treat generic visual
  object fields as enemy health.
- Rynn +0xeac/+0xed0 and target +0x4da are registry references, not persistent
  item IDs. Resolve against context+0x7c, stride 8, using (reference-1).
  The registry's second word is not a generation counter.
- Bow item +0x14/+0x18 decode to current/max arrows in this tested case. The
  report labels them quantity candidates for other item types.
- Type 13 alone does not identify an arrow. The observed moving actor's
  factory is 0x41a1e8. Do not count unrelated type-13 objects as casts.

### Spell inventory exit investigation

Native Tab and Escape edges were delivered in magic02 (pad 0x0100 and 0x0820),
but inventory screen 21 persisted. This is not yet a demonstrated glue defect.
Screen handler 0x430d68 forwards keys to 0x4309d8; spell focus handler 0x449b70
returns success. Command 0x4030 invokes 0x345b10, with close guard 0x345fc8.
In magic01 dumps, mode 0x4fbb38 is 0, Rynn+0x104c is -1, and the guard's
controller+0x0c is 0, so those sampled values do not explain a close veto.
Next changed recipe: return to Equipment with Page Up, then close before
casting; inspect the actual command path if still stuck. Do not repeat the same
timeout recipe or force a successful cast by editing mana/projectile state.

## Reuse and integration

From the decomp root, after coordinating an exclusive bounded GPU slot:

```powershell
python tools/combat-notes/scripts/harness/combat_save_fixture.py iso build/recomp_combat/encounter/NEW_FIXTURE 675929 159648 523349
& tools/combat-notes/scripts/harness/combat_encounter_run.ps1 -DecompRoot (Get-Location).Path -SaveRoot build/recomp_combat/encounter/NEW_FIXTURE -Script tools/combat-notes/scripts/harness/recipes/combat_encounter_melee.txt -Name NEW_RUN -TimeoutSeconds 100
python tools/combat-notes/scripts/harness/combat_ram_report.py build/recomp_combat/encounter/t/NEW_RUN
```

Use fresh fixture/run names. The helper refuses existing targets. The runner
disables neural SR for isolation and uses a copied executable/configuration;
it does not validate SR or update dist. Scripts use sync-relative guest frames,
so inspect actual phase transitions rather than assuming fixed wall time.

Cherry-pick only the scoped encounter evidence commit, not the worktree's prior
sync merge. No new production input change is justified by the current failed
magic/block recipes. Main owns integration builds, shared sources and dist.

Remaining focused work: establish continuous held-block semantics and release animation;
identify an actual directional combo animation with damage; investigate repeat
spell acceptance and existing Arokh actor access using private fixtures. Keep every unsuccessful or inaccessible case explicit.

## Offline follow-up: reduction window and existing dragon

Damage handler 0x314ab0 calls 0x314578 before subtracting HP (for its damage
category argument greater than 1). That function returns unchanged damage when
Rynn+0xab0 bit 0x4000 is clear. With that bit set it also checks the attacker
type and facing: normalized direction toward the attacker dotted with the
player's negative forward vector must exceed 0.4. It then applies the resource
property 0x159 percentage, or delegates to shield reduction at 0x3faac8, and
emits event 0x10d3. This is a concrete reduction path, not proof of a successful
timed parry. The actor update sets/clears bit 0x4000 according to animation and
resource timing. All eight hostile675 RAM samples have that bit clear,
including `ram_block_held`. Sparse samples cannot rule out a brief window.
The RAM helper now reports this bit separately from the physical input level.

The existing-save dragon candidate is now resolved through its descriptor:
registry slot 1, handle 0xa5b460, factory 0x270ba0, resource class **0x1001c**.
The game's dragon-mode predicate 0x3d2798 explicitly recognizes that class;
0x3d2658 resolves Rynn's associated dragon registry reference at +0x4d8.
That reference is already 1 in the original on-foot starting save and is **not**
a mounted-state flag. This provides a
stronger identity test than generic object type or vtable alone. The position
remains (737878.75,202773.14,198857.88).

`fixture-dragon-existing` relocates Rynn to (738379,202773,198858) using the
original save, changing only the same three position records; source SHA-256
remains `b1fec620193ff5c04bbd00b4328ea9d69b1b3683a28a91b7a0ec40d0d7eeba95`.
`combat_encounter_dragon.txt` is the bounded mount, takeoff, primary/secondary
and landing attempt used by `encounter-dragon01`. It completed, but did not
establish access or any successful dragon action. The report identifies Rynn by resource class if the
controlled actor changes, avoiding interpretation of dragon data as Rynn.


## Released GPU slot: three focused runs

All three used the same 1081C498 baseline, finished by script quit with exit 0,
and recorded unchanged original and source saves. No main/shared sources or dist
were changed. No game remains running from this batch.

- **encounter-block01**, `combat_encounter_block.txt`, quit frame 858. Rynn HP
  is 100 before/early hold, 90 in later hold/release snapshots, then 80/70/60
  after later short tap attempts. Six traced calls to 0x314578 have incoming
  damage 10, and return **0,10,0,10,10,10**. This is actual defensive damage
  reduction beyond callback delivery. Held input remains pad 0x0020 from f513
  until its release at f698, with neutral pad after release; later taps have
  down/up pairs 758/764, 788/794 and 818/824. Do not interpret two zero-damage
  events as continuous protection from both attackers or proven parry timing.
  Sparse RAM samples still miss the short reduction windows. Input and sound
  logger timestamps have separate clock origins; do not align them directly.
- **encounter-magic03**, `combat_encounter_magic_exit.txt`, returns from Spells
  to Equipment using Page Up and holds native Select for eight frames. It still
  stays screen 21; all three final dumps have mana 100. Some unrelated type-13
  actors also exist before selection, so their presence is not a spell cast.
  The trace budget was consumed by a per-frame close predicate before the key
  close attempts: it does not prove which later handler ran. A subsequent trace
  should omit hot 0x345fc8 and retain only 0x345b10/0x33ca08/0x431d10. No input
  fix is justified without establishing the failing path.
- **encounter-dragon01**, `combat_encounter_dragon.txt`, quit frame 908. The
  renderer shows Rynn in incomplete-looking distant geometry without a visible
  mountable dragon. Her associated dragon reference stays 1, also its value in
  the original starting save. Controlled resource class stays 0x10009 (Rynn),
  and dragon class 0x1001c stays at its original position. The association is
  not a mount pass. The remaining access cause is unresolved; quest/actor activation and region
  loading need tracing. Original quest flags were not changed. All attempted Arokh actions remain
  unverified. The read-only report now labels this field `dragon_ref4d8`.


## Magic cast established; dragon moved into loaded camp

Subsequent runs retain the same executable SHA and private-card protections:

- **encounter-magic04-trace**, using the same exit recipe as magic03, closes
  inventory at native frame 838 and quits at 1000. Trace 0x345b10 first returns
  early on the press path; on release it reaches 0x33ca08 and 0x431d10, which
  returns 1 (close accepted). Mana is 100 before attacks, 85 after the first,
  73 after the later click. This demonstrates that the native close can succeed;
  it does not establish why every earlier attempt failed. Avoid treating the
  inconsistent fixed-frame recipes as proof of a production input defect.
- **encounter-magic05**, `combat_encounter_magic_cast.txt`, delays Select until
  relative frame 420, then waits for gameplay before attacks. Mana 100 -> 85
  accompanies a new type-9, resource-class 0x10008 actor, handle 0x8674a0,
  factory 0x3971f8, data 0x1744c80, position
  (437635.66,171543.14,343450.94), about 10,500 units from Rynn. It is absent
  three guest frames later. Native factory 0x3971f8 initializes through
  0x397650/0x398370. Two nearby type-13 actors from 0x3faed0 in magic04 are
  effects and are not the spell projectile. The reporter now lists
  `spell_projectiles` separately. The later magic05 click has no confirmed
  second projectile; mana recovers to 90. Do not count it as an accepted cast.
- **encounter-dragon02-camp**, quit 898: `--dragon-near-player` moves only the
  existing dragon record to (447650.94,171315.11,344144.19), leaving Rynn at her
  original location. All other 815 resource payloads remain unchanged. The
  renderer now shows the large dragon model in the loaded camp. Rynn remains
  the controlled class 0x10009 throughout; the dragon stays stationary, and no
  mounted or flight action is established. The 30-frame walk also overshoots
  its initial position. A traced 0x32c3f0 gate returns 1, but this belongs to the R3 camera/view
  command path and does not establish L1/E Use acceptance. Mounting remains unresolved.

The dragon retains flags +0x94=9 (including airborne bit 8). Native takeoff
0x26f788 sets that bit, and landing/reset 0x270918 clears it; Rynn collision
handler 0x30aa28 ignores a class-0x1001c actor with bit 8 set. This is a concrete
candidate access condition, not yet a proven mounting root cause.

The additional fixture option `--dragon-landed` requires
`--dragon-near-player` and the exact inspected source-save hash. It clears only
bit 8 in the dragon's saved flags at offset 0x2da (9 -> 1), in addition to its
position, and records that change explicitly. This is a forced landed actor
fixture, not natural quest progression. It does not change health, inventory or
quest flags. `combat_encounter_dragon_mount.txt` uses shorter movement near the
dragon. The subsequent result below still establishes no Arokh pass.


## Per-file report robustness and latest bounded dragon attempt

`combat_ram_report.py` now reports `rynn_available=false` for unavailable context,
registry or Rynn data, without any HP/mana fields. Each requested file is parsed
and validated separately; an unreadable/malformed dump yields `reason=parse_error`
with its error type/message. Earlier and later valid snapshots remain in the
same JSON array. Consumers must inspect availability instead of treating a
successful parser process exit as a gameplay pass.

Focused parser diagnostic used, in order, C2 candidate `ram_begin`, unloaded
`ram_end`, a nonexistent file and `ram_hit1`. It retained HP 100 and 70 for the
two valid files, emitted `actor_registry_unavailable` for the end dump, and
`FileNotFoundError` for the missing file; neither unavailable result has HP.
The complete C2 dump directory also parses without losing valid snapshots.

Read-only evidence from main's `build/t/srnr-object-hostile-01`:
executable SHA-256
`C2CA860C08711088B730514954563CE583DF03C9B77268E45AD6E1032A22F0BD`.
Chronological valid snapshots show:

| Snapshot | Rynn HP | Enemy slot 83 HP | Enemy slot 84 HP |
| --- | ---: | ---: | ---: |
| begin | 100 | 40 | 40 |
| target | 80 | 40 | 40 |
| hit1 | 70 | 36 | 40 |
| hit3 | 40 | 32 | 40 |
| block_held | 10 | 32 | 40 |

`block_up`, `strafe` and `end` have unavailable actor registries and no combat
verdict. This confirms actual damage on that exact candidate, not the success of
its full script or of a subsequent render/present handoff build. Main owns the
combined SR/NR validation and final dist provenance.

**encounter-dragon03-landed** finished with exit 0 and script quit frame 880.
Its private fixture loaded with dragon flags +0x94=1, confirming that the airborne
bit really was cleared. All seven dumps keep controlled resource class 0x10009
(Rynn), and the dragon remains stationary at
(447650.94,171315.11,344144.19). Neither a mount nor a flight/attack transition
was established. Original and fixture-source save hashes remain unchanged.
The airborne bit is therefore **not established as the sole access cause**.
All Arokh controls remain unverified; no production Arokh fix is justified by
these failed access attempts. Further work must identify the native mount/use
conditions rather than equating an associated dragon reference with mounting.

Final combat fix status for this handoff: the previously integrated input-buffer
and Quick Inventory fixes remain in main. These follow-up commits add bounded
encounter evidence and diagnostic/script corrections; they do not introduce a
new production input fix. Magic casting has mana/projectile evidence on baseline
1081C498, while repeat-cast acceptance, spell enemy damage, exact combo progression,
continuous held defense and all Arokh actions retain the limitations above.


## Independent exact handoff candidate check: 0E026E

Read-only follow-up on main's `build/t/srnr-object-handoff-01`; no new live run.
The manifest identifies executable SHA-256
`0E026E07966CC278423350E99474A900602B110D9BEAD0B1D535112E652352EC`,
fixture source `fixture-hostile675`, `DRAKAN_DLSS=quality` and neural rendering
1. This is separate from the earlier 1081C498 combat baseline and C2 candidate.
All eight dumps have available Rynn/NPC data. The existing batch report agrees
with a fresh report. A separate raw-struct check independently decodes HP using
0x4fe86c and verifies each NPC's data+0x23c equals its registry handle before
reading health +0x24c and max +0x250.

| Snapshot | Rynn HP | Enemy slot 83 HP | Enemy slot 84 HP |
| --- | ---: | ---: | ---: |
| begin | 100 | 40 | 40 |
| target | 80 | 40 | 40 |
| hit1 | 80 | 36 | 40 |
| hit3 | 70 | 32 | 40 |
| block_held | 50 | 28 | 40 |
| block_up | 40 | 28 | 38 |
| strafe | 30 | 26 | 38 |
| end | 20 | 26 | 32 |

Enemy slot 83 resolves to handle 0xa5f000/data 0xc2f920; slot 84 to
0xa5f120/data 0xc2e440, each max HP 40. Target reference 83 persists from target
through end. Actual outgoing dagger damage and incoming enemy damage are
established on this exact candidate, and Rynn survives the final snapshot.

RMB down f656 gives pad 0x0020 and a matching up f776 gives pad 0x0000; final
f915 is neutral (mouse/pad/movement holds zero, flags94 zero). Script quit is
f916. This establishes native release delivery and no held input at the end.
HP drops during the two-enemy hold, and all sparse reduction-window samples
are clear: **continuous held protection and successful timed parry are not
established by this run**. Do not transfer the earlier baseline's traced zero
reductions to this executable without matching trace evidence.

Both original and source-save preservation flags are true. SR/NR evaluation,
render/present handoff and independent moving-object pixel evidence remain
main's validation scope; this check independently confirms combat guest state.
Magic/Arokh results remain tied to their earlier baseline until explicitly rerun
on this exact executable.


## Native mount transition reached in corrected ground-state fixture

**encounter-dragon04-ground** uses baseline executable 1081C498, not main's
later camera/present candidates. It finished exit 0, script quit frame 881;
original and fixture-source card hashes remain unchanged. GPU was released
without another launch for main's final F0B84885 matrix.

Offline tracing identified the native mount checker **0x323258**, called by the
on-foot controller at 0x311910 during the native jump/request path. It requires:

- Rynn state flags permit mounting and an associated dragon exists.
- Relevant animation data are not in their guarded bit-9 state.
- **0x27ee20** reports both mount resources loaded (resource state 3).
- Squared distance to the dragon is below 2,097,152 (about 1,448 units), and
  Rynn faces toward it (positive dot with negative forward).
- The dragon's **data+0x9c equals 1**.

The old airborne-only fixture left +0x9c=0, so it was not a complete ground-state
fixture. Native landing function **0x26fa28** sets +0x9c=1 as well as calling
0x270918 to clear airborne bit 8. New explicit `--dragon-ground-state` (requiring
`--dragon-landed --dragon-near-player`) sets saved offset 0x2e2 from 0 to 1.
The observed serialized region 0x2d6 onward matches runtime data+0x90 onward;
the original save hash and expected values are checked before edits. The
receipt lists position, flags and state changes separately; 815 other resource
payloads, health, inventory and quest flags remain unchanged. This remains a
forced actor fixture, **not natural quest progression**.

Actual run evidence:

- Trace: **0x323258 -> 0x323558 -> 0x44d3b0**, on the real guest controller path.
- Before mounting, controlled class is 0x10009 (Rynn), dragon flags94=1/state9c=1.
- By `ram_near_dragon`, controlled class becomes **0x1001c (Arokh)**. It remains
  Arokh in mount/flight/primary/secondary/final dumps; dragon flags94=0x20,
  state9c=1. The reporter still resolves Rynn independently for player stats.
- `render_2200.png` shows Rynn riding on the dragon from behind. Native mount
  initiation/control ownership and a rider view are established in this fixture.
- Dragon position stays (447650.94,171315.11,344144.19) in every dump. No takeoff,
  forward flight, landing transition or dragon attack/projectile is established.
  A control swap or rider image alone is not a flight/attack pass. Camera audit
  still reports dragon=0, and the unchanged flags/position need further tracing;
  this run does not isolate a production input defect or justify a forced fix.

The earlier all-actions-unverified statements are superseded **only for native
mount initiation in this forced fixture**. Completed mount animation/recovery,
flight steering, takeoff/landing, breath/selection, target cycling and attack
release remain unverified. The fixture generator and read-only reporter are the
only code changes in this follow-up; no shared runtime, main source or dist edits.

Control-path correction: L1/E Use is command 0x4009 -> 0x3168a0. Command 0x400a
and gate 0x32c3f0 concern R3/V camera/view mode; earlier gate observations must
not be treated as E/Use validation.

Audio evidence correction: main's initially zero-length in-progress WAV later
flushed to a valid nonzero finished capture. No audio regression is inferred
from the temporary pre-flush file length; no listening review is claimed here.


## Independent final high-resolution candidate: F0B84885

After completion of main's `build/t/srnr-final-highres`, independently parsed all
eight RAM files and cross-checked begin/hit1/end directly with raw struct reads,
HP decoding and NPC owner-handle identity checks. Exact executable SHA-256:
`F0B84885ACBE981D25A351076FBF9B5A6B6F81E022C79C72FEF81922848EA3D1`.
Receipt confirms the hostile675 fixture, quality SR, neural rendering 1, and
unchanged original and source-save hashes. No new live game was launched here.

| Snapshot | Rynn HP | Enemy 83 HP | Enemy 84 HP |
| --- | ---: | ---: | ---: |
| begin | 100 | 40 | 40 |
| target | 80 | 40 | 40 |
| hit1 | 80 | 36 | 40 |
| hit3 | 70 | 32 | 40 |
| block_held | 40 | 32 | 40 |
| block_up | 20 | 30 | 40 |
| strafe | 10 | 28 | 40 |
| end | **0** | 24 | 38 |

All eight snapshots have available actor data. In particular, end HP 0 is a
**valid death snapshot**, not unavailable/unloaded memory and not an alive-Rynn
pass. Native target 83 persists through strafe, then clears by end. Actual
outgoing dagger damage and incoming enemy damage are established on this exact
executable. Survival is not a criterion met by this run; no input or rendering
regression is inferred from death during an unprotected two-enemy encounter.

RMB down/up f656/f776 gives pad 0x0020/0x0000. Final f915 is neutral for mouse,
pad and movement holds, with flags94=2 (death state), and script quits at f916.
Release delivery is established. Sparse reduction-window samples are clear;
**successful timed parry and continuous held protection remain unverified on
this candidate**. Normal harness quit does not turn every combat action into a
pass. Bow/magic/toggle and render/present checks are main's separate final runs.


## Production dragon-context fix and separate focused diagnostic

Tools commit **736d7fb** changes only `drakan_input_glue.cpp` and a private inline
`drakan_controlled_actor.h`. It introduces no shared input/runtime settings,
public ABI/header changes or regenerated guest code. The inherited 117-line
probe was excluded from staging and restored unchanged after the commit.

Proven defect: `g_dragon` was set only by optional Camera_UpdateLook callbacks,
and recent callback age alone selected gameplay. Native dragon controllers
A/B call Player_CameraLookTick only in camera mode 6; normal controlled-dragon
mode 1 therefore cannot refresh that hint. Dragon04 RAM proves the current
resource class is 0x1001c while old audit remains dragon=0 and eventually uses
menu bindings. This can route Space to menu Cross instead of gameplay Triangle
and retain an on-foot context despite native ownership changing.

The fix resolves current context+0x4b0 -> resource class each PadMgr tick using
the same inclusive descriptor-index check as native 0x3d2798. The presence of an
associated dragon on Rynn does not select dragon mode. For live controlled
dragon data, valid camera modes 1/6, native control flag 0x4fb730 and unscripted
flow 0x4fbafc=0 permit gameplay without a recent look call. Existing GUI-screen
and force-mode overrides still run afterward. Existing focus/overlay suspension
neutralization and native game command gates remain in effect. The existing
input module receives its correct dragon flag; no button mapping is rewritten.
Camera callbacks retain their own native look arguments, but no longer overwrite
the authoritative PadMgr role cache.

Separate CPU diagnostic `combat_dragon_context_check.ps1/.cpp` passes **24
checks**: descriptor limits/sentinel/bounds/aliases, unload and actor changes,
scripted/control/camera gates, on-foot strafe preserved, native flight axis
restored, Space/Triangle versus menu Cross, menu ownership, settings-overlay
neutralization and focus loss. Three actual dragon04 RAM files independently
resolve before mount as dragon=0 and both after-mount samples as dragon=1 with
fallback gameplay=1. Log: `build/recomp_combat/dragon-context-check/results.log`.
MSVC emits pre-existing fopen deprecation warnings from input sources.

This validates CPU context selection, not completed flight or the newly linked
runtime. Main must incrementally build and rerun combined rendering and the
mount fixture before deployment; no full generated rebuild is required by this
private C++ glue/header change. Existing fixture:
`build/recomp_combat/encounter/fixture-dragon-ground-ready`; recipe
`combat_encounter_dragon_mount.txt`. No game was launched during main's matrix.

Final F0B8 magic read-only follow-up: selected spell index at Rynn+0x106c is 0,
its vector record is level 1 with descriptor index 0xa09, resolving to resource
ID 0x100d6. Mana is 100 -> 85 -> 75; press/up pairs f991/997 and f1061/1067 reach
native pad Square/up. No spell-projectile actor is present in the late relative
35/38 dumps. The owned cast recipe now additionally dumps relative **27/28**
and **97/98**, immediately after each release, to catch short-lived spawns.
Main may enable `DRAKAN_TRACE_FN=3971f8` with `DRAKAN_TRACE_N=200` for constructor
entry proof (a1=1 denotes creation); this is existing opt-in tracing, no runtime
edit. Mana consumption alone does not establish a projectile or enemy hit.


## Live context-fix verification: 98C2D216

Independent read-only analysis after `build/t/srnr-final-dragon-context` completed.
Exact executable SHA-256:
`98C2D216C5F331674111B945CD0EE658D15D13870682E92B714C9D8D9C7DFB1B`.
The run uses the forced ground-ready existing-save fixture with SR/NR enabled,
finishes with normal script quit f880, and preserves both original and source
cards. No further production edits follow from this analysis.

- **Context and mount ownership established:** start is controlled class 0x10009;
  all six later dumps are 0x1001c. Audit dragon=1 persists from f509, with
  gameplay/capture active even though Camera_UpdateLook calls stop at 16. This
  is live evidence that 736d7fb fixes the stale context/menu fallback.
- **No actual flight movement established:** all seven dragon positions are
  exactly (447650.9375,171315.109375,344144.1875). The heading matrix is also
  unchanged: (-0.8660,0,0.5000; 0,1,0; -0.5000,0,-0.8660). State +0x9c stays 1;
  flags94 changes from 1 before mounting to 0x20 and stays there. The native
  mount transition and rider view are distinct from completed flight readiness.
- **Takeoff/landing input delivery only:** takeoff Triangle is pad 0x0010 at
  f620..649, released at f650. Landing/crouch Cross reaches pad 0x0040 in its
  later hold. Neither establishes a state/position transition. Takeoff, landing
  and flight steering remain **unverified**.
- **Primary/secondary delivery and release established, attack result not:**
  primary f690/f720 produces pad 0x0080/up and flags90 0x40202 -> 0x40200;
  secondary f740/f770 produces pad 0x0020/up and flags90 0x40220 -> 0x40200.
  These are native request bits, not evidence of breath/melee animation, damage
  or projectile creation. All sampled flags94/state9c/position remain unchanged.
  Dragon attacks, selection, target cycling and attack animation completion
  remain **unverified**. No production defect beyond corrected context is proven.

Use actor-handle RAM coordinates for this verdict. `[in-pos]` still refers to
`g_player`, the last camera-look target (Rynn here), so it cannot measure Arokh
flight. Likewise audit itemEAC/itemED0/state1040 are Rynn-layout fields and have
no established meaning for dragon data; their nonzero values are not equipment
or spell evidence. The RAM reporter resolves Rynn and dragon actors separately.

The forced fixture relocates the original dragon and changes its airborne flag
and state9c only. It does not establish natural quest activation or a complete
native landing/mount-animation lifecycle. Unchanged state 0x20 is an observation,
not a diagnosed scheduler/input bug. Further testing should trace native ready/
animation conditions before forcing additional state or changing production code.

Scripted mouse support already exists: `FRAME mouse DX DY FRAMES` injects DX/DY
**on each frame**, not divided over the interval (Script.cpp's repetition loop).
For example `180 mouse 12 -8 10` supplies ten samples totaling (120,-80). A
future bounded steering recipe should capture before/after dragon heading,
position and synthesized right-stick samples, and require actual changes before
claiming steering. The present recipe contains no mouse motion, so it cannot
validate mouse steering. No follow-up game was launched while main owned GPU.


## Final exact-executable magic verdict: 98C2 release run

Independent analysis after `build/t/srnr-release-magic` completed, manifest SHA:
`98C2D216C5F331674111B945CD0EE658D15D13870682E92B714C9D8D9C7DFB1B`.
All 13 RAM files parse with available Rynn data; original/source saves unchanged.
Script quit f1110. This is separate from the earlier F0B8 magic test.

Selected spell is independently resolved using the native 0x38f518 layout:
Rynn+0x106c selects index **0**, level **1**, descriptor index **0xa09**, resource
ID **0x100d6**. Native attack down/up pairs are f990/996 and f1060/1066, with
matching Square/up pad samples and request-bit release.

| Snapshot group | Mana | Spell projectile present in RAM |
| --- | ---: | --- |
| ready | 100 | no |
| launch21/22/27/28 and relative35 | 100 | no |
| relative38 (moving) | **85** | no |
| launch91/92 | 87 | no |
| launch97/98 and after | 88 | no |

Despite missing its live interval in every snapshot, the existing opt-in
**0x3971f8 factory trace establishes one actual projectile creation**:
`ra=0x283910, a0=0x867380, a1=1`, then return `v0=0` (native successful
initialization). The same object receives **ten event-8 updates and nine event-4
ticks** through this native factory. The recorded creation is adjacent to
`[play 60.685]` and presentation 3245; input/sound clocks have separate origins,
so their timestamps must not be directly equated. The 200-call trace budget is
not exhausted (20 entries total). The object slot is reused by other types in
RAM snapshots; its later contents are not evidence of a spell actor or a defect.

**Verdict:** one spell cast, mana consumption and projectile construction/native
updates are established on the exact 98C2 executable. A measured trajectory,
visible projectile review and enemy spell damage remain unverified because the
snapshots do not capture the actor alive. No second creation appears in the
trace and the later mana values recover, so the second click is **not** counted
as a second successful cast. The cause of repeat-cast acceptance is not isolated;
no new production defect or runtime change is asserted. No game was launched
for this read-only check; main retained exclusive GPU ownership.
