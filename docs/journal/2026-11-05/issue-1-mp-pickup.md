# Issue #1 multiplayer pickup freeze

## Scope and evidence

The report says collecting an item freezes the client, including City of the Damned, but does not
identify co-op versus arena mode. No live reproduction was run in this investigation; shared game
sessions and integrated builds are coordinated separately.

Code review identifies a concrete co-op pickup race: `hookSend` detects local native `0x10c1`
taken messages, while incoming `ObjGone` independently queues the corresponding actor for native
destruction. The local item may already be in inventory when that message arrives. The co-op
`destroyActor` path did not remove references from inventory or hands. Arena's existing
`dropFromInventory` explicitly records the same failure mechanism: equip/use through freed item
data reached a null JALR at `0x341548` and ended the guest thread. Arena already cleared its
references; that existing safeguard does not establish the cause of this report.

## Change

Extract the arena's reference cleanup into portable `world::detachInventoryItem` and use it before
co-op kill-list insertion as well. It clears every matching 16-byte inventory cell, each matching
hand handle, and marks the inventory dirty. Other items and hand holder metadata remain untouched.
Actor/player pointer validation stays in the runtime adapters. This covers immediate and pending
co-op ObjGone paths because both use `destroyActor`.

## Validation and limits

A standalone MSVC C++17 build of `runtime/world/tests/inventory_test.cpp` passed (exit 0), with
synthetic memory only. Coverage includes an already collected item in the first and last cells,
first and last hand slots, unrelated inventory/hand contents, dirty-list invalidation, repeated
removal and null inputs. The test is also registered as `world_inventory_test` for integrated CTest.
The shared runtime test tree and game deployment were not rebuilt by this worker.

The change fixes an identifiable stale-reference race. The reported complete freeze remains
unconfirmed, especially if the report concerns arena mode, which already had this cleanup. Live
acceptance still requires normal item collection and simultaneous collection in City of the Damned,
on both host and client, checking continued input/frame progress and equip/use after collection.

## Mounted-owner review correction

Review caught that the controlled actor may be Arokh, whose instance has no Rynn inventory layout.
Both adapters now require native Rynn class `0x10009` before accessing inventory; the common helper
also rejects other classes before any reads/writes. Type 3 alone is insufficient: Arokh also uses it.
The regression adds dragon/NPC/unknown-class inputs containing coincidental matching words and
requires zero reads and writes. This source follow-up awaits the parent's integrated build/test.

## Second log review: archived native failure, 2026-11-05

Re-read issue #1 and every current follow-up comment through the GitHub connector. The multiplayer
follow-up attaches a side-by-side multiplayer screenshot, not a gameplay log. `Downloads/drakan (1).log` is the same
83,903-byte SP Surdana report archived at `build/issue-review/issue-1.log`: it saves and quits
normally, so it cannot establish the reported multiplayer pickup trigger. Other Downloads files
`drakan.log` / `drakan.1.log` likewise contain no missing-target diagnostic. No fresh top-level
`dist/DrakanPC/drakan.log` exists. Its nested `build/ootf-run` log is an older unrelated run.
An unrelated active game was left undisturbed.

The archived arena logs do contain a useful concrete native failure:

* `build/harness/pvp_city_1/a/drakan.log:7937`: thread 1 indirect call at source `0x3418d4`
  targets zero, with argument 0 zero and property argument `0x13`. The immediately preceding events
  are phase entering Playing at 148.763 seconds, spawn placement, removal of one loose campaign
  item, and a two-entry kill-list flush. Subsequent network statistics continue through 229 seconds,
  but native input never reaches the scripted quit. This is an EE failure despite healthy UDP.
* `build/harness/arena_inq_r8/b/drakan.log:5372`: the same native source/zero target immediately
  follows phase Playing at 110.779 seconds, spawn placement, one-item campaign removal and kill
  flush. Network heartbeats again continue without native script completion.

Private inspection of existing generated output identifies `0x341858` as the item usability
check. At `0x3418d4` it calls the item's property object's virtual getter for property `0x13`
(skill requirement). This supports a missing/freed item property object, rather than a network
wait. No game-derived generated code or disassembly is included in tracked files.

These old traces do **not** prove an ordinary pickup caused the report. The City run's synthetic
TAKE request occurred earlier, never produced a pickup claim, and its summary records pickup
checks as false. Its fault occurs after a later level/phase restart. The precise freed item's
handle is absent from the diagnostic, and current source differs from that old build. The
original report's trigger remains unconfirmed.

### Additional correction and regression

The existing arena peer-removal branch detached inventory references, but campaign `removeCampaign`
called `killActor` without that safeguard. Move detachment to the common arena kill boundary, before
native kill-list insertion, covering sweeps and synthetic removal as well as peer claims. The
Rynn-class guard remains mandatory. No native null-call bypass is added.

