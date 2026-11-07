# Outstanding work after the 2026-11-06/07 stability, playtest and editor sessions

Written when three agents (editor, OotF campaign, base-game playtest) were stopped on purpose. It lists what is still open,
where the evidence lives, and the next step for each item. Everything proven is in the dated journal notes named below; this
page only points at them.

## State at the stop

- 25 commits are local on `main`, not pushed. The last push (`de67409b`) passed the commit policy and documentation checks.
  Run `python scripts/repository_guard.py` and `python scripts/run_tests.py` before the next push, and check CI afterwards.
- `dist/DrakanPC/drakan.exe` was redeployed at 16:27 on 2026-11-07. It does **not** include the later runtime fixes
  (`4a96154e` polyclip preemption, `be1d61ce`, `04c9d222`, `0d2a3e1e`, `ae106806`, `99b87a97`, `75205e1c`). Redeploy with
  `pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath <iso> -WorkDir build/final-game -Incremental -Jobs 8` once no game runs
  from `dist`.
- Uncommitted in the tree:
  - `editor/scripts/vendor-pack.ts`: the stopped editor agent's partial work on a `--new-shop` option that builds a shop with
    the editor's `buildNewShop` for the shop-in-game proof. Unfinished and unverified; finish or discard.
  - `ps2recomp/ps2xRuntime/src/lib/drakan_overrides.cpp`: another session's long-lived edit. Not ours.
- Two game processes from the stopped agents may still be running from `build/ootf8/play/` and `build/gaps2/play/`. Close them
  (`taskkill //PID <pid> //F` from Git Bash) before the next quiet-machine measurement.
- Scratch from the agents lives under `build/` (gitignored): `build/harness/*`, `build/gaps*`, `build/ootf*`, `build/proofs`,
  `build/fixed`, `build/editorplay*`, `build/fps4b*`, `build/vuring*`, `build/mp`. Safe to delete when the space is needed.

## 1. Order of the Flame campaign (issue #5)

Evidence: `docs/status/ootf-port.md`, `docs/journal/2026-11-07/ootf-progression-3.md` (sections 8 to 11).

Proven: the converted campaign reaches Rift World, the four Dr. Evil heads and the Ebon Dragons die at their real hit points,
the ending scene plays, the end card scrolls (`cce1190f`) and the game returns to the title. Delon's invulnerability was a
converter bug (formats `5e67f7c`), and his hit points reach 0 under breath. The Ruined Village opening needs no key press.

Open:

1. **Ruined Village exit.** The exit (PC object 1075, a Creature detector with "Does Cave Entrance Teleport? 1" and "Dragon Takes
   Off Upon Teleport? 1") is meant for a mounted dragon. Arokh is story-gated: he sits in the lair at 484416 24773 201196 tied to
   the Dragon Clutch detector 1089, a Dragon Falls prop and the "arokh awakening" scene 1625. Nobody found the step that frees
   him; E beside him did nothing. Next: read the PC links of 1089 and scene 1625 and play the step.
2. **Chain from Wartok on in one process.** Only separate walk-in pairs were proven. No per-level time, FPS and memory table
   exists past the Intro and Ruined Village. Use the closed-loop driver (`f5dab15f`, `docs/journal/2026-11-07/ootf-closed-loop-driver.md`).
3. **Delon on foot.** Rynn arrives in Rift World mounted with an empty inventory, so a sword fight was never played. Options:
   a weapon item placed on the altar ring (needs a weapon class in the Rift World object database), a spell through the
   inventory flow, or a natural save that carries Atimar. His Defense 100% behaviour and damage while shielding are unmeasured.
   Altar floor map: ring solid for x 338000..346500, z 178000..186000, hole about x 341000..343500, z 180500..184000;
   `place 342044 100804 184976` lands on the floor.
4. **Human flyability of the RibDoor tunnel.** Scripted W-only flights enter the hall between heights 94000 and 97500 (about a
   3500 unit window, dive of about 3000 from the arrival height 100119). No person has flown it. The tightest point is the ramp at
   x 334000, z 279000 (floor 92500, ceiling 97262). Nothing was changed.
5. **Credits after a title-chosen game.** The per-read overlay is meant to cover a game chosen on the title after boot; only a
   `DRAKAN_GAME=d1` run was verified in game.
6. **War giant hammer reveal** after the kill was not re-checked.
7. **Issue #5 comment.** A comment-ready summary is in `ootf-progression-3.md`. It has not been posted.

## 2. Base-game playtest gaps

Evidence: `docs/journal/2026-11-07/playtest-gaps.md` (all follow-ups), `clean-playtest.md`, `kragmor-arrival.md`,
`surdana-null-jalr.md`.

Proven: all 13 levels tour clean (60 vblank/s median), exits that fire on foot or by teleport complete the message box, save
screen, `exit taken` and load, deaths reload in about 0.1 s with a quick save and fall back to the title without one, 21 minute
soaks of 3A and 4A show no growth, Kragmor no longer strands the player (`727aaa8e`).

Open:

1. **4B Northern Tundra exit.** Arokh mounts reliably and crosses the ridge by flight, then reaches the Savage Gate (closed ring
   door at 650275 144967 1744595) in front of the pocket holding exit detector 5595 (681024 142708 1765312). An Invisible Static
   Wall (7527) closes it until Vis Toggler 5727 fires, fed by timers 5728, 7795, 7796 and 7798 on message 24 ("Shoot") with no
   incoming links, so an engine message such as breath or an arrow on the gate wheel is the likely source. Not yet tried:
   aimed breath at the wheel from x 640000, y 141800; check Stomp Player 5513 at 621397 140387 1726379.
2. **Valley of the Fallen.** Zola Dane's object 9274 sits on a scene set at y 230000 (Gothral, farmers, `Dane Teleport` 20420,
   `Zola Teleport Fountain` 20421) with no incoming link. His death (message 8) feeds cut scene 20233, the lever chain
   (timer 20860, lever 18379, timer 18378, transition 9781) and the Valley icon. What enables the real fight is unknown; the
   fight and the lever were not played.
