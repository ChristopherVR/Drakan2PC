# Wartok Canyons and Modern UI reports

## Confirmed paths

The installed PAL build reproduced the mounted primary-fire effect in the cave near
`(212256, 22800, 254976)`. Holding S and left-click produced a fire ring and expanding
shrapnel in front of Arokh. The trace identifies the same impact position for the
main burst, shrapnel and ring. The near-camera filter now handles all three visual
resources at their respective impact call sites. The projectile and its damage
processing still run. The test checks the three resource matches, the distance
limit and unrelated resources. A rebuilt in-game comparison is pending.

Holding E for two seconds on foot in the spider cave played the stock
“Arokh can't hear me” line. Holding the corresponding L1 input also played it.
In the open canyon at `(95000, 12480, 59600)`, holding E triggered Rynn's stock
“Arokh” call. These initial checks establish SPU2 key-on only. The output-mix
retest below measures how much speech reaches the final audio.

The modern save screen was deriving the displayed shape of an HD picture from
the pixel proportions of a square texture made for the retail 7:3 aperture.
It now compensates for that aperture and fits the corrected aspect into the card.
Native thumbnails use their existing 4:3 display aspect.

The native Game and Music sliders now commit on mouse release. Modern Settings
avoids replaying slider feedback during the drag. PAL builds also expose Voice
and Effects volume. Native music and interface categories keep their existing
controls. Imported clips without speech metadata may use the Effects setting.

The Windows reporter pre-fills OS, CPU, RAM and graphics adapters. It produces a
ZIP with the log and independently opted-in campaign saves and last gameplay
frame. The synthetic archive test used the production ZIP path to verify both a
log-only report and one with a save, save metadata and a screenshot substitute.

## Still open

| Report | Current evidence |
| --- | --- |
| Cave water flicker and a line at the right edge while rotating | Mounted camera sweeps next to the cave pool show water in both the native and Modern Water runs. The sampled frames show no distinct vertical line along the right edge. |
| Visible water seam in the supplied screenshot | Surveyed six outdoor Wartok positions near water. Tested one terrain-layer boundary beside placement 167 on foot and at water height. The terrain stripe in the report remains unmatched. |
| Secondary flame should break Wartoks into pieces | Reproduced the missing breakup on the cave Wartok. Its native class has empty breakup generator arrays. Pack probes with candidate generators loaded, but the game resolved both candidate references to `0xffffffff`. |

The standalone settings and game UI tests passed (32 passed, two data-dependent
skips). The changed Windows runtime translation units also passed MSVC syntax
checks. The incremental play build has no prepared `build/final-game` in this
checkout. The root configuration could not fetch the pinned WebGPU dependency
from GitHub. The installed player was used to reproduce symptoms only.

## Final audio mix and original Audio controls

With sound volume 56, music volume 13 and output gain 100%, the session's
rendered stereo PCM contained Rynn's actual stock call. The open-canyon
keyboard call measured about −31 dBFS RMS and the two cave responses about
−36 dBFS RMS. These estimates come from matching the decoded speech waveforms
to the output around their SPU2 start frames. The cave line was the same for E
and L1 in this replay. WASAPI opened the default Realtek device. Device,
speaker and room audibility are separate from the rendered PCM measurements.
Order of the Flame now gives the stock call and refusal lines up to 6 dB more
gain. The native master and PC Voice volume can still attenuate or mute them.

The original volume controls are fill bars. They lack the native draggable-thumb
flag, and part of their visible artwork extends beyond the nominal hit rectangle.
On the installed build, a click on the lower Master bar produced no hit and a
Music drag applied a single 25-point page step, leaving Music volume at 38.
The input code now maps pointer position to the drawn fill track. It updates the
bar during the drag and posts one native apply on release. The regression test
covers clicking outside the old hit rectangle, dragging across the row and
outside the client, releasing during another guest call and losing focus.

The imported Long Bow used automatic preview axes and appeared edge-on in both
inventories. A copy of the campaign pack with explicit Y-up and X-side axes
showed a full bow silhouette in the grid and detail panel of both inventories in
the installed game. The loader now applies those axes to matching older OotF
class records at load. The pack compatibility test passed and the game continued
to load a save with the original pack's compatibility stamp.