The two-instance harness now rejects missing guest branch targets and requires native input's
scripted quit on each participant; network stats are insufficient. Three synthetic Python
regressions pass, including fault logs with continuing UDP statistics and faults followed by an
exit. `--pickup-method walk` selects the existing native interaction aid, rather than synthetic
TAKE, for forthcoming functional acceptance. Ruff passes for the edited harness/test.

Fresh acceptance is pending the parent's canonical build and live-test slot: use City Skeleton
Square, actual walk/Use collection, both host and client, then verify inventory/equip and continued
scripted input. Existing tests of the common cleanup do not alone prove the sweep trigger or
ordinary-pickup fix. The shared game/build was not touched by this worker.

## Fresh direct-dist menu runs: r1 and r2

Both use deployed SHA-256 `9c38cb9042b43a916a7e4e49cffac0534f3ff6df20ede62e883c303c647f4c78`.
`scripts/harness/arena_pickup.py` creates private main/native/input/multiplayer INIs and cards,
launches `dist/DrakanPC/drakan.exe` with that directory as cwd, and navigates real Host/Join menus.
It records eight RAM snapshots per role around collection, native inventory, equip attempts and
continued gameplay. It never uses synthetic TAKE. Host/client are seeded as native Rynn.

* `build/harness/issue1-city-native-pickup-r1`: default map. Host registered City through the menu,
  completed the native input script and exited 0. Client remained on the Join page (Refresh focus);
  one explicitly recorded OS Up/Enter intervention did not join, and its process was terminated
  after host completion. **Not an acceptance pass.** Crucially, the default map's adopted Blade
  and Mana entries were disabled: no loose actor within 600 units. The HLE arena filter now skips
  campaign pickup initialization. The later gained inventory object was starting loadout resource
  `0x33cbc`, not a pickup. The harness now explicitly requires Blade resource `0x3373c`.
* `build/harness/issue1-city-native-pickup-r2`: private same-id map override supplies `res=0x3373c`
  for Blade and `res=0x31b33` for the formerly adopted Mana. The original Blade id/class came from
  the private old City world snapshot (resource index 847 in that build). Both factory actors
  were created and bound on both machines. Delaying client menu input 20 seconds and host Start
  to 60 seconds allowed an unattended real JoinMatch. Both native scripts completed, eight RAM
  snapshots per participant were captured, and both exited 0 with no missing guest branch target.
  **Pickup acceptance still fails:** neither sent a pickup claim or gained the Blade.

The r2 failure is an approach obstruction: native collision moves host about 926 units and client
about 1424 units away from the target; repeated logs keep the Blade live at its original coordinate.
The host photograph shows the steep graveyard/ridge location. Native inventory, rendering and
scripted input continue throughout; this is not a reproduced freeze. No original-coordinate
collection is certified. A subsequent private arrangement will place a real spawned Blade near
the confirmed spawn-0 floor (`159154, 169772, 457445` from RAM), then use native contact/Use.
The live slot was transferred cleanly to the D1 worker after both r2 game processes exited.

R2 also exposes a test-input detail: the first inventory open displays the native Equipment help
modal. The next recipe explicitly dismisses it before selecting the second grid cell. Script
cursor coordinates use the 640x512 input coordinate system, so cell two is about `250,124`, not
the older `215,218` coordinate. Native equip may correctly reject Lastat's Blade for insufficient
skill; invoking its real usability path and continuing afterward is still meaningful coverage of
the archived property-getter fault. Starting dagger ownership alone never counts as a collected Blade.

### Archived peer-pickup trace recovered during the broader log review

The more direct historical pickup case is `build/harness/rt_ar5/b/drakan.log`: line 1407 reports
Short Wood Bow pickup 0 taken by player 1 and removed here; line 1531 calls target zero at native
`0x341548` with saved item handle `0xb03e80`. Subsequent UDP stats continue. `rt_ar` / `rt_ar2`
repeat the same actor and fault. The earlier `2026-11-01/regression-triage.md` independently records
this reproduction; `arena-pvp-open-items.md` describes the subsequent inventory/hand cleanup.
Thus an actual archived pickup-removal freeze exists, but that earlier arena fix already covered
peer-removal when this investigation started. It does not make the new City test pass, and it
does not prove the user's current build has the identical trigger. The new common arena kill
boundary extends the protection to campaign sweeps rather than suppressing a native failure.

Additional harness-oracle regressions (`scripts/tests/test_arena_pickup.py`) pass 3/3: late initial
dagger loadout cannot count as collection; a real Blade plus native completion can; a guest fault
cannot be hidden by subsequently present items or quit. Both targeted files total 6 passing tests.

## R3 fixture result and native-Use follow-up

