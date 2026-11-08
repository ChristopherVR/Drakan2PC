# Loading screen flipping to 4:3 between levels (2026-11-08)

Report: "when loading the game swaps between 4:3 (black borders) and back to the widescreen borders".

## Evidence

OotF New Game at 1280x720 (`aspect = auto`), `DRAKAN_WS_LOG=2`, one shot per second. The converted Intro level
(logos, owl, landscape) stays screen -1 the whole time and is presented wide; the Intro to Ruined Village level change
logged:

    vsync=3699 world=1 screen=26      (save write, gameplay still wide)
    vsync=3701 world=0 screen=14      push wide=1   (loading, wide)
    vsync=3715 world=0 screen=26      push wide=0 present 1.333   <- 4:3 pillarbox
    vsync=3724 world=0 screen=14      push wide=1                 (loading again, wide)
    vsync=3732 world=1 screen=-1      push wide=1 projection 1.778

## Cause

A level change runs the engine's save-in-progress screen (0x1a) twice: once before loading and once in the middle of
it (the auto-save of the level being left, `[saves] ... .rsg` is logged inside it). `layoutPolicy` presents wide
only for a world page or a native menu page; 0x1a is only kept wide through the *world* latch (`saveWriteScreen`),
which the loading screen (0xe is a front-end screen) had already cleared. So for about 10 vblanks the picture was
pillarboxed to 4:3 inside an otherwise wide loading screen, and the foreground map and HUD scale switched too.

Movies (screens 0x1d / 0x1e, the logo and book) stay 4:3 on purpose and were not touched; the converted Intro level's
scenes are in-engine 3D and are widened as before.

## Fix

`WorldPolicy::layoutScreen` (`drakan_widescreen_policy.h`) latches the loading screen: while 0x1a directly follows 0xe
the layout is chosen as for 0xe; the first screen that is neither ends the latch, so a later in-game save is
unchanged. `hostTick` passes the latched id to `layoutPolicy`. Unit test: `widescreen_policy_tests.cpp`
(level change sequence). Loading therefore keeps one presentation for its whole length and switches once into
gameplay.