## Duncan's Battle Axe and Rune of Stone from the player's quicksave

The current Wartok Canyons quicksave starts mounted. After dismounting, Duncan's
Battle Axe in cell 2 had 0/150 durability and allowed dropping at both the item
and class levels. In Modern Inventory, both X and a click on Drop removed it.
The original Equipment footer showed Space for Drop. Space left the axe in the
grid, and Q removed it. The footer was drawing the shared gameplay Triangle glyph
while the native action used the menu bindings. The original footer now displays
the configured keyboard Drop key. The player quicksave and test input copy had
matching hashes after the replays.

The same save has the Rune of Stone in cell 18. With the old Y-up and X-side axes,
its overlapping surfaces made the preview appear flat. The X-up and Y-side probe
displayed a triangular silhouette in the Modern grid and detail panel. The pack
loader now applies those axes to the matching imported model.

The Windows Direct3D 11 renderer built with WebGPU disabled and no WebGPU download.
The full local player build then reached the recompiler's ELFIO fetch, where the
local Git process failed to resolve the GitHub host. The keyboard prompt change
passed a Windows compiler syntax check. Live tests used the existing executable.

## Follow-up on the cave, river and flame

Direct-started the level for both cave tests and placed mounted Arokh at
`(211926, 24550, 253700)` or `(212256, 22800, 254976)`. The first run took 24
samples while turning with native water and 24 after enabling Modern Water. The
second turned with the right stick under native water. Arokh's wings sometimes
crossed the pool in the first run. Neither the camera samples nor a thin-column
scan of the first run's right edge isolated the reported line.

The installed pack has 105 water layers and 2,423 textured water cells across
Wartok Canyons. Among adjacent textured cells from different layers within
10,000 units of a placed Leather Wartok, the scan found one shared edge whose
vertex heights differed by 8–3,000 units. Layers 307 and 196 meet near
`(130048, 217088)` with a 52-unit difference at one vertex. An on-foot and swim
replay there showed continuous water across the boundary. That scan does not
cover a full untextured cell between sheets or try every camera position.

The native cave encounter near `(211926, 24550, 253700)` went from full health
through 44 and 2 HP to -79 HP over two bursts of Arokh's secondary flame. There were
no Wartok chunk handles in the sampled registry. The creature uses NPCS class
5174, with a signed cutoff of -60 and empty large and small breakup-generator
arrays. All six native Leather, Chain and Plate center and flanker classes share
those defaults. The campaign already loads a candidate Wartok generator and four
chunk classes from RESOURES.ODB. Substituting either that generator or the disc's
small-chunk FX generator in a copy of the creature class gave populated arrays
on disk, but the game stored the unresolved reference value in RAM. Those
probes are kept out of the production pack. Successful reference resolution and
native breakup on flame damage are separate remaining checks.

The local build now retries a fetch when a previous attempt left an initialized
dependency checkout without a HEAD. An offline Git bundle verified recovery and
reuse of the completed checkout. A dispatch test also verified rejection of an
unrelated checkout. The normal incremental game build reached ELFIO again and
hit the same GitHub DNS failure. The existing play executable and player save
were used read-only.

## Fatal secondary flame

The first pack probes used one array payload for both breakup fields. The class
loader resolves each field in place, so the second field tried to resolve an
already translated index. A fresh pair of array payloads resolved the imported
Wartok piece generator. Changing those fields alone left the body intact after
an Arokh burn kill. The native damage handler takes the normal NPC death path
on the burn message.

An explicit native Gib message sent to the same Wartok in a private level
fixture invoked the imported generator and successfully created all four
Wartok chunk classes. A separate run on the installed pack confirmed that the
fatal secondary flame tick called the death handler with that flame's burn
effect as its source. The NPC's health went from 8 in the preceding sample to
-34 in the following sample.

The runtime now queues the imported generator after a fatal secondary-flame
hit on an Order of the Flame leather, chain or plate Wartok. It uses the native
actor state change from the Gib path only after the effect is created. The
guest dispatch check, compiled with MSVC against a private RAM snapshot,
covered a fatal tick, a nonfatal tick, repeated damage to a corpse, other
attack sources, transition, and generator failure. The full player build still
stops when Git cannot resolve the ELFIO remote. The updated player has yet to
be run through a live kill.