R3 uses deployment `de57cf5f6badbc06d234debc28df7b178668660695e3b4c57adae9ed7e5a8415`.
The private map moved the Blade to `159954, 170000, 457445`, near the measured starting floor.
Both participants joined normally, completed their native scripts and exited 0; all eight RAM
snapshots per role exist and no missing guest target was reported. **Strict acceptance fails**:
no native pickup claim, no Blade in inventory, no corresponding server acceptance/peer removal.
The test is not counted as a pickup pass.

Inspection shows the GOTO aid is guest teleportation followed by repeated direct transform steps,
not ordinary walking input. Its sweep spans +/-900 units and ends past the item. The current
`kPlaceTolerance` is **3500** units (earlier status estimates of 1500 were incorrect), so its
"placement took" log cannot establish contact. In r3 the host ends about 1256 units off target.
The corrected UI cursor does select the second inventory cell; the screenshot shows it empty.
The native scripts continue, so this is failed contact setup rather than the reported freeze.

R4 removes GOTO entirely. A private fixture puts one real factory Blade near each participant's
measured settled spawn position, uses ordinary E/Use input at 10-13 seconds, then inventory,
native equip/usability, movement and a scripted quit at 38 seconds. It requires actual native
Blade ownership, claims for pickup 0 by player 1 and pickup 1 by player 2, server acceptance and
peer removals. The original City coordinates remain uncertified, and no default-map repair is
applied before native collection is proved. This fixture is queued behind the reserved tests.

## R4 native Use result and spatial-registration correction

`build/harness/issue1-city-native-pickup-r4/`, executable DE57 as above, private map receipt
`maps-candidate.ini` SHA-256 `62d29cfc6ac100b4f4cf83ba012d2d70f72fd1cf8125c2902b3b885280cc4676`.
No GOTO or TAKE environment is present. Real E/Use input runs at 10-13 seconds near a real
factory Blade at each participant's settled spawn. Host/player 1 target is pickup 0 at
`159000,169900,457420`; client/player 2 target is pickup 1 at `204250,169650,464640`.
At the post-Use capture, the host is about 81 horizontal units from its Blade and the client about
80 units from its own; item height is about 190/210 units above their feet. Both scripts complete
and exit 0, all eight captures per role exist, and no missing guest branch target appears.
**Acceptance fails**: no native Blade receipt, claim, server acceptance or peer removal. No freeze
is reproduced. The host screenshot shows no Blade at the near-player location.

The fresh RAM supplies a concrete cause for these uncollectible factory items. Both world Blade
handles (`0x894e50`, `0x886680`) have resource `0x3373c`, model `0x317d9`, valid property pointers,
but actor cell `+0x7c` is `-1`, spatial flags `+0x104` are `0x10000`, and model instance `+0x50`
is zero. The native starting dagger, after stow/equip, has a valid cell and active spatial flags.
`+0x9c` is a secondary-list index, not a semantic actor-flags field. The harness now records the
registered world items and cell state in every parsed RAM receipt, so the evidence is reproducible
without hard-coded handles.

The existing `drakan_ghost::refileActor` implementation already documents this exact lifecycle:
creation before destination terrain is resident leaves cell -1; normal native refile returns
immediately, so the helper performs native cell lookup and then registration. Arena factory
pickups never retried this registration. They could remain invisible and absent from native
interaction queries even once the player's destination terrain was loaded.

### Implemented bounded registration retry

Only a Present, live-at-home, unclaimed factory pickup with cell -1 is retried through the
existing native helper. At most one registration attempt runs per pickup scan (every third
PadMgr boundary). Failed attempts for a given item are spaced 240 boundaries apart, permitting
all 64 entries to get a turn before failed early entries retry. No killed, remotely claimed,
already registered or inventory-owned actor is exposed to world collision lists.

A further ownership check now precedes position settling: a newly spawned item collected within
the first 150 settling boundaries must become Gone and send its claim, rather than copying the
inventory position into its home. This is a source-identified claim-loss window; R4 itself did
not collect a Blade, so it does not reproduce that subcase. Native Rynn class validation guards
the read-only inventory check; mounted Arokh is never interpreted as an inventory owner.

Portable `world_spatial_registration_test` passes as a standalone MSVC C++17 executable: terrain
missing then arriving, valid-cell stop, eligibility suppression, one-attempt budget and fairness
across all 64 actors. Updated inventory ownership/detachment regression also passes, including
non-Rynn zero-read checks. The 10 targeted Python harness assertions pass and Ruff/diff checks
are clean. Parent owns the canonical rebuild and serialized R5 live slot. These source changes
are not counted as live pickup acceptance until native receipt/server/peer evidence exists.
Default City map metadata is unchanged pending that proof.

### Native-options fixture correction before R5

