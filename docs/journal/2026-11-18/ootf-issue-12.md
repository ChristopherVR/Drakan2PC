# Order of the Flame issue 12

The [player report](https://github.com/ChristopherVR/Drakan2PC/issues/12) lists 15 symptoms. It gives no build, save or crash log. This pass used the local PAL executable, English PC data, and private harness cards.

## Music and subtitles (12–14)

* **12. Music Volume.** The old campaign curve gave identical gain from 25 through 100. The new curve gives half gain at the default of 25 and reaches full gain at 100. The runtime test checks strict increase at every integer setting, mute, full scale and the engine's scene attenuation. In the opening at Music Volume 50, track `e0004` logged gain 0.668 with engine gain 0.446, as expected for a track with base gain 0.891. This verifies the music-category control. The intro also has music recorded as sound effects. The balance of those clips and the player's particular scene remain open.
* **13. Subtitles.** The local play folder had Show Subtitles enabled, but its pack had no subtitle index. The guided builder had no way to pass a transcript, and the editor importer never supplied one to conversion. Both now read a local transcript and the builder includes its contents in the conversion cache key. A previous private pack supplied 295 English lines for this machine. The rebuilt 23-level pack contains 295 marked sound descriptors and retained save build `493b53de681b8791`. A fresh opening displayed seven timed pages across four dialogue clips in Ruined Village. The screenshot at `build/harness/issue12-subtitles/village_b.png` shows the caption in the rendered frame. The guided installer now includes the English transcript and offers local generation, as verified below.
* **14. Quiet gameplay speech.** Rynn's gameplay recordings and their converted volume settings were checked separately from the music tracks. The measured gain changes are recorded below.

## Spider cave (15)

Wartok Canyons contains objects named Spider Cave Entrance and Spider Cave Door, with spiders deeper inside. Ruined Village also contains cave spiders. The PC Grimstone Mines object list contains no placed class named Spider.

The mounted Wartok Canyons run reached the cave and exited normally. On foot, Rynn reached the death screen near the first spiders. With her health held at maximum, she reached both groups, spiders attacked her, and the game exited normally. The frame at `build/harness/issue12-cave-encounter/encounter.png` shows the encounter. To repeat from the repository root:

```powershell
python scripts/harness/play_run.py --name issue12-cave-encounter --recipe scripts/harness/recipes/ootf_wartok_spider_cave.txt --attempts 1 --timeout 130 --env DRAKAN_GAME=d1 --env "DRAKAN_START_LEVEL=OotF 03 - Wartok Canyons" --env "DRAKAN_START_LEVEL_FROM=Level 0 - Intro" --env DRAKAN_PLAYTEST_GOD=1 --env DRAKAN_FULLSCREEN=0 --env DRAKAN_WINDOW_WIDTH=960 --env DRAKAN_WINDOW_HEIGHT=540
```

A separate Ruined Village placement run exited normally, but the engine settled Rynn above the requested spider positions. These short runs bypass the normal level approach. The reporter has no save to share. Follow-up runs use progression saves created locally.

## Other reports

| Items | What to check on the reported route |
| --- | --- |
| 1–2. Black ground, falling through floors, canyon pop-in | Record the position and camera direction. Compare the visible terrain and collision height at that point. For Wartok Canyons, repeat the same approach to the missing section from far and near. |
| 3–5. Arokh's face, movement and summon | Use a pre-awakening save. Check the neck and jaw during close-ups, then test player control, mounting and a call after separation. The [earlier animation work](../2026-11-16/ootf-arokh-scenes.md) identifies the scene and the neck channel. |
| 6–7. Lock-on and orbiting pickups | Record the controller scheme, the target or item and whether another item is within reach. Observe target selection and the inventory count after LB. |
| 8–9. Drop disabled and zero durability | Identify an affected item. Compare its converted class defaults, live current and maximum durability, and drop eligibility before and after save/load. Intentional behaviour is still unconfirmed. |
| 10. Bow rectangles and frozen movement | Record the equipped bow, arrow type and aim mode. Capture a shot and movement with the bow equipped before, during and after aiming. |
| 11. Inventory health | Compare the original and modern inventories while injured and after drinking a potion. The modern inventory explicitly hides the gameplay HUD when its own screen takes over. |

## Follow-up playthrough and fixes

* **1. Starting ground.** Compared all 39,710 authored Ruined Village floor triangles to the installed level. Their collision holes, hidden faces, diagonals and layer locations match. All floor vertices agree to within two world units. Wartok Canyons (61,488 floor triangles) and Grimstone Mines (19,484) also match. This checks terrain conversion. A runtime fall through a prop or a streaming boundary is still possible.
* **2. Canyon draw distance.** From the canyon entrance ridge at (86800, 20200, 50000) looking north, a 32-cell limit leaves sky behind the palms. The fog-horizon run draws a continuous mountain ridge. The runtime now derives the search reach from the active fog distance in every converted level. It measures from the camera cell and refreshes the visible list when the reach changes. The regression moves the eye along one wide layer and varies fog distance around the same ridge. Comparison frames are in `build/harness/issue12-terrain-{32,75}/ridge_north.png`.
* **3–5. Awakening, riding and calling.** Triggered the full 148.5-second awakening in a fresh Ruined Village run and accepted the save prompt. The face is upright in the two dialogue shots at 58 and 82 seconds. The private canyon arrival save loads with Rynn mounted. Holding forward moves Arokh from (87660, 14241, 39210) to (87659, 14487, 45031). Holding Cross dismounts. After Rynn backs away and holds L1, Arokh takes off, flies toward her and circles over her below the ridge. Landing beside her in that spot was not observed. Evidence in `issue12-awakening` and `issue12-canyon-controls` under `build/harness`.
* **6. Target lock.** At the canyon Long Bow pickup, four successive R2 presses alternated Rynn's native target handle between 221 and zero. The lock and unlock shots and the sampled handles are in `build/harness/issue12-canyon-final`. An intermittent failure was not reproduced in that fight.
* **7–9. Pickup, drop and durability.** Collected Rusty Chain Mail with LB in the village, inspected its 15/100 durability and dropped it. Three LB presses at the canyon weapon pile collected a bow, sword and ten arrows. A comparison of 97 placed melee weapons, bows and armours across the story levels found no difference in their drop permission or initial/maximum durability. Atimar's Blade is droppable with unlimited durability. Three Runeblade placements are explicitly undroppable and have unlimited durability in the source. The native modern inventory had an independent error: it displayed an external-ammo bow's durability pair as arrows and the quiver's unrelated 100 as its count. The UI now labels that pair Durability and leaves the quiver's real stack count visible. The in-game empty ammo reference resolves to 0xffffffff. The reader accepts both that and the on-disk zero.
* **10. Bow movement.** Collected and equipped the converted Long Bow and a quiver in Wartok Canyons. Three keyboard/mouse launches, including a sighted shot, consumed three arrows. With the left controller stick forward and the sight open, the game received forward movement but kept the standing animation. Over matching three-second windows Rynn moved 17 units before the fix and 6,823 afterwards. Both tests fired while sighted. The fix uses the game's normal bow walk/run transition and restores idle on release. The input recipe can drive a stick through the same input path as a real controller. Before and after logs are in `build/harness/issue12-pad-{before,after}`. A rectangular projectile was not isolated in these runs.
* **11. Inventory health.** Both modern inventory layouts now draw Rynn's current/maximum health. The UI test changes 84/100 to 94/100 while the inventory stays open. The in-game full layout shows the health bar under Rynn's portrait in `build/harness/issue12-pad-after/durability.png`.
* **14. Quiet voice.** Compared Rynn's interaction recordings with matching disc lines. Several converted clips had over 13 dB of descriptor attenuation on top of the recording. Conversion now raises quiet non-looping lines in Rynn's Comments group toward the comparable disc dialogue level (-23 dBFS RMS), by up to 9 dB. It changes the descriptor gain and keeps the original sample peaks. For example, the installed key prompt went from -1567 to -1193 millibels and the line about the mines from -646 to -150. The correction also works without a transcript. Effects, other speakers and intentionally silent descriptors retain their source gain.
* **15. Village spiders.** The tavern cellar provides an underground approach. Walking from its entrance reached the iron gate, then a separate run visited the lever and the spider room. The game handled several attacking spiders, a quicksave, a reload and a scripted exit. `build/harness/issue12-cellar-spiders/spiders.png` and `reload.png` capture this area. These checks and the Wartok cave encounters above have not reproduced the crash.

### Universal terrain verification

The rebuilt game with standard settings renders the missing north ridge in `build/harness/issue12-universal-canyon/ridge_north.png`.
The sampled canyon camera cell drew 63–64 layers from an authored list of 51. A second test over the Islands at (340000, 70000, 610000)
showed distant islands across the sea. Two interleaved pairs at 960×540 gave 245–247 world FPS with a diagnostic 32-cell cap
and 168–187 FPS with the full horizon (median frame time 3.84 ms and 5.38 ms, respectively). The bench report and frames are
in `build/bench/issue12-horizon-islands`. Both comparisons used the same executable and renderer settings.

### Awakening and starting ground checks

Sampled the full awakening every two seconds. All 73 frames are in `build/harness/issue12-awakening-camera`. Arokh's
horns, eyes and jaw keep their expected orientation in the sampled close-ups, including the head-on shots around
60–62 seconds. The run reached Wartok Canyons and exited normally.

Loaded the canyon arrival save, dismounted, and called Arokh from another spot on the canyon floor.
He flew from his original position, descended and settled at (100420, 13263, 57592), about 5,800 horizontal
units from Rynn at (95000, 12480, 59600). The positions and frames in `build/harness/issue12-summon-open`
show a return flight. How the game chooses the final landing site still needs verification.

On foot in the village, Rynn's height stayed near 19,200 along the first walk by the buildings even where the ground
looked very dark. A separate placement beside the bridge ended at the death screen. That placement does not establish
a fall through the terrain during normal play. Frames and positions are in `build/harness/issue12-village-ground`.

Fired four more shots with the collected Long Bow at the canyon weapon pile and on the ridge. The game logged
all four launches. Frames at 50 ms and 20 ms intervals show nearby enemies and impacts but do not isolate a
rectangular projectile. The two sequences are in `build/harness/issue12-arrow-flight` and `build/harness/issue12-ridge-flight`.

### Deeper cave route and subtitle distribution

In Wartok Canyons, tested the mine gate, the lower passage, the webbed spider pit, the rockfall detector and the
passage beyond it. Rynn fired among live spiders in the pit. A quicksave past the rockfall reloaded into the passage
and the 105-second run exited normally. Nine frames and a private progression save are in
`build/harness/issue12-deep-spiders`. The route used placement at the trigger locations, so continuous entry through
the mine gate and the reported crash remain unverified.

Separately played the farmer's 86-second scene from its entrance trigger. The scene moves the Mine Key from the
table to (60275, 10033, 89409); visiting its original placement before the conversation did not collect it. After
the handover, LB collected the key. At the spider mine lock, it moved to the keyhole, the door's locked flag cleared,
and the door rotated open. The run saved this progression state and exited cleanly. The handover, door and private
save are in `build/harness/issue12-farmer-key`.

The guided installer now defaults to the included English captions. The approved transcript holds 295 scene clips
and 510 timed pages. An English installation uses it offline. Setup offers local transcription as a separate choice
and remembers the selection for rebuilds. Both routes were exercised with local PC recordings. A synthetic language
test also checks that a German installation does not select the English file.

Commit `66b6a188` contains the transcript and installer choice. The default conversion from clean English media
packed all 295 clips and 510 pages across 23 levels. The opening rendered them in
`build/harness/issue12-bundled-caption-game`. Selecting local generation on an installation with a supplied transcript
instead processed 373 recordings; the resulting 23-level pack includes 293 clips and 474 pages, and a second build
reused the result. Two further recognized Rimril recordings belong to the unused Islands sequence 403,
“partial completion”. Neither Islands level names it in a placed object's “Sequence To Play” field or its class
default. The pack indexes the clips it converts, as described in the [scene sound audit](../2026-11-14/ootf-class-sounds.md).

### Walking through the unlocked spider mine

Loaded the save from the farmer and key handover. A route aiming straight at the lock caught on the doorpost.
Walking around the post and crossing through the middle of the doorway brought Rynn from (46266, 16417, 80200)
to (43368, 16043, 81893) inside the mine with ordinary movement. The script tapped Attack twice near spiders, saved,
reloaded beside a spider and exited normally. The scripted walk stopped at a timber support before the lower
passage. The input trace, both screenshots and the save/load log are in
`build/harness/live-drive/issue12-open-door-walk`. The reported crash remains unreproduced.

The controls screen now explains that tapping uses an object and holding calls Arokh. The hint follows the
active control scheme's Use command. Tests cover the default binding and its move to Triangle in scheme C.

### Rectangular arrows (10)

The bow also assumed that left mouse was assigned to Square, the Standard scheme's Attack button.
With the Alternate scheme and left mouse bound to its Attack button (Cross), three holds in the mine
produced zero aimed shots. The ordinary native attack still fired. The input now reads the active
Attack button from the game's pad mapping before starting a draw and uses that button on release.
If the scheme changes during a draw, the pending shot is canceled.

On the rebuilt game, the same three holds and releases produced three aimed shots in each of Standard,
Alternate and Dual Control. These runs used the default OotF bow mode, a Long Bow and the mouse bound to
the scheme's Attack button. All three exited normally. Compare
`build/harness/live-drive/issue12-bow-{standard,alternate}-initial` and
`build/harness/live-drive/issue12-bow-{standard,alternate,dual}-fixed`.

Loaded the canyon save with the collected Long Bow and 74 arrows. From the empty ridge at (132200, 42280, 63700),
facing 2.2 radians, a sighted shot into the sky drew a solid blue-and-black rectangle. Removing just the
projectile's Special Effect reference for a diagnostic run removed the rectangle. The source Fork Arrow Blur
is a single vertex with no polygons. It has a 128 by 128 texture with 16 opacity levels. The converter had
discarded that texture because no polygon referenced it. The game's trail uses the model's texture table
when it generates the polygons in flight. The converter now carries the texture through both import and packaging.

The Fork Arrow mesh also lacked the strip display list carried by all 32 native projectile classes with a
model in RESOURES. Converted projectiles now get that draw data. The resulting Fork Arrow has 71 triangles
and two textures. An automated test checks both the packed model and its trail texture.

Rebuilt all 23 levels and repeated the shots from the same save with the normal save compatibility check.
Three launches consumed three arrows. In the sighted shot the arrow shaft and its faint trail are visible
against the sky. Compare `build/harness/issue12-isolated-arrow/arrow_8000_0325.png` with
`build/harness/issue12-arrow-both/arrow_8000_0325.png`. The rebuilt pack kept save build `493b53de681b8791`.

### Ruined Village bridges, dark ground and missing terrain materials (1–2)

Rynn walked the stone bridge to the burned house from the river bank. Over the middle of the bridge, all 156
position samples put her origin 506–514 world units above the authored deck. She reached the opposite bank and
stopped at its fence. The earlier test had started at the bridge's corner and walked across its width.
The route and bank screenshot are in `build/harness/live-drive/issue12-village-bridge-crossings`.

At the suspension bridge near (340992, 24192, 176128), walking straight ahead dropped Rynn into the river.
The original PC mesh has two gaps across the deck at those positions. From the same bank, running and jumping
at both gaps brought her to the far bank at (340968, 24634, 187265). The two runs are in
`build/harness/live-drive/issue12-suspension-{crossings,jump}`. These tests have not reproduced a fall
through intact ground.

All 38,684 visible floor triangles in Ruined Village reference a texture present in the installed pack. None of
the 70 distinct destination textures decoded as entirely black. On the burned-house side of the bridge, the three
dark textures have mean brightness 29.3, 23.7 and 18.8 on the 0–255 scale in the PC source. The rebuilt equivalents
measure 29.3, 23.8 and 18.8. All three are opaque.

Matched Direct3D 11 and Vulkan frames show the same nearly black patch by the burned house. A 218 by
138 pixel crop had median luminance 5.92 out of 255 on both backends, with visible texture detail and no
pure-black pixels. The grass control was 75.15 on both. The authored lighting fields matched the installed
fields across all 346 Ruined Village layers. These comparisons used the same camera and default graphics
settings, with health protection during the captures. See
`build/harness/issue12-village-floor-protected-{d3d11,vulkan}` and `build/issue12-village-floor-lighting.log`.

The terrain converter did have a material discovery error. When only the right half of a cell supplied its
texture, the geometry converter used it but the texture collector could omit it. Both now use the same rule,
including for shoreline cells and steep slopes. Across the 23 converted levels, fallback terrain cells dropped
from 19 to zero. Eight were in Succubus Lair and eleven were in five multiplayer levels. In the before/after
runs (`build/harness/issue12-halfcell-{before,after}/cliff.png`), the ledge in the cleft regains its rock pattern.

### On foot from the mine entrance to the lower passage (15)

From the save inside the gate, using the mine's walls and timber supports to plan the walking route got Rynn
past the post that stopped the previous run. The session returned to the entrance save once during the first
spider encounter. On the next approach Rynn walked to (34103, 13971, 92703), among the spiders roughly
14,000 world units farther along the route. She saved and reloaded there and the game exited normally. Movement,
two screenshots and the save/load log are in `build/harness/live-drive/issue12-support-mine`.

Rynn then reached the pillar at the mine chasm. Equipping Atimar's Blade and striking the upright pillar
toppled it across the gap. With normal damage enabled, she ran along the pillar, jumped from its far end
and landed on the opposite shelf. Quicksave and quickload returned her to (23176, 13740, 89374) on that shelf.
The input trace, pillar and landing screenshots, save/load log and clean exit are in
`build/harness/live-drive/issue12-mine-pillar-jump2`. Walking into the chasm on an earlier attempt produced
the in-game death screen.

From the far bank Rynn walked to the ambush, descended past attacking spiders and entered the webbed lower
chamber. The game completed three quicksaves during the descent. The screenshots `depth_2.png`,
`depth_3.png` and `depth_4.png` in `build/harness/live-drive/issue12-mine-descent` show the successive
areas. Loading the final checkpoint started a new session in the pit. Rynn climbed to the upper passage
before falling back to the lower level in `build/harness/live-drive/issue12-mine-climb`; the game still
accepted input and exited normally.

Following the wider centre of the upper shelf from the same pit checkpoint took Rynn through the rockfall
area to the passage beyond it. She saved at (24634, 8920, 116888), reloaded at (24633, 8935, 116899)
and exited normally. See `build/harness/live-drive/issue12-mine-wide` for the input trace, the two passage
screenshots and `restored_passage.png`. The linked checkpoints now cover the route on foot from the
unlocked gate to beyond the rockfall with normal damage. The reported application crash did not occur.

### Target selection against moving enemies (6)

Free mouse combat's settings description previously told every player to press F or middle click to
lock on. Those are Standard defaults. The description now refers to the current Toggle Targeting entry
in Key Bindings. The generated settings comment also reflects that an explicit target lock takes
precedence over free mouse combat.

In the cave, using Alternate's R1 and Q toggled the selected enemy on 13 consecutive attempts after a
spider came within reach. Dual Control's Cross and C did the same on 12 consecutive attempts. Before
then, the spider Rynn faced was 9,621 units below her. The other nearby spider was still off to one side.
The native target and camera lock flag changed together. See `build/harness/live-drive/issue12-lock-scheme-alternate`
and `build/harness/live-drive/issue12-lock-scheme-dual`.

At the mine pillar, tested 30 presses using Standard controls with R2, F and the middle mouse button.
The input trace includes the native pad bit and the player’s selected target. The first three presses left
the selection empty. Rynn was facing toward spiders over 9,600 units below her, and the approaching spider
on her ledge was 78–82 degrees off her facing direction. Facing that spider selected it on the next press.
All 27 subsequent presses, including short taps, alternated between a live target and no target.
This input test used the health lock. See `build/harness/live-drive/issue12-moving-spider-lock`.

In a separate fight with normal damage, six presses of Next Target alternated between two attacking spiders.
When a sword hit took the selected spider from 5 HP to −3, the game automatically selected the other living
spider. The before/after target references and enemy health are in
`build/harness/live-drive/issue12-target-after-kill/drive.jsonl`. An intermittent failure with an eligible
target in front of Rynn remains unreproduced.

### Cutscene music and dialogue (12)

The campaign runtime was discarding the game's half-volume music fade at scene entry. It now follows
that fade and remembers the underlying music level across overlapping scenes. A cue that keeps its track
used to suppress the level restore as well. It now keeps the track and restores the mix at the end.
The converted nine-second stop still ended the music before the old man's conversation.

In the same Ruined Village opening, the previous executable kept music-category attenuation at zero.
The rebuilt game reached 0.5 during the dialogue and returned to zero when control returned to Rynn after
the chained scenes. A separate quickload from the village into Wartok Canyons also restored the gameplay mix.
The game and mixer traces are in `build/harness/live-drive/issue12-scene-mix-{before,verified,load}`.

Compared the mixed speaker output with the source voice and decoded music in 14 voiced sections of
Rynn's first line to Delon. The median voice gain was 0.121 at Music Volume 0, 0.122 at 25 and 0.122
at 100. At 25 the voice measured 9.8 dB above the score across those sections. At 100 it measured 3.6 dB
above the score. These are measurements of the opening line. The three local recordings are
`build/issue12-score-{0,25,100}.wav`.

### Pickup at full inventory (7–9)

Collected placed Wartok Canyons items until all 24 inventory cells were occupied. The setup used placement
to travel between pickups and kept Rynn's health full. Loaded that private save with normal damage to test
the Heavy Bow and arrows on the cave floor. The bow stayed at (239573, 27862, 308770) throughout three L1
presses with the inventory full. The objects moving beside Rynn were her equipped Long Bow and arrows.
Both handles were already present in her inventory before the presses.

Two more items at that pile share the same shelf. With inventory space, the Fire Crystal and red potion
each entered the grid as the same object that had been on the ground. With all cells occupied, the
crystal stayed at (241962, 27877, 309013) through the L1 presses and after Rynn left. The red potion
joined an existing stack: its count at item data +0x2c rose from nine to ten and all cells remained
occupied. These tests used a private arrival save and full-inventory save; health protection allowed
repeated pickup attempts near the enemies. See `build/issue12-loose-objects/runs/{space_close,full_words}`.

Dragon weapon and health power-ups have a separate authored rotation. The eleven story weapon power-ups
specify 0.2 revolutions per second and the dragon health power-up specifies 0.5. In Wartok Canyons,
tracked the Poison Gas Quickening Power Up while Rynn approached it on foot and pressed L1. Across
82 samples its orientation changed, its position stayed at (289792, 34000, 281613), and it never entered
Rynn's inventory. The closest sample put Rynn 18 units from the power-up. See
`build/issue12-loose-objects/runs/power`.

Away from the pile, dropping the Long Bow freed three cells. When Rynn returned, the same Heavy Bow
handle (resource 0x343ea) entered the grid before another L1 press. The new bow occupied the three free
cells. The item positions, grid contents, drop and return are in
`build/harness/live-drive/issue12-full-item-track`. This test did not reproduce loose items orbiting Rynn.

Comparing the source and installed object data for all 23 levels covered 125 placed melee weapons,
bows and suits of armour (97 story, 28 multiplayer). Drop permission, initial durability and maximum
durability matched on all 125. All ten placements with two zero durability fields have that pair in the
source. Three are Runeblades with dropping disabled. The seven others allow dropping.

### Ground near the village and repeated cave loads (1, 15)

Around Rynn's starting position, selected dry, visible floor cells on both sides of terrain edges where the
original surface was continuous. Each trial placed Rynn just above one side, then used normal forward
movement. At 63 sites, all 3,204 position samples that could be compared to the original floor kept Rynn's
origin at least 315 units above it. At 30 sites she walked at least 128 units onto each side of the edge,
including 24 boundaries between separately stored terrain layers. Two placements missed their intended
sites. Results and positions are in `build/harness/live-drive/issue12-village-layer-crossings`.

In a separate mine stress test, loaded a save near two approaching spiders, attempted to target and fire,
stepped sideways, then quickloaded the same private checkpoint. All 12 cycles returned to playable Wartok
Canyons, with live spiders and the player back at the checkpoint. The game exited normally. The native
load log and positions are in `build/harness/live-drive/issue12-cave-fight-reloads`.

### Calling and riding Arokh after the awakening (4–5)

Starting mounted at the Wartok Canyons arrival, Rynn dismounted and walked a 47-waypoint route with
normal damage enabled. From roughly 21,000 units away, holding L1 redirected Arokh's flight toward
her. He came down 6,476 horizontal units away, and she walked to him and remounted. Neither actor was
repositioned during the run. The route and positions are in `build/harness/live-drive/issue12-walk-call-remount`.
A second run ended when Rynn died while waiting.

Repeated the walk with health protection to measure the controls after remounting. With W already held,
Arokh traveled more than 100 units after 3.16 seconds and covered 6,796 horizontal units before stopping
on rising ground. In a separate repeat he responded to a turn, switched into flight with Triangle and
traveled 7,462 horizontal units in four seconds of forward flight. Both runs exited normally.
See `build/harness/live-drive/issue12-remount-control-hold` and
`build/harness/live-drive/issue12-remount-turn-takeoff`.

Long-range calls remain open. In two diagnostic runs, Rynn was placed in a distant meadow after
dismounting, with health protection so enemies could not end the comparison. Over three minutes Arokh
approached even without a call. With L1 held to call him, he came within 14,958 horizontal units and
ended 6,251 units above Rynn. Neither run produced a mountable landing. These runs skipped the route
between the two points. The traces are in `build/harness/live-drive/issue12-canyon-call-meadow-held-protected`
and `build/harness/live-drive/issue12-canyon-call-meadow-silent-protected`.

Two instrumented repeats explain why moving Rynn instantly gave a poor result. At about 88,000 and
322,000 units, Arokh kept updating and recognized all three calls. His flight path still began with
Rynn's position from before the move, followed immediately by her distant position. He circled the
first point while the intervening terrain blocked the direct route. Moving Rynn back near the canyon
entrance eventually cleared those points. The traces are in `build/harness/live-drive/issue12-flight-calls-{meadow,snow}`.

On a separate walk from the canyon arrival, terrain stopped Rynn partway along the planned route.
Arokh followed her through the canyon, then closed from 14,490 to 3,219 units after her call and she
remounted. She continued on foot from the checkpoint in another run. The game tracked her walked path
and Arokh came within 2,131 horizontal units at its end. For this second run use the `airtrail.jsonl`
samples, which resolved the active actors after loading. The planned route to the distant meadow
was not completed on foot. See `build/harness/live-drive/issue12-canyon-ridgewalk` and
`build/harness/live-drive/issue12-canyon-walk-continuation`.

### Summoning from water and dry ground (4–5)

From the mine's second entrance, Rynn called Arokh and walked to him to remount with normal damage.
A longer walk from the same checkpoint used health protection. Rynn slipped into shallow water beside
a cliff. Arokh answered but finished nearly 8,000 units above her. She walked directly under him and
could not mount. The runs are in `build/harness/live-drive/issue12-mouth-return-normal` and
`build/harness/live-drive/issue12-mine-mouth-northwalk`.

With normal damage, Rynn waded from that checkpoint to an open bank and called from 17,786 horizontal
units away. Arokh landed 4,554 horizontal units from her and 310 units above her. She walked to him and
remounted. Neither actor was repositioned during this run. See
`build/harness/live-drive/issue12-reach-dry-bank`.

The three calls from the diagnostic meadow position lost their explicit call flag after 20.31, 20.33
and 20.27 seconds. Arokh was still 73,489 to 94,030 units away. The native call state starts a
20-second timer and clears the call flag when it expires. The meadow test also left a flight path
point at Rynn's pre-placement position. It does not establish whether additional time would let
Arokh land somewhere Rynn could reach.

### Rynn's voice in the gameplay mix (14)

The game's Sound Volume controls both dialogue and effects. On the default voice setting, Rynn's
dragon call played the disc recording 0x2018f. With music muted and Sound Volume at 25 and 100,
the same 200 ms of her call measured gains of 0.0408 and 0.1633 in the mixed output. Against the
other sounds in that window, her voice was 6.2 and 6.0 dB louder, respectively. Sound Volume 0 muted
the output. Raising the master made the call louder while preserving its balance against the effects.

Four separate story triggers in Wartok Canyons use converted recordings even with the default voice
setting. Placed Rynn at the detectors to trigger the lines in the game and recorded the mixed stereo
output. A private comparison pack restored just the four authored descriptor volumes. At Sound Volume
25 with music muted, the following changes were measured by aligning the decoded voice to each recording
and estimating its gain in matching 200 ms windows.

| Gameplay line | Original gain | Corrected gain | Change |
| --- | ---: | ---: | ---: |
| Mine entrance (unchanged descriptor) | 0.0571 | 0.0548 | −0.4 dB |
| Grimstone | 0.0505 | 0.0611 | +1.6 dB |
| Wartok's directions | 0.0552 | 0.1116 | +6.1 dB |
| Mines further up | 0.0581 | 0.1023 | +4.9 dB |

All four clips were also identified in a separate playthrough at Sound Volume 50 and Music Volume 25.
The captures and per-line results are in `build/issue12-gameplay-voice`.

### Triggering the spider mine collapse (15)

The wide shelf lets Rynn pass underneath the small detector above the barrel seesaw. On the walk
from the spider pit to the far passage, all eight boulders stayed at their starting heights. That
route verifies traversal. Testing the collapse itself requires entering the elevated detector.

The cave recipe now visits both spider groups, captures the world state, then places Rynn in the
detector at (28976, 10561, 111316). After the explosion, it captures the state again. Run the recipe
with the command in the Spider cave section above, then run the check on its output:

```powershell
python scripts/harness/ootf_spider_cave_check.py build/harness/issue12-cave-encounter
```

In `build/harness/issue12-cave-collapse-recipe` on Direct3D 11, the detector disappeared and the same
eight boulders fell between 2,519 and 3,686 units. The game exited cleanly. Passing the pre-event
capture to the check twice failed because the detector was still present. A separate Vulkan run from
a private progression save also activated the collapse and exited without a device loss; see
`build/harness/live-drive/issue12-cave-fall-trigger`. In another Vulkan run Rynn walked past the
second entrance's cave-audio triggers in both directions and reloaded outside. This check used health
protection and one placement outside the entrance. The reported application crash remains unreproduced.

Rynn also activated the seesaw through ordinary gameplay. From the spider-pit save she walked up
the passage with normal damage, jumped onto the tall crate (standing height 9,798), then jumped toward
the seesaw. She reached height 10,459, consumed the detector, and all eight boulders left their
starting positions. The Vulkan game and renderer exited cleanly. This run used no repositioning or
health protection. Its inputs, positions, object snapshots and final frame are in
`build/harness/live-drive/issue12-cave-seesaw-pad`.

A separate Direct3D 11 run quicksaved during the delay between the seesaw firing and the rocks moving.
The detector had disappeared while all eight boulders were still at their original heights. After the
collapse finished, Rynn quickloaded that save. Each boulder reached the same final height as in the
uninterrupted collapse, and the application exited normally. The five state captures and save/load
log are in `build/harness/issue12-save-during-collapse`.

### Status of the 15 reports

| # | Report | Local result |
| --- | --- | --- |
| 1 | Black ground and falling through the starting area | Fixed missing terrain materials in conversion (`ddd72934`). The affected cells were in other levels. Village textures matched the source. The falls reproduced at gaps in the suspension bridge. A fall through intact village ground remains open. |
| 2 | Missing canyon terrain | Fixed the draw distance for converted levels using the active fog horizon. Verified in Wartok Canyons and Islands (`f3c7d3a5`). |
| 3 | Upside-down face in the awakening | The full scene played with Arokh's face upright on the current build, which includes the earlier scene animation fixes. |
| 4 | Arokh freezes after acquisition | Verified movement on canyon arrival, dismount, remount and flight. A persistent freeze remains unreproduced. |
| 5 | Arokh fails to come when called | Corrected the tap/hold guidance (`cdf1dfab`). Arokh returned and could be remounted on walked routes, including after Rynn waded to a dry bank. The 20-second call expiry and long-range landings across intervening terrain remain open. |
| 6 | Inconsistent target lock | Corrected the scheme-specific guidance (`79ad478c`). Valid presses selected enemies on Standard, Alternate and Dual Control. An intermittent failure with an eligible target remains unreproduced. |
| 7 | Pickups orbit Rynn | With a full inventory the Heavy Bow and Fire Crystal stayed on the ground, while a potion joined an existing stack. A dragon power-up rotated at a fixed position. The orbiting pickup remains unreproduced. |
| 8 | Equipment cannot be dropped | Drop flags matched the source on all 125 equipment placements checked. Three Runeblades are protected. Ordinary weapons and armour were dropped in game. |
| 9 | Zero durability on acquisition | The zero-durability placements also have zero in the source. Corrected the inventory's handling of bow durability and quiver counts (`87b67b17`). |
| 10 | Rectangular arrows and frozen bow movement | Fixed projectile textures (`4a1542ad`), sighted controller movement (`3e7baeee`) and hold-to-aim with remapped Attack (`0c136b30`). Verified projectile rendering, movement and aimed shots in the game. |
| 11 | Missing inventory health | Added current and maximum health to both inventory layouts. Verified that drinking a potion updates the display (`98480e4b`). |
| 12 | Music obscures dialogue | Fixed the campaign volume curve and lowered music through dialogue. Verified the mixed recording and the return to gameplay volume (`b71075df`). |
| 13 | Missing subtitles | Bundled English subtitles and retained local transcription in setup. Verified both conversion routes and in-game captions (`66b6a188`). |
| 14 | Quiet gameplay speech | Raised quiet Rynn remarks in conversion. Measured the difference in four gameplay lines, including an unchanged control (`7a6af045`). |
| 15 | Spider cave crashes | Completed the mine route, fired the seesaw collapse and reloaded a save during the event. The regression checks actual boulder movement (`5fb80c57`). The application crash remains unreproduced. |

### Ruined Village floor on Direct3D 11 and Vulkan (1)

Compared the same village floor patches at 960×540 with the installed textures and graphics settings.
Both renderers used the same RTX 5080. Rynn took damage during the first burned-house comparison, so
the replacement pair used health protection on both backends. Camera positions for the four matched
views below differed by at most 1.62 world units. Camera axis angles agreed to 0.001 degrees.

| Floor sample | Median luminance on Direct3D 11 | Median luminance on Vulkan |
| --- | ---: | ---: |
| Starting area | 21.12 | 21.12 |
| Approach to the stone bridge | 59.03 | 59.03 |
| Scorched ground beside the burned house | 5.92 | 5.92 |
| Open grass on the near bank | 75.15 | 75.15 |

Values use Rec. 709 luma weights on the captured RGB bytes (0–255). The same 218×138 region of
scorched ground held 330 distinct colors on Direct3D 11 and 332 on Vulkan. No pixel was pure black.
The average absolute RGB difference between the two crops was 0.10. The dark ground and its sharp
edge were visible on both backends. All 346 Ruined Village terrain layers retained their source
light direction, ascension, color, ambient color and dropoff in the installed pack.

The screenshots and camera captures are in `build/harness/issue12-village-floor-{d3d11,vulkan}`
and `build/harness/issue12-village-floor-protected-{d3d11,vulkan}`. The reporter's exact floor
patch is unknown.

### Spider-cave retest on 22 November 2026 (15)

The Windows build replayed the seesaw collapse in Wartok Canyons. The detector fired, all eight boulders fell
2,519–3,686 units, and loading the save taken during the event returned to gameplay. The run quit cleanly after the
reloaded rockfall. See `build/harness/issue12-cave-followup` and the maintained `ootf_spider_cave_check.py`.

The crash report still has no save, exact landmark or crash log beyond the original description.

### Long-range Arokh calls on 22 November 2026 (4–5)

The PAL dragon's approach tick expires the active call after about 20 seconds. An explicit Order of the Flame call
for Rynn's linked dragon now receives one timed extension if Arokh is still more than 20,480 horizontal units away
late in the approach. The extension is based on the remaining distance and is capped at 60 seconds. The game still
selects the flight path and landing site. Nearby calls and automatic following keep their existing timing.

In a Wartok Canyons replay, the initial separation was 88,366 units. The harness placed Rynn at the valley while
Arokh was still following the original point. The extension fired once at 55,441 units. The explicit call remained
active until 22.8 seconds after the input, beyond the original deadline. Arokh landed at 27.5 seconds, 2,581 units from
Rynn, and she remounted through normal input. A shorter meadow check also ended in a remount without extending the
timer. See `build/harness/live-drive/issue12-moving-call-deadline` and `issue12-meadow-delayed-after`.

The budget applies while the dragon is active in the level and cannot move him across blocked terrain or wake an
actor outside the engine's activity area. A new report can use the two positions and a save to distinguish a path or
activation problem from a call that expires in flight.