## Mounted entrance pool

Three mounted sweep positions over the entrance pool produced 78 screenshots
with native water and 78 with Modern Water. The pool remained visible or was
occluded by scenery or Arokh from those viewpoints. A scan of the final 80
columns of each frame found no sustained single-column color spike. This did
not reproduce the reported flicker or right-edge line.

A follow-up on the installed pack captured 124 frames at 2048 × 1303 from four
starting headings over the pool. The biggest changes in the blue water area
coincided with Arokh's wingbeats. The same right-edge scan found no sustained
single-column spikes in those captures.

## River below the rope bridge

The reported strip of exposed riverbed reproduced from the bank near
`(183600, 13100, 158800)`. Projecting the water grid onto the capture located it
at the boundary of layers 213 and 214, around `z=172032`. Their common vertex
heights matched in both the level data and live memory. Raising the water by
256 units and sharing the boundary vertex masks in separate probes left the
strip visible. Joining the two layers made the same crossing render as a
continuous surface. The converter keeps the previous layer indices and redirects
visibility references to the joined surface.

The level generated by the converter fix matched the successful gameplay
probe byte for byte. A copy of the player's quicksave loaded with the updated
pack. Rynn crossed the seam while swimming from about `z=170694` to `z=177606`.
The play installation was then updated through the private pack installer.
The original pack was backed up and the installer verified its save stamp.
The quicksave hash was checked before and after the update.

The formats package type-check and its 26 focused conversion tests passed.
The full formats run passed 811 tests and failed one unrelated reference-engine
coverage assertion in `ootfcoverage.test.ts`.

## Offline Windows build and in-game follow-up

The installed builder kit supplied a compatible PAL translator, raylib and
FFmpeg. The translator generated all 12,173 functions. The optional Windows
Ninja build uses the same MSVC compiler and stages the game through
`Build-LocalGame.ps1`. This bypassed the Windows SDK registry lookup in MSBuild
and built the Direct3D 11 player without a WebGPU download. The deployed
campaign pack and the original Wartok quicksave kept their SHA-256 hashes.

A live secondary flame kill queued the correct Wartok and the factory created
all four piece actors. The posted guest call ran the generator inline and
returned zero. The corpse remained in the first rebuilt player. The completion
hook now recognizes successful child creation from that same generator call
before applying the native corpse removal.

The earlier mounted Wartok manual save starts by cave layer 392 at
`(76810, 10835, 13961)`. Its camera replay reproduced the right-edge line at
3840 x 2160 with both DLSS and Modern Water off, with either enabled alone, and
at 3397 x 2032 with both enabled. At 3840 x 2160 and GS scale 1 the line did not
appear. The native microprogram path also reproduced it at high scale. Draw
bounds on the 512-pixel world target frequently end at `511.9375`, precisely
the final pixel centre at scale 8. With the rasterizer's right-edge fill rule,
those triangles can leave a column showing the background. The shared world
triangle path now restores coordinates within two GS subpixels of the right
viewport edge to the boundary. HUD sprites and tighter scissor rectangles do
not enter this path.

The player quicksave copy also confirmed that pressing the displayed original
Equipment drop key, Q, removed Duncan's Battle Axe at 0/150 durability. A
separate original-menu replay dragged both Audio sliders and saved the new
values. The formats reference-only table now omits the implemented Expanding
Fire Ring. All 812 formats tests passed with a 20-second timeout for the
large truecolor texture fixture, which exceeded its default timeout during
the concurrent build. The settings and input suites passed 41 tests with two
data-dependent skips.

At commit time, the latest flame and right-edge changes had compiled, but the
terminal interruption left the final link incomplete. The installed player is
the previous rebuild, which was used for the axe and slider checks. Both new
fixes still need deployment and gameplay verification. The separate cave-water
flicker remains open. The mounted replays did not isolate a water dropout apart
from scenery and wing occlusion.

## Rebuilt player replays