The movement worker identified that the inherited harness's three-field native-options sidecar
omitted nine required settings, preventing preparation of the game's normal startup configuration.
R1-R4 used that partial file. Their negative native movement/control observations are therefore
invalid as production control acceptance. They remain useful RAM/log evidence of unregistered
factory actors and lack of collection; they do not establish that valid ordinary Use was exercised.
This is a material fixture limitation, not a successful pickup result.

The pickup harness now copies the **complete deployed twelve-field** native-options file into each
private run folder, rejects a partial source before launch, and requires the actual startup log
`[menus] native options prepared` in acceptance. It never writes the deployed file or user card.
Partial-rejection and complete-preservation tests pass; targeted Python coverage is now 12 tests.
R5 is scheduled on deployment `4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278`,
which includes final spatial and early-inventory-claim corrections, after the directional worker's
clean live-slot handoff. No further runtime edits are made during that build/deployment.

A final source audit found that `world::isLiveHandle` checks handle vtable and registry reference,
not the native dying bit. After deployment 4CE9, the parent authorized one explicit additional
spatial eligibility guard for actor `+4` bit 31, preventing re-registration of queued-kill actors
before the tail flush. That one-line guard awaits the final integration deployment. R5 on 4CE9
covers ordinary alive items only; no killed-actor live probe is run.

## R5 strict native collection acceptance

`build/harness/issue1-city-native-pickup-r5/`, SHA-256
`4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278`, same private native-Use
fixture as R4, **complete deployed twelve-field native options** copied privately. Both startup
logs contain `[menus] native options prepared`, and both navigate ordinary HostMatch/JoinMatch.
No GOTO, TAKE, direct stow or direct item destruction is used for collection.

* Both instances log recovery of native collision cells **22** and **24** for the real Blade actors.
* Client/player 2 collects pickup 1: its native claim is accepted by the server at **106.359 s**,
  and the host removes the corresponding world copy. Host/player 1 collects pickup 0: claim
  accepted at **107.249 s**, and the client removes its world copy.
* Native inventory cell 1 contains resource **0x3373c**, type 10, with a valid property pointer on
  each participant. Host handle **0x894e80**, client handle **0x8866b0** remain in their inventories
  through final capture; their unrelated equipped starting daggers remain valid.
* Directly inspected host `equip_request.png` and client `late_inventory.png`: both show
  **Lastat's Blade**, skill needed **5** versus Rynn's **1**. The native equip attempt correctly
  refuses the requirement, rather than silently equipping an unusable weapon. The UI remains
  responsive and the dagger stays equipped. This exercises native item properties/usability;
  successful equipping of the higher-skill Blade is intentionally not claimed.
* After inventory closes, native W movement translates host **2438.8** units and client **2481.1**
  units horizontally. Both subsequently open inventory again, use/attack and move, then reach
  native scripted quit. Both processes exit **0**, with no missing guest branch target.

The original harness invocation returned 1 because its RAM reader treated native small integer
cell tags **1/2** (slots 9/17) as actor addresses. The production arena `armTick` already skips
handles <=0x53. The reader now applies that exact known distinction, records the tags separately,
and still rejects other invalid pointers. A synthetic RAM regression covers this case. The
original `summary.json` is retained; corrected independent `review.json` contains **all checks
passing** (both complete role receipts and four server/peer checks), records the oracle correction
and verifier hash, and preserves the original harness exit status. This is a verifier correction,
not a suppressed gameplay failure. Targeted Python tests total **13 passing**.

All owned game processes exited before the clean slot transfer to the Inquisitor movement worker.
No concurrent live runs were launched by this worker.

### Default-map follow-through and remaining limits

After native collection was proved, add resource fallbacks to City's original pickup 0
(`res=0x3373c`) and pickup 1 (`res=0x31b33`), preserving **coordinates, indices, labels and times**.
The original private City snapshot independently identifies the Mana at approximately
`198100.672,168050,425684.656` as resource `0x31b33`, class `0x100ab`. The default map previously
attempted adoption of actors whose native initialization the arena filter now removes.
`net_arena_test` adds a compiled-builtin metadata regression for these resources and unchanged
positions. Parent owns its final build/test/deployment, including the explicit dying-bit guard.

R5 certifies ordinary native collection, receipt replication, native inventory/usability and
continued gameplay on both participants in an explicit near-spawn fixture. It does **not** certify
approach/collection at the original authored City coordinates, a WAN race, a killed-actor stress
probe, successful high-skill Blade equipping, or an exact reproduction of the reporter's frozen
binary. The historical stale-reference failures are documented above; the current fixes cover
unregistered factory items, early pickup claim loss and safe inventory detachment before removal.

The maintained older two-instance harness now names its synthetic placement aid
`--pickup-method goto`: it teleports/sweeps the actor and sends Use, and is not
ordinary walking. The R5 native acceptance uses `arena_pickup.py --interaction use`
with no GOTO or synthetic TAKE.
