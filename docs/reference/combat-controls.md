# Combat input investigation — 17 October 2026

Follow-up: [combat-encounters.md](../journal/2026-10-17/combat-encounters.md) records actual hostile
damage, lock-on/strafe and bow projectile/ammunition checks on the later deployed
baseline, together with the remaining unverified cases. The original delivery
diagnostics below retain their original evidence limits.

Scope: SCES_500.06, mouse/keyboard on foot. Continue the saved `drakan-combat`
worktree without committing its inherited command/controller probes. The shared
checkout and dist are not integration targets during this investigation.
No AGENTS.md or CLAUDE.md was found in the repository or combat worktree; relevant
references are `docs/reference/input.md`, `docs/reference/mouse-menus.md`, `runtime/input/README.md`,
the existing `scripts/combat_{build,run}.sh`, and `build/combat_tests/c1..c6,x0..x7.txt`.

## Findings and evidence levels

| Action | Established | Still requires gameplay evidence |
|---|---|---|
| Attack / chaining | Standard Square → `0x4006`. `Game_OnInputCommand` calls player vtable `+0x164` with 1 on press and 0 on release. Existing `c1.in.log` shows matching edges at frames 569/570 and 629/630. | A handled-command return of 1 does not prove an attack. Equipped melee animation, combo acceptance window, damage and direction-dependent chains are unverified. Do not add artificial autorepeat or hold-to-chain based on these logs. |
| Held RMB / parry | Standard Circle → `0x4007`. The handler sets `player+0x90 & 0x20` on press and clears it on release; item/magic branches may clear it immediately. Release falls through to the primary attack release call. | A held input flag does not establish a shield block or a successful timed parry. Test with a melee weapon and an enemy attacking; separately test casting with a selected spell. Simultaneous LMB/RMB release ordering needs equipped combat. |
| Strafing | Controls v2 remaps left-stick X to axis 3 on foot. `docs/reference/input.md` section 12 contains prior visual evidence. The saved logs identify controller C (`0x311220`) running on foot. | New checks here can establish movement and releases, but cannot establish enemy-facing behavior without a valid target. A/B are not identified as Arokh solely from this observation. |
| Lock-on | F/MMB → R2 → `0x400b`; the handler requests a toggle of player flag bit 14 through `0x45ef50`. R → L2 → `0x400c` holds bit `0x100000`; outside targeting it enables aim-axis mode while held, and inside targeting it also sets `0x8000`. | Cycling a real target, persistence through switching weapons, target loss/death and mouse turn gating need a live target. `PadMgr+0x4d4` is the stick position/rate flag, **not a lock-on state** despite the inherited probe label `aimMode`. |
| Bow / magic | Attack release and selected-item IDs (`+0xeac`, `+0xed0`) can be observed. RMB is the original contextual Magic/Parry action. | Bow draw/release, projectile direction, spell casting, mana use and switching between equipped melee/bow/magic remain unverified without the equipment. Do not promise a dedicated bow or spell hotkey. |
| Wheel / weapons | Shipped keymap actions `0x114/0x115` resolve to `0x4034/0x4035`. Their handlers call `0x270818/0x2707d8` on a context-dependent object. | These are not proof of universal previous/next **equipped** weapon behavior. Inspect item IDs and visible equipment with multiple items. |
| Number keys | **Quick slots (2026-10-25).** 1..5 are bound to `QUICK1..QUICK5`: key N uses / equips the Nth item of the native Quick Inventory (column-major grid order, the bar's list at `ctl+0x214`) through the native bar: the driver sets the bar index, overlays the scheme's R1 (`0x403a`, `23cae0`) and then Square (`0x4006`, the bar's confirm: `33eaf8` + `33f630`). Verified live: bow and club drawn (EAC / ED0), the Health Elixir drunk (HP 84 -> 100). The old `ACT:0x02..0x06` (commands `0x403b..0x403f`, no case in `Game_OnInputCommand`) migrate to `QUICK1..5`; 6 stays `ACT:0x0e` = Quick Inventory. See [quick-slot-hotkeys.md](../journal/2026-10-25/quick-slot-hotkeys.md). | Swimming, climbing, summons and cut scenes are refused natively (decompile / unit tests only); other controller schemes, a key during a live combo and the modern bow aim with a key press are not run. |
| Arokh | Existing controls select a stick path when the camera reports dragon mode. | **UNVERIFIED:** mounting, takeoff/landing, flight steering, breath attacks, breath selection, dragon target cycling and attack release. The accessible starting save does not establish any of these. |

Static evidence comes from local `analysis/input/0023ce18_Game_OnInputCommand.c`,
`001e40f0_PadMgr_DispatchActionEvents.c`, `00457390_Input_SetAimAxisMode.c`,
`00311220_Controller_Update_C.c`, and `tables.txt`. Decompiled game code and RAM
dumps are local research artifacts and are not included in commits.

## Concrete delivery faults and isolated proposal

The shared input files are untouched. The `combat-investigation` worktree proposes:

* `FrameInput.h/.cpp`: retain a FIFO of transitions per physical key/mouse button.
  Consume at most one transition per source per game frame, giving every queued
  click a down sample and a separate up sample. A release/repress while a button
  was already held previously produced an unchanged pad mask and no new attack.
  Three complete clicks between ticks previously collapsed into one click.
* Focus/context loss cancels the FIFO and clears stale presses, wheel and motion;
  a fresh press after that reset follows a release sample. Keyboard repeat does
  not enqueue additional downs while the physical level is already down.
* `DrakanInput.h/.cpp`: cancel delayed raw edges when menu/suspension takes input;
  neutralize an unfocused/suspended snapshot, including XInput. Send held ACT/CMD
  releases through the guest callback even while suspended. Previously the
  suspension path discarded `pending_` before dispatch and left the guest held.
  Check suspension again in the direct mouse-look path.
* `PadSynth.cpp`: send one ACT/CMD press/release pair per wheel notch instead of
  collapsing a multi-notch frame into one pair.

The FIFO is input delivery buffering, not a combat animation queue: the guest can
still reject an attack during an animation or scripted sequence. At low game FPS,
a large burst takes additional frames to drain. Context changes intentionally
cancel old clicks so they cannot activate a new menu. Per-source ordering is
preserved; ordering between different physical sources in one frame is not.
Custom ACT/CMD aliases that bind several sources to the same held command still
need an aggregate ownership design; this proposal does not solve that case.

Focused diagnostic: `scripts/harness/combat_input_diagnostic.ps1` compiles a small driver
against the selected source tree; it does not run the repository test suite.
Baseline shared sources failed 7 of the initial 8 checks (burst count 1 down/1 up
instead of 3/3). The candidate passed those 8 checks. A follow-up adds the fresh
press-after-reset case and strengthens the unfocused pad check with a held
keyboard pad binding and a connected gamepad. See the final run evidence below.

## Combat glue audit

`tools/PS2Recomp-combat/ps2xRuntime/src/lib/drakan_combat_audit.h` and its two-line
glue integration are independent of the inherited probes. Set
`DRAKAN_COMBAT_AUDIT=15` to record input edges and an idle sample every 15 frames.
It reports screen/dragon/focus/capture/suspension, mouse edges, the actual cached
pad bytes, the current context's player, attack vtable function address, movement
hold flags and raw selected-item IDs. Pointer chains are checked against EE RAM;
the audit neither writes guest state nor forces combat actions. It is off by default.
Samples occur after PadMgr and before the next controller update; an input edge
and its later animation state need not appear in the same record.

The older saved `DRAKAN_CMD_LOG=1` probe additionally logs commands, return values
and controller identity. Its generic entity enumeration is not used by these
checks: some handles resolve to small IDs rather than object pointers. Do not
interpret their `(no body)` lines as evidence that an enemy is absent.

## Reproduce and integrate

1. Read the final evidence/limitations below before applying the proposal.
2. Review/cherry-pick the parent repository's `combat-investigation` commit into
   the integrating branch. It contains only the isolated input proposal, this
   document and combat diagnostic scripts. Resolve any later input-owner changes
   manually; do not copy the entire runtime tree over concurrent work.
3. Separately review/cherry-pick the audit commit from `tools/PS2Recomp-combat`
   (`0a9e2c934b47b9e47c8a7c265eae0624e4c88ac4`) into the runtime tools branch. The inherited uncommitted probe diff is left
   uncommitted and is not a dependency of the new audit.
4. For an isolated build, configure the existing combat cache with
   `-DPS2X_INPUT_DIR="C:/Games/Drakan - The Ancients Gates/decomp/tools/combat-notes/runtime/input"` (historical: that cache variable no longer exists; the single CMake root links `drakan::input`, so point a worktree at its own checkout instead).
   The combat cache was pointed there for this investigation. Return it to the
   integrating input tree when ready. Other build caches are unaffected.
5. Build using `scripts/harness/build_game.ps1 -BuildDir <decomp>/build/recomp_combat`.
   It checks the actual CMake exit code and defaults to two build workers. The
   inherited shell build wrapper can falsely print BUILD OK after other failures.
6. Run the focused diagnostic using `scripts/harness/combat_input_diagnostic.ps1`;
   `-SourceRoot <tree>` selects baseline or candidate sources. Run gameplay using
   `scripts/harness/combat_diagnostic_run.ps1 -BinDir <combat>/bin -Elf <decomp>/iso/SCES_500.06`.
   It creates a unique evidence directory, restores environment values, enforces
   a deadline and requires both a successful exit and `script quit`. It only
   terminates the process it started. No automatic retries or save writes.
7. `combat_sequence.txt` covers rapid/held LMB, held RMB plus D, A/D releases,
   targeting toggles/hold, multi-notch wheel, keys 1–5, pause/resume and
   post-menu attack/release. Scripted input bypasses Raw Input accumulation;
   use the focused driver for within-frame clicks. Gameplay requires a usable
   existing save and successful intro/loading, so timeout is a failure, not a pass.
   `combat_quick_inventory.txt` isolates 6 / Quick Inventory after the first run
   stopped advancing there. The runner saves its exact script/config and an exe
   SHA256 manifest so a later edit cannot silently change reproduction steps.
8. Validate equipped melee/bow/spells and a live target before promoting combat
   claims or updating dist. No dist update is part of this work.

## Final run evidence

* Focused driver: **9/9 PASS**, MSVC C++20. Output:
  `build/combat_diagnostic_candidate/result.log`. Burst count: 3 downs/3 ups,
  final released. The original baseline executable failed 7 of the original
  8 checks; the ninth check was added after that comparison. No full repository
  test suite was run.
* Build: both the first build and the final incremental build succeeded. The
  latter receipt is `build/recomp_combat/combat-build.log`. The executable SHA256
  is `B0C93569064C363E4DF9E7D714CE800736F44EA36B6A5E645BC57C42AD3A51A7`.
  (That build predates strict linking: it printed duplicate `ShowCursor` (LNK4006)
  and `/FORCE` output (LNK4088). Both are fixed; `drakan.exe` now links without
  `/FORCE:MULTIPLE` and raylib's `ShowCursor` is renamed by a compile definition.)
  This binary incorporates concurrent shared GS/settings
  sources; success does not validate those other workers' changes.
* **Failed run**, `build/recomp_combat/t/combat-resume-01/input.log`: loaded the
  save, ran the early combat actions, then stopped advancing at frame 954,
  t=50.867 immediately after `0x403a` / ACT `0x0e` (6 / Quick Inventory) DOWN.
  It reached the time limit without `script quit`, and the runner rejected it.
  The runtime reported that its game thread did not stop within 3 seconds.
  This is an unresolved stall, not evidence of functional Quick Inventory or
  its root cause. That run's exact original sequence is saved beside its log.
* **Completed run**, `build/recomp_combat/t/combat-resume-02/input.log`: same
  sequence with 6 isolated out; `script quit` at frame **1099**. Saved script,
  input config, exe SHA256/environment manifest, and `summary.json` are beside
  the log. Pause opened at frame 970 (`-1 -> 23`), resume at 1000 (`23 -> -1`).
* In the completed run: primary attack had 6 downs/7 ups; RMB had 2 downs/3 ups.
  The extra releases are the game's resume cleanup at frame 1000 (attack action
  `0x120`, Circle action `0x125`), not lost downs. The scripted Square action
  `0x124` had all six corresponding releases, including the 45-frame hold at
  619–664 and the post-resume tap at 1029–1030. R/L2 and both A/D directions had
  matching press/release pairs. Each three-notch wheel event produced three
  press/release pairs at frames 874 and 889. F reached the targeting command,
  but the targeting flag did not change in this target-free save.
* Last audit (frame 1095): pad `0000`, all sticks `128`, movement holds both
  `00000000`, flags90 `00062000`, selected-item IDs both zero, state1040 zero.
  The trace summary reports no held combat `(command, action)` pairs at end.
  Run `python scripts/harness/combat_trace_summary.py <input.log>` to reproduce that
  delivery summary; it does not claim hits, combos, parries or working targets.
* The current player resolves to `0x00be4680`, with attack vtable function
  **0x307198**. Inspecting local generated `ps2_recomp_0044.cpp` confirms that
  this function sets/clears attack request bit 2 at `player+0x90`; its release
  branch calls `0x3207c0`. The request can be consumed while LMB remains down,
  so a held pad bit does not establish automatic attack chaining.
* RMB pad `0020` stayed held until its scripted release, while the guest's
  contextual `0x20` flag cleared earlier without an equipped item. This is
  evidence of input delivery, **not a verified held block or parry**.
* Keys 1–5 reached `0x403b..0x403f`, each with return 0 and no selected-item
  change. Weapon switching, bow draw/release, magic casting, a live target,
  animation timing and damage remain **UNVERIFIED**. All Arokh actions listed
  above remain **UNVERIFIED**. Current gameplay used scripted snapshots; the
  within-frame driver exercised the real accumulator/synthesizer methods, but
  this round did not independently validate Windows Raw Input event arrival.

The new tools commit contains 44 inserted lines in two files. Its inherited
117-line glue probe diff remains uncommitted. The isolated parent proposal
changes five input files plus this combat document and combat-only scripts.
Shared `runtime/input`, shared `runtime/settings`, other agents' changes and
dist were not edited or committed by this investigation.

## Dual wielding

`[general] dual_wield` (Gameplay > Combat > Dual wielding, env `DRAKAN_DUAL_WIELD`, default off;
`ps2recomp/ps2xRuntime/src/lib/drakan_dual_wield.cpp`). With it on, using a second one-handed weapon while Rynn
holds one puts it in her left hand. Investigation and live runs:
[dual-wield.md](../journal/2026-10-25/dual-wield.md).
Turning it off stows the off-hand weapon through the native inventory path on the next game update (when play
resumes if Settings paused it). The weapon stays in inventory. This also clears a reserved off-hand while the bow
is drawn, without detaching the bow; enabling again does not automatically equip the stored weapon.

Native model it builds on (static, SCES_500.06):

| Piece | Where | What |
| --- | --- | --- |
| Equipment slots | `player+0xa48 + n*0x10` | 0 right hand, 1 bow hand, 2 shield, 3 armour, 4 `0x100c7`. Item -> slot: controller vtable +0x24 = `0x344cb8` (class `0x1000e`/`0x10095`/type 11 -> 0, `0x1004f`/`0x10026` -> 1, `0x10090` -> 2, `0x1002d` -> 3, `0x100c7` -> 4). Equip message `0x1045` -> `0x31e460` -> `0x31f2f8` (slot table, world) -> `0x31ca20(player, slot mask, 1)` (attach). |
| Hand holders | `player+0xe90` (left: bow), `player+0xeb4` (right: melee), 0x24 bytes | `+0` mode (melee: item property `0x1e`, the stance; 5 bow, 4 crossbow, 3 usable), `+0xc` bone, `+0x10` hit-window list per animation (`0x4970c8` fills it in `0x30fb28`), `+0x1c` u16 weapon ref (= the `ed0`/`eac` fields). Attach `0x49cda0`, detach `0x49d028`. |
| Bones | Rynn's model properties | `0x45` right hand (37), `0x46` left hand (19), `0x51` shield (32), `0x24` slot 4. Attach `*0x4fd57c = 0x13b878(model, item, bone, 0, 0)`, detach `*0x4fd580 = 0x13b8c8(model, bone)`; **one item per bone** (the skeleton's `vt+0x34` replaces). |
| Blade | weapon data object | `+0x1c` bit 3 live, bit 4 hit this window; `+0x60` owner ref (skipped by the sweep, credited with the hit); `+0x194` damage multiplier; `+0x198/+0x1a0` targets already hit. `0x49a538(weapon, live)` switches it, `0x49c590(holder, live, reset)` per hand, `0x49c3e0(time, holder)` each frame from `0x303ff8` picks the window. `0x4979a8` sweeps the blade, `0x498020` applies `prop(0x1a) * +0x194`. |

The port: the second weapon goes to slot 2 (only while the right hand holds a different stance-0 melee weapon, slot 2
is empty and the bow is not drawn; the slot-2 item always maps back to 2 so it can be unequipped). Every native
reader of slot 2 checks class `0x10090`, so the weapon is equipped, flagged and unequipped like a shield and otherwise
ignored. After `0x31ca20` handles masks 2, 4 or 8 the hook attaches it to bone 19 with Rynn as owner (or detaches a stale melee
item no holder owns). The `0x49c590` hook routes the right holder's hit window to the striking hand. Blocking helpers
are tail-called on the real context; `0x31ca20`'s `$v0` comes back through a host trampoline at an empty
function-table slot (`0x100000`).

The hand rule lives in `runtime/gameui/include/gameui/DualWield.h`, shared by the hooks and the inventory screens.

### Inventory

* Native Tab screen: the details builder `0x342f90` (controller vtable +0x2c) is post-hooked; for melee weapons the
  runtime writes a hand line into the range line `ctl+0x384`, which the panel (`0x433850`) draws under Speed outside
  shops: "Equips to left hand", "Equips to right hand", "In left hand", "In right hand". The line is refreshed after
  every attach pass (an equip does not rebuild the details), and a native line (a bow's range) is never overwritten.
* Modern Tab screen / I strip (`gameui`): the Reader records each melee weapon's hand (`ItemEntry::hand` from slots 0
  / 2) and stance; Confirm reads "Equip Left Hand", "Equip Right Hand" or "Unequip Left Hand", equipped weapons are
  marked R / L, the detail line reads "Equipped, Left hand", the Equipped strip shows Right hand / Left hand / Bow /
  Armour and the Character page captions the weapon card "Right hand · Left hand: ...". The runtime's native hand
  lines are dropped from the modern details. A left-hand candidate is compared with the left-hand weapon.

### Left-handed strikes (pose mirror)

The game has no left-handed attack animations; every second strike plays the same clip mirrored left <-> right
(`drakan_pose_mirror.h`). Pose pipeline (verified against RAM dumps and the generated code):

| Piece | Where | What |
| --- | --- | --- |
| Pose update | `0x103268(sk, force)` | `0x10a340` clips, `0x104678` hierarchy walk (slerp `0x19e198`, quat -> matrix `0x19e7d8`, compose `0x13b998`), `0x117cd0` IK, then `sk+8 |= 0x100000` and **`0x1165b0(sk)` skinning** (returns to `0x103890`). `DAT_004da734` = the model resource during the update. |
| Pose | `sk+0x10c`, count `+0x114` (65), stride 0x6c | `{position[3], rotation[3][3]}` model space (row vectors, `x * R + p`), `+0x4c` local quaternion, `+0x68` bind index. Rebuilt from the clips every update. |
| Bind | `*(res+0xc8)`, 0x40 per node | inverse bind (same affine), `+0x38` first child, `+0x3c` next sibling. Symmetric about x = 0 (left -x). |
| Skeleton | Rynn | 15-19 left clavicle / upper arm / forearm / hand / grip, 20-31 left fingers, 32 shield bone (child of 17); 33-37 / 38-49 right; legs 54-58 right, 60-64 left; the rest on the centre plane. |
| Skinning | `0x1165b0` | palette `IB[k] * M` in a scratchpad allocation (only alive inside the call), VU0 macro code on the CPU, collision joints to `sk+0x128`. |
| Attachments | `sk+0xf4`, 0x6c each | `+0xc` = `+0x3c` (local) * node matrix, written by `0x10b100` during the walk (attachment 0 node 37, 1 node 19). Channels (`sk+0xe8`, 0x80) with `+0x18` != 0 export their node matrix (ch5 head, ch7 right grip -> `player+0x54c`, ch8 spine). |

The hook pre-runs `0x1165b0` when called from the pose update for Rynn's skeleton: `M'[i] = B[k(i)] S IB[k(m)] M[m] S`
(S = diag(-1, 1, 1), m the partner node) makes every skin palette entry the mirror of its partner's, rotations stay
proper; then the attachments and the exports are recomputed so the weapons and the hand position follow. Strikes: a
strike starts when `player+0x94 & 0x80` (attacking) rises, the spine attack clip / combo step (`player+0xb1c`) changes or the
clip restarts; strikes alternate right / left while attacking and an attack after 1 s without attacking starts on the
right. Locked on, the game starts an attack, drops the flag for a frame and restarts the clip: a restart of the same
clip and step within its first 0.1 s of clip time is the same strike. The mirror blends in and out over 0.12 s
(quaternion nlerp). Only with a left-hand weapon equipped and attached.

Moving attacks use different root/leg and spine/arm clips. In that case only the upper body is mirrored, around
the native pelvis; the root, pelvis and legs remain native at every blend weight (`applySplit`). The root-motion
hook also checks the current channels and leaves locomotion unchanged. Using the root clip to track strikes or
mirroring its sideways delta had caused strafe direction changes and incorrect leg motion during left attacks.

* Blades: the native window (`0x49c590` on the right hand holder) goes to the striking hand. During a left-handed
  strike the right weapon's request is withheld before the native call and the left blade takes the window; during a
  right-handed strike the left blade stays dead.
* Root motion: `0x1198d0` moves the actor by the clip's root delta through `0x17ea70(model, world delta, 0)` (returns
  `0x11a32c` / `0x11a424`), inside the pose walk. A pre-hook converts the world delta to model space with the actor
  matrix (`model+0x20`, rows, world = M * d), scales the sideways part by 1 - 2w and converts back, so a mirrored
  stationary strike lunges to the mirrored side (the weight is the previous update's while it ramps). Separate
  locomotion clips use weight zero immediately, including the frame strafing starts.
* Weapon types: the disc's weapon schema has "Weapon Type = One Handed, Two Handed, Hammer/Axe" with separate sheathe /
  attack animation sets per type, in the order of the native table `0x4fc108` (24 rows `{type 0, type 1, type 2}`
  action), so property `0x1e` 0 is one-handed and only those go to the left hand. Disc item names include "Two Handed
  Sword", "Steel Two Handed Sword", "Mithril Two Handed Sword"; every melee weapon in the 2566 local RAM dumps is type 0.
* DLSS: object motion vectors come from projected-vertex correspondence between frames (`dlss.md`), and the mirror
  blends over 0.12 s, so switching is ordinary vertex motion, not a discontinuity.

Verified live (magic5 card, Wood Club + Rynn's Dagger): slot routing, both cells flagged equipped, dagger on bone 19
with owner ref = Rynn's model, unequip (slot cleared, bone detached), the hand lines in both screens, combo steps 1
(native) / 2 (mirrored, weight 1) / 1 / 2, the mirrored grips (native right grip (314, 191, 6) -> left grip (-320,
194, 14), within the bind asymmetry); against the Surdana hostiles (fixture below): strikes R / L / R, the right blade
only in right strikes and the left only in left ones, hits on enemies by both hands each in its own strikes, the
mirrored lunge a reflection at full weight. Save/load (fresh process and in-session reload), bow transitions and
dropping either hand's weapon are also verified on the magic5 fixture. The bow preserves the off-hand assignment
in slot 2 and temporarily owns bone 19; returning to melee restores the off-hand attachment. Native bow drawing
only clears a **shield** from slot 2, not a melee weapon. The completed Rynn skin update clears the reserved melee
item's stale model-attached flag (`item+4 & 0x400000`) once the bow owns the bone and calls native item hide
(`0x34d068`, item data). Bow attach replaces the skeleton entry without native detach or scene-list removal,
otherwise leaving the old weapon floating at its last position. Clearing the attachment flag alone does not hide
it. Native hide clears the visibility modes (`item+4 & 0x3c`) and updates scene membership; native show (`0x34d1f8`)
restores mode `0x28` when melee reattaches the weapon. This also runs after the equipped-items hook because bow
drawing finishes later. The separate model visibility bit (`item+4 & 1`) is cleared with `0x17e418(model,0)`;
without this, the equipped-model draw still displays the displaced weapon even after scene-list removal.
Native skeleton attach restores model visibility from Rynn on melee return.
The bow harness checks attachment and visibility modes before/after firing and on return;
inspect its screenshots as well to catch scene-list errors that flags alone cannot establish.
Dropping the primary weapon puts the remaining off-hand
weapon away through native unequip, so it can be equipped normally instead of being stuck in an unusable left-only
state. Dropping the off-hand leaves the primary equipped. Rynn's Dagger itself cannot be dropped (native item rule).
Limit: IK targets (`0x117cd0`) are solved on the unmirrored
pose (no IK controller was active in the dumps). Debug: `DRAKAN_DUAL_WIELD_LOG=1` logs equips and left strikes, `=2`
every attack frame, the grips and root moves (with the mirror weight), blade changes (`blade right=0 left=1
strike=left`) and hits (`hit hand=left strike=left targets=1`).

**Harness:** `python scripts/harness/dual_wield_check.py --save <card>/mc0` (a save whose slot 1 has Rynn with two
one-handed weapons, bow put away, no shield). Two runs through `play_run.py` from `dist/DrakanPC` with a run-owned
`drakan.ini` (window pinned to 1280x960 4:3 windowed so the native Tab grid is where the script's cursor goes,
`dual_wield = 1`, native Tab screen): a scan run picks the weapons from a RAM dump (or `--cells right,left`), the check
run equips right then left, attacks six times and unequips the left weapon. `--engage` (a save with an enemy in
reach) locks on with F before the strikes, ends after them (Rynn may die) and adds the hits check, fighting up to
`--fights` times (default 3) until one fight passes; enemy fixture: `python scripts/harness/combat_save_fixture.py
<card dir> <new dir> 675929 159648 523349` puts Rynn beside the Surdana hostiles. Recipes use `clock milliseconds` and hold
every key / button for 120 ms (the input sample rate is not the game's frame rate). PASS / FAIL per check in
`build/harness/<name>/dual_wield_report.json`: details (hand lines), equip (slots 0 / 2, equipped flags), attach (bone
19, owner), strikes (right and left strikes, full mirror, never two lefts in a row), mirror (full-weight grips reflected
within 25 units), blade (each hand's blade only in its own strikes), root (full-weight lunge preserves horizontal
speed and vertical motion), unequip, and with `--engage` hits (both hands hit, each in its own strikes). Not covered:
save / load.
Unit tests: `scripts/tests/test_dual_wield_check.py`.

**Transition harness:** `python scripts/harness/dual_wield_transitions.py --save build/harness/magic5/card/mc0`.
Uses isolated cards, real save I/O and native menu input. The fixture must have the bow in cell 0, droppable club
in cell 1 and dagger in cell 16. Cases: bow, drop-left (dagger right, club left), drop-right (club right, dagger left),
save, cold load, and in-session reload after unequipping the dagger. `--case` selects one; load/reload take a card
already saved while dual wielding. Reports are `transition_report.json` in each run directory and `report.json` in
the run's config directory. Checks cover slot and attachment restoration, inventory flags, owner reference,
dropped-blade inactivity, recovery of the remaining weapon, and alternating strikes after load/return to melee.

`dual_wield_movement.py --save <dual-wield-card>/mc0` attacks while holding A and D and checks unmodified leg matrices
and locomotion deltas, alternation and blade windows. `dual_wield_toggle.py --save <dual-wield-card>/mc0` disables
during attacks, re-enables and equips, draws the bow and disables again. It uses the script command
`<time> dual_wield on|off`, which calls the same `setEnabled` setter as Settings without changing any INI. Both use
isolated cards; the toggle fixture expects club right / dagger left, with bow/club/dagger in cells 0/1/16.