3. **1A Lair exit 12479** (cave at y about 150000 under a 160498 plateau; `place` never accepts the cave floor) and **1A to 3
   portal 18028** (Detect Which? = 1, mount only). In 1A Arokh is parked on a cast stage at y 200000, about 570k units away and
   not mountable. How a player gets him there is unknown.
4. **Forward New Game chain** past 1A, 1B and 1C was not run (1D's return is gated, others are mount only, disabled or
   underground). Needs the mechanisms from items 1 to 3.
5. **Exits never driven:** level 8 (8143, 10481, story script driven), 3A 8021 (end-of-level Arokh flight), 4A 5132 (no links),
   Valley 9779 and 9781.
6. **Shadowmire spot** (320168, 205764, 503600): not a soft-lock, drops into a lower cavern with its own detector. Unknown
   whether a player can fall in legitimately.
7. **Harness limits:** a pad tap after `sync screen 5` does not advance the save screen, the Valley and 3A openings take 60 to
   130 s and block `place`, and `place` into solid ground is lifted to the surface.

## 3. Puzzle wiring and the editor

Evidence: `docs/journal/2026-11-07/door-proof.md`, `editor-round3.md` to `editor-round8.md`, `docs/reference/modding.md`
(puzzle section), `scripts/harness/puzzle_door_check.py` (16 scenarios, all passed at the last run).

Proven in game and measured from RAM: lever (toggle), pressure plate, held plate, two-plate counter, timer, ambush
(`Wait For Trigger`), kill-all gate with Blow Up and with sword kills, key lock with a picked-up key and its no-key control,
locked-door rules.

Open:

1. **Shop form and shop in game.** `scripts/harness/vendor_shop.py` exists; the editor shop template was not run to the end in
   game this round (partial `--new-shop` option is uncommitted, see above).
2. **New level exit fires in game.** The "Level exit to..." template was fixed to place the Trigger Detector that fires it
   (`dd4e4a19`), but no run walked it. Rynn drifted away from the placement. Use the walk-in approach in
   `docs/reference/input.md` section 13. The project map chain with a second level copy was not run.
3. **Save As in the desktop app** (File > Save Project As) is built and unit tested but never clicked through. The close hook
   after a Save As is untested.
4. **Not proven:** bow kills for the kill-all gate, a plate released by Rynn walking off it, a Locked-door warning seen firing in
   the form (no Stratos door has a Locked value), the editor trigger-to-spawn path beyond the dormant skeleton proof.
5. **Accessibility:** no real screen reader (NVDA) run; the OS file drag over panels cannot be driven by Playwright; the campaign
   wizard focus trap is unverified in a browser.
