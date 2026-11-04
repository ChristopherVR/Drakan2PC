# OotF walk-in exits: explicit detector messages

Reviewed normal campaign progression after the save-editor pass. The historical campaign chain is not a walkthrough:
it used host-fired exits, and its named-destination requests fail for converted local XDB strings. The November 4
chain log reached Intro and Ruined Village, then reported no exit leading to the requested names. This is not evidence
that the source quests or every ordinary walk-in exit work.

Found a concrete converter defect in normal walk-in setup. `applyCampaignExits` correctly clears copied detector
parameters, but then relied on the chosen template class's defaults. The template LEVEL1NA dependency order starts
with System (db 2), followed by Resources (3) and FX (4). The pipeline chooses the first class named Trigger Detector.
The actual disc census shows its defaults differ between databases:

| Database | Class | Trigger Message default |
| --- | --- | --- |
| SYSTEM | 3050 | 1 (ON) |
| FX | 9695 | 5 (touch) |
| LEVEL6EN | 6779 | 1 (ON) |

Consequently the generated System detectors could send ON to the exit rather than the touch message expected by the
exit/prompt path. The previous investigation's assumption that the class default was 5 only held for the FX variant.

Added `exitDetectorParams`: synthesized exits explicitly set Task 6, Trigger Message 5, and Detect Which 0 (foot).
Mounted detectors inherit the same fields and override Detect Which to 1, with one parameter per field even when a
debug Detect Which override exists. The existing two-detector arrangement remains; this does not invoke host jumps.

Validation: all 25 campaign-format tests pass, including a regression for both foot/mounted parameter sets and duplicate
field prevention; formats TypeScript check passes. Regenerated the combined 23-level pack from the player's source
and extracted PS2 template at `build/ootf-run/implementation/mods/ootf-levels.zip` (137,794,043 bytes; SHA256
`A87B8AFB690CC2BBE4A89F81931F524539818736C7D220CB8545EE8B76F84E6C`). Parsed all generated level containers:
52 synthesized detectors across 12 levels link to System exit class 498 and explicitly carry Task6, Message5, and foot
or mount selection. Rift World has no campaign exit. Conversion log: `build/scratch/ootf-exit-convert.log`.

Source pressure-plate census is saved privately at `build/scratch/ootf-progression-plates.json`. Existing conversion
still simplifies plate release/reset and initial-state behavior; no claim of a complete quest walkthrough is made.

## Real walk-in transition proof

Run `build/scratch/ootf-walk-rv/rv-wartok-walk-strafe` used the fresh combined pack with private test setup only:
Ruined Village as the fixture's start, Rynn object1740 placed at (144480,24896,211968), and the opening Stomp2217 set
to triggered rather than automatic so the test does not relocate a cinematic-controlled actor. Gameplay setup used
the existing direct-level path and position aid; no `DRAKAN_LEVELCHANGE` or host exit request was present. The shared
production pack was unchanged. `DRAKAN_PAUSE_UNFOCUSED=0` is necessary with parallel/no-activate windows.

The generated Wartok exit1077 is at (148480,24496,211968), with a 2x2x2-cell detector centered 700 above it. The player
starts outside its x bounds. Holding D produces real native motion (144480 -> 145070 -> 148724), enters the box, sends
handler message0x20 and the native touch event, then opens GUI2. `after_walk.png` shows the actual "In order to progress
beyond this point the game must be saved" prompt. CROSS follows the normal GUI2 -> GUI5 save-slot path. The log records
the RV exit and loads Wartok; GUI14 -> -1 with gameplay1 at t52.61. Arrival: (76478,10592,14235), yaw1.850. A save is
written only to the isolated run card. The run quits cleanly, exit0, after101s; no crash, stall, missing-target or bad
memcpy line occurs. Shared game lock was released.

This proves actual walk-in message delivery and the ordinary save/transition path. It does **not** prove a complete
campaign route or unobstructed arrival camera: `destination.png` has HUD but the camera is blocked by cave geometry.
Earlier setup attempts without unfocused-pause disabled or with relocation during the RV opening scene were invalid
and are not counted as failures of the fixed exit. The first movement attempt used W (-z), whereas the test needed
rightward D (+x); that setup correction is recorded in the private run names.