The resumed Windows Ninja build linked and deployed through `Build-LocalGame.ps1`.
The executable SHA-256 is `87F5FFFBB8723FFAFC1D338A79D1D4921B3C23DE45BDD28EBCB7BFBB31933F75`.
The original campaign pack and quicksave matched their earlier checksums.

The secondary flame replay created all four stock Wartok pieces, then called
the native actor disable routine on the original body. The separate primary
fire replay while reversing suppressed the near-camera burst, ring and
shrapnel on two impacts. The primary fireball still followed the native
projectile path. Both replays exited normally.

The mounted entrance replay captured 46 frames in each 3840 x 2160 mode.
The earlier native control had a discontinuity along 1,615 pixels of the last
column in one frame. The corrected native replay and the corrected DLSS
Quality and Modern Water replay showed no sustained one-column discontinuity.

For the separate flicker report, the mounted approach and hold at the lower
bank captured 96 settled frames per mode at 1280 x 828. In the native replay,
the largest apparent loss of water color was frame 83, where Arokh's wing
crossed the foreground. The other detected flashes comprised isolated pixels
or groups of seven pixels or fewer. With Modern Water and ray-traced
reflections, the largest connected change in the same comparison was one
pixel. A separate dropout in the water surface remains unreproduced.

Holding E on foot played the stock summon in the open canyon and the refusal
inside the spider cave. Waveform matching against the game PC's stereo output
recording measured -24.64 dBFS RMS for the summon and -30.09 dBFS RMS for the
refusal, about 6 dB above the earlier recordings. The matched speech was
present in the final mix sent to the audio device.

## Far-bank water flicker

A denser 1280 x 828 replay walked forward and turned from the earlier mounted
cave save. After releasing W, it requested screenshots at 17 ms intervals.
A strip of visible blue water by the far bank disappeared and returned over
successive screenshots. Arokh's wing stayed clear of the affected strip. The
largest connected strip covered about 520 pixels with original water. The
Modern Water replay with DXR reflections showed the same pattern. Both runs
captured 210 screenshots after releasing W.

A separate draw capture near this camera angle found blended water and opaque
terrain within two GS depth units of each other at a shoreline sample. The
existing water depth adjustment kept that sample stable. That capture missed
the visible flash. It confirmed the depth tie at the neighbouring angle.

The terrain audit then found eight fully textured cells in water layer 393
whose four corner heights match the floor on layer 392 exactly. The PS2
subpixel rounding and different water and floor triangle edges can produce
different raster depths for those coplanar surfaces. The first trial increased
the margin in the blended water pass from about 16 GS units to 2,075 at the
sampled depth of 8.5 million. The pass keeps depth writes masked.

The synthetic renderer fixture places one water sample 512 GS units in
front of the shore and another 16,384 units in front. With the previous
margin the first pixel turned blue. The adjusted pass keeps the first
pixel brown and the second blue at internal scales 1 and 2 on WARP.

The rebuilt player reproduced the camera angle from the original-water
capture and kept the far bank stable in all 210 screenshots. The dry terrain
matched within a mean 0.12 RGB levels in the comparison crop, and the open
water sample contained 4,129 blue pixels versus 4,131 in the baseline. A
second 210-screenshot replay used Modern Water with DXR reflections active.
In both trial replays the largest connected one-frame water changes were
isolated pixels. The trial executable SHA-256 is
`45EBBA78B0D5A1EA02843C294D1499B1886D250D33811E353BC91C38AF66B620`.
The original Wartok quicksave still matched its earlier checksum.

A wider turn reproduced a smaller 41-pixel flash at the far bank with the
first margin. The second trial allows about 4,150 GS units at the sampled
depth. Its synthetic fixture covers a 2,304-unit separation at the bank and
uses the same 16,384-unit separation for the open-water control. Both
controls passed at scales 1 and 2. With the final margin, the largest
connected one-frame change detected in the 200-frame turn was two pixels.
The matching original-water replay and the Modern Water replay with DXR
showed connected changes of at most six and two pixels, respectively,
over 210 screenshots each. The deployed executable SHA-256 is
`F94A67DDD805F307C921F684F97B988565260136A960556643282B1702B7EAB7`.