6. **ui-sweep** was last run before round 8 (198 states, 0 findings). Rerun after the round 8 changes:
   `npm run build -w @drakan/editor-app` then `npm run ui-sweep -w @drakan/editor-app`.
7. **Polish:** deeper 1920x1080 look and feel (spacing, icon weight) was only read on four screens plus sweep shots.
8. **Quota overflow** for the converted level store was not exercised in a real browser (a 2 GB allocation crashed the tab).
   Switching language after the disc is open does not re-derive a restored pack; reopening the disc does.
9. **Play restore** was tested with a stand-in program for `drakan.exe`, not the real game with two editors.
10. **Editor assets:** Play still replaces nothing of the player's own pack (separate id), but a full 23-level pack and the Play
    pack cannot run together; Play switches `ootf*` entries off for the session and restores them afterwards.

## 4. Rendering and performance

Evidence: `docs/journal/2026-11-06/stratos-garbled-effects.md`, `docs/journal/2026-11-07/fps-4b.md`,
`docs/reference/perf.md`, `docs/reference/stability.md` section 9, `docs/journal/2026-11-07/review-findings.md`.

Settled: the Stratos garble is the game's own polygon draw with no near-plane clip. It is clipped by the opt-out near-plane clip
(`[video] poly_near_clip`, F1 Video page, `DRAKAN_POLY_NEAR_CLIP=0` disables). On a quiet machine no 4B spot is below 95 world
fps and vblank holds 60. The worst spot (C) is limited by the EE thread at about 96% busy and the cost is spread over many
guest functions.

Open:

1. **Polyclip preemption path** has a unit test with a stub only. No run actually hit a preempted draw. The scratch vertices live
   at RDRAM 0xf0000 behind a one-time all-zero check; nothing proves no other code uses that area.
2. **Real PS2 behaviour** for the straddling ribbons is unverified. The clip is an enhancement, not a hardware-faithful fix.
3. **Near-clip cost** in the Level 8 `s_4` scene has no counter number (the runs predate the counter). The 3A fire ring costs
   0.22% of EE time on average and 0.78% in the worst second.
4. **First shader/pipeline compile** can block the EE for up to about 30 s under heavy CPU contention. Warming the pipelines
   before the first guest frame would remove the stall that the guest-time RPC wait (`1f27d1ea`) only survives.
5. **EE cost at 4B spot C** (8 ms per flip): faster guest memory and dispatch or fewer guest instructions per frame. One cheap
   item: `Tracker::freed` in `object_motion_tracker.h` scans whole maps on every free (2.4 to 4.7% of the EE thread).
6. **`bringDragon`** has no wall check (no cheap collision query), and is skipped on campaign pack and editor levels. Nothing
   else of the review findings is open; see the "Resolved" table in `review-findings.md`.
7. **Rejected script lines** now call `requestStop()` instead of exiting with code 2, so the process exit code is no longer 2.
   Not tested with a bad script.
8. **`ootftex.test.ts`** timed out once at 5 s under load; unrelated, not investigated.

## 5. Reporter-side items (issues #1 and #3)

Evidence: `docs/journal/2026-11-07/issue-1-language-audio-shadows.md`, `docs/journal/2026-11-06/issue-3-localized-ootf.md`.

Nothing reproduced at HEAD for the multiplayer list, the language stutter, the audio tone or the ray shadows. Still needed from
the reporter or real hardware:

- a clean-machine stutter capture with their hand-modified German image (my swapped image did not reproduce it),
- a real German Order of the Flame install for an end-to-end conversion (only synthetic fixtures exist),
- confirmation of the audio fix on their hardware (AE-7), and a WebGPU shadow re-run,
- abrupt process loss for multiplayer (falls back to the 10 second timeout), LOD of the rest of the roster, a WAN pickup race.

## 6. Housekeeping

- `docs/status/handover.md` has Open work lists that predate this session in places; the items above supersede them where they
  overlap.
- `ootf-progression-3.md` was converted from CRLF to LF by one commit (`ef3420b2`), so that commit shows a whole-file diff.
- `4f4a6c24` and `95f8d421` were each committed with one failing test that the next commit (`a688d1ed`, `bd61f450`) fixes.
  Keep each pair together if the history is ever rewritten.
- Several agents could not stop their own processes (the permission classifier refused). Check `tasklist` for stray
  `drakan.exe`, `node.exe` (vite, desktop) and `cl.exe` before any measurement.
