# HD movie pack (AI-upscaled logo / intro / outro)

Goal: show the intro (and the other movies) at 4K-class quality. There is no higher-resolution source; the 512x512 IPU
frames are upscaled offline (Real-ESRGAN x4 -> 2048x2048) and drawn by the renderer in place of the native frames.
Design, format and numbers: `docs/reference/fmv.md` sections 9 and 10.

Findings on the way:

* The movies never go through textures. `GSR_TEXLOG` shows no texture decode during the logo or the intro;
  `GSR_XFERLOG` shows 1024 16x16 CT32 uploads per frame into each of the frame buffers 0x0 and 0x1000. The "FMV tiles"
  claim in `hd-textures.md` was wrong (corrected).
* IDEC starts byte aligned one byte after the frame start, so the compressed frame can be identified from the bitstream
  (CRC-32 + length) without knowing the player object.
* The IPU decodes ahead of the GS (6 slot ring, bursts); a "frame N is next" message from the EE would race the GS
  worker. Content signatures (sum of per-macroblock hashes) let the GS recognise a complete frame itself.
* Real-ESRGAN pitfalls: auto tiles (200 px) leave seams at output 799 / 1599; whole-frame tiles with `-j 4:6:4` write
  black frames silently. Builder uses `-t 512 -j 1:1:2` and a brightness check.
* The decoder's accurate swscale path was the bottleneck (36 ms/frame), slowing the intro by a third; fixed.
* Open: the intro runs at ~48 frames/s in the port (63.7 s instead of ~122 s) with or without the pack, and the movies
  are stretched in 16:9 with `aspect = auto`. `native_display_commit_test` does not link (`drakan_ws::initialPresenter`
  missing in its stubs; pre-existing).

## Follow-up (same day): the open problems

* **Noise / shimmer** (reported in play): Real-ESRGAN invented per-frame "fur" from the MPEG noise. Deblock + hqdn3d
  before the upscale and temporal hqdn3d after it halve the measured shimmer; pack rebuilt (fmv.md section 9).
* **Movies stretched to 16:9**: the engine updates the camera during the logo / book movies; the widescreen world policy
  now stays 4:3 while the IPU decodes frames (`PS2Ipu::decodedFrameCount`).
* **Intro too fast**: the players use "Full" (one frame per engine tick). First fix attempt patched the "Full" interval to
  40 ms: black flicker in play, because the movie screen clears the buffer every tick. Final fix holds the engine tick
  to 40 ms per frame while a "Full" movie plays; the hook has to compare the frame counter across calls because
  `f_0x1767a0` is resumable. Intro 124.6 s (narration 123.3 s).
* **native_display_commit_test** link error: stale `drakan_ws::initialPresenter` stub signature, fixed.
