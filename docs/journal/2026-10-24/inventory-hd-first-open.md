# Inventory HD art late on the first open (24 October 2026)

Report: the first time the inventory opens, the images along the top show the native PS2 art; the HD pack images
appear only after a while. Later opens are fine.

## Cause

Pack images (and live upscales) are built by `texup::Pool`, a FIFO of 1-4 workers. A 4x pack image decodes in about
26 ms, and with `texture_upscale = auto` at internal scale >= 4 every new world texture also queues an 8x upscale.
The first inventory open queues its page textures behind that backlog, so they wait for every world job ahead of them.
On later opens the results are in `upCache` (keyed by content hash) and are adopted immediately.

The section icon atlas itself (`INTERFCE.TXD` 94, the runtime overlay from 1486030) was not the slow part: runtime
atlases and overlays already run their job inline. A synthetic replay of the streamed-atlas flow (atlas uploaded one
block below the scratch target 0x3e80, target drawn every frame, T8 textures streamed into the same target, earlier
CT32 textures through the same slot) showed the overlay on the first draw at scale 1 and 4.

## Fix

- `texup::Pool::submit(job, urgent)`: a second lane that starts before queued background jobs.
- `setupUpscale`: textures of the GUI page on screen (`!engineWorldOpen && sceneCamera.guiScreen >= 0`) and 1:1 UI
  sprites take the urgent lane. Pack images of the page run on the render thread while the frame's budget lasts
  (`kGuiPackSyncUs` = 12 ms, checked before each load). The world behind the page keeps the background lane.
- Tests: `guiPagePackFirstFrameTest` (48 queued 8x world upscales, then a GUI-screen draw of a texture with an exact
  pack image; it is HD on the first draw, and native without the fix) and a lane-order check in `gsr_test_texup`.

The first inventory frame shows the first page texture(s) in HD, and the rest follow within a few frames from the
urgent lane. If a visible stall shows up on first opens, lower `kGuiPackSyncUs`. Loading the whole page in its
first frame would cost about 10 page textures x 10-26 ms.

Not verified in game yet.

In-game check (Surdana start, inventory opened 20 frames into gameplay, shots 1-120 frames after the key): the top strip
(section icons, ring frames, PgUp/PgDn caps) is HD from the first frame the page is visible (frame 4, still sliding in)
and identical to frame 120 from frame 8 on. The first frames present slower (overlay ~8 fps, one-off) while the page's
pack images load under the 12 ms per-frame budget.

Follow-up (same day): the user wants the page HD-perfect from its first frame and accepts a one-off stall, so the
budget is gone: every image of the GUI page (pack image or live upscale) is built on the render thread before its
draw; only 1:1 UI sprites outside a page still use the urgent lane.

Checked in game with the same run: HD from the first visible page frame; the opening frame took ~258 ms (overlay)
once, later opens are cached.
