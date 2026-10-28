# Gameplay HUD under the pause menu in widescreen — 2026-10-24

Symptom: when the pause menu (0x17) opened, the health and mana orbs jumped from the screen corners onto the centred
4:3 menu canvas at full size and were clipped at its edge.

## What the game draws

Draw log of a pause (`build/harness/pause-hud-slide`, `GSR_DRAWLOG` over the whole run, frames f5755..f5890). After
EndWorld, each paused frame draws in this order: the late perspective water packet, the gameplay HUD sprites, then
the pause page. The page begins with a centred dimming sprite (154..358 x 128..384, black, alpha 64), followed by the
frame and the text atlas `0x3000`. The HUD uses the textures `0x3dba`, `0x3f35` and `0x3f79`; `0x3f35` and `0x3f79` are
also used by the page's dimming sprite. On the first paused frame the air gauge (462..488 x 32..142) disappears. The
orbs slide 10 native pixels per frame diagonally out of the corners (left orb -x +y, right orb +x +y) for about 6
frames, then stop being drawn. On resume the HUD is drawn again, still under screen 0x17 for a frame, and slides back
in.

## Cause and fix

The host turns `nativeMenu` on for 0x17, so `hud::menuSubmission` claimed every post-world draw. The HUD sprites went
through `menuMap` (4:3 squeeze, page clip) instead of the gameplay corner/cluster maps. Under any menu the host also
set the HUD size to 1.0.

- gs: `hud::pauseHudDraw`. Under 0x17, until the first page draw centred in the middle 40% of the frame width,
  draws centred in the outer bands are not page draws. They take the gameplay corner/cluster path, so they get the
  same corner maps as in gameplay, including off-screen pieces during the slide. `pausePageStarted` is reset at
  each world phase marker and EndWorld.
- host: `detail::gameplayHudSize` keeps the gameplay `hud_scale` under 0x17. Title, options, loading and message
  boxes still lay out at size 1, and the menu path itself ignores the HUD size.

## Follow-up: page drawn twice on the resume frame

`build/harness/verify4-pause/r02.png` (2 frames after the resume ESC) showed the normal 4:3 page together with a small
copy whose items were pulled towards the screen edges. I reproduced it with a drawlog run
(`build/harness/resume-drawlog`). The host saw `gui screen 23 -> -1` before f1921. The game still drew the full pause
page in f1921 and f1922, and those draws' camera packets carry screen 23. With `nativeMenu` already off, the page
draws took the gameplay legacy HUD cluster map (anchor at the nearest edge or the centre, `hudScale * hudSize`). The
numbers match: "PAUSED" at native y 173 becomes 173 * 0.60 = 104 (display y 228), and the frame corners at native
x 126 become about 57. This predates `pauseHudDraw`, which only reclassifies draws that are already menu draws. It
is a one-frame lag between the host's once-per-present screen poll and the GS draw stream, and it can also happen
on open.

Fix: `hud::nativeMenuDraw` also treats a draw whose camera packet names the pause screen (0x17) as a native menu
draw, as was already done for loading (14).

## Follow-up: opening frame

With `nativeMenuDraw`, `verify5-pause/p01` showed the orbs at size 1 on the 4:3 canvas edge (red orb at display
x 390..560, which is native 48..108 through `menuMap`). On the opening frame the host had already pushed
`nativeMenu` for screen 0x17, but that frame's camera packet still said -1. So the draws were menu draws, and
`pauseHudDraw`, which checked only the camera's screen, did not see the pause. Before `nativeMenuDraw` the same
mismatch could also occur; it depends on timing, and the slow drawlog run (`build/harness/open-drawlog`) did not
hit it. `hud::pauseScreen` now treats either side naming 0x17 as the pause: the camera packet, or the host flag
together with the host's menu screen.

## Follow-up: no draw-order dependence

Three runs with the `pauseScreen` build (`build/harness/verify6-pause-1..3`) still showed page-mapped orbs on the
first paused frame in two of them: the dimming panel was already drawn in that frame, before the HUD. The page/HUD
split is now purely geometric and `pausePageStarted` is gone. Under the pause, a menu draw centred outside the native
page box 120..390 x 110..400 (512x512 frame) is gameplay HUD. I checked this against every post-world screen-0x17
draw in the three drawlogs (shore slide, village open, village resume). All page pieces are centred inside the box:
dimming panel 154..358 x 128..384, frame batch 126..384 x 118..395, text and highlights 207..305 x 161..361. All
HUD pieces are centred outside it: orbs at rest and through the whole slide, the left gauge, the air gauge
462..488 x 32..142, and the small left sprite 52..86 x 167..222. The late water packet (124..423 x 241..331) is
centred inside the box but is perspective, so it is never a menu draw.
