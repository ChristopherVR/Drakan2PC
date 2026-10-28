# Modern water lost under the pause menu — 2026-10-23

Symptom: with `modern_water=1`, opening the pause menu (ESC) in gameplay replaced the modern water with the
original flat teal PS2 water polygons; ray effects on the world also dropped out.

## Evidence

Run `build/harness/pause-water-diag` (shore save, `build/verify-20261023/shore.txt`, `DRAKAN_DLSS_LOG=1`): the game
keeps rendering the world every frame while paused. The scene camera keeps being published with `World|Valid`
(`flags=1b/1f`) and `screen=23` (pause, 0x17); 244 such packets in the paused window, next to 2396 gameplay ones
(`screen=-1`), and BeginWorld/EndWorld pairs continue. `guiAllowsWorld` (`drakan_scene_camera.cpp`) deliberately
keeps World for the pause screen.

## Cause and fix

The surface classifier in `d3d11_draw.cpp` additionally required `surfaceSource.guiScreen < 0`. Under the pause menu
no draw was reconstructed, the target's surface list had no water triangles, `finalizeNeuralWorld` skipped the ray
scene (`wantWater` false) and the native water stayed.

The gate is now `surface::surfaceCameraAccepted` (`surface_builder.h`): a world camera is accepted under any GUI
screen (the host's publication gate already refuses front-end/loading/movie screens); the post-EndWorld fallback
without a world camera still requires a gameplay camera. Gameplay behaviour is unchanged. It should also cover the
world drawn behind in-game help boxes and subtitles (not verified).

## Follow-up: native water in the central 4:3 rectangle

With the gate above fixed (`build/harness/verify2-shore/paused.png`), the paused frame had modern water only in the
side strips. The central 4:3 rectangle still showed flat teal tiles with hard edges, cut off by a vertical scissor
edge at the 4:3 boundary. That is also where the HUD orbs sit and get clipped. The unfixed native run shows the same
edge. A `GSR_WATER_COVERAGE_DEBUG=1` run (`build/harness/pause-ws-cov`) shows full-width coverage and an unchanged
widened projection (`proj=-0.8938` in both pause and gameplay), so neither the camera nor the coverage is 4:3.

Cause: the game drains its last CPU-clipped water packet after EndWorld. In gameplay those textured perspective
triangles are kept out of HUD mapping (`hudWorldTriangle`). Under the pause menu, though, the host sets `nativeMenu`
(`nativeMenuScreen(0x17)`), and `hud::menuSubmission` claimed every post-world draw. The water triangles were
squeezed through `menuMap` into the centred 4:3 canvas and scissored to it, on top of the modern water.

Fix: `menuSubmission` now rejects textured perspective triangles (`perspectiveTriangle`, Q != 1). Menu sprites,
FST and Q = 1 UI triangles are unaffected.
