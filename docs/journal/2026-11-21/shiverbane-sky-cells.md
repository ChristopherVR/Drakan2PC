# Shiverbane sky texture verification

[Issue #8](https://github.com/ChristopherVR/Drakan2PC/issues/8) reports a regular grid on foot and vertical bands in flight
on build `bacb3eaf`. That revision predates `cf20bc18`, which keeps raw 24-bit textures with sparse noisy rows at their
native size. The rule fixes a texture defect measured with a Shiverbane sky ramp. The reporter's exact grid remains
unconfirmed because the issue has no screenshots or save near the affected view.

## Sky ramp

In a PAL Shiverbane run, the first sky draw uses a 64×64 `PSMCT24` texture with clamping, point sampling, and no mipmaps.
The sky dome uses two strips. In the captured texture `bb67795cdafa5c63`, rows 0–6 and 32–38 contain the fog color
`RGB(162, 174, 178)`. The rest contains leftover video memory. Two rows meet the old dense-noise threshold. Fifty meet
the current sparse-noise threshold. Another run produced a different texture hash because the surrounding video memory
had changed.

I compiled the texture filter from `bacb3eaf` and the current source, then fed both the same dumped texture. The old code
selected 4× Smooth. The current code selected native size. Sampling the old 4× result over the lower sky strip changed
929 of 6,656 points by more than 10 RGB levels in at least one channel, with a maximum change of 16. The native samples
returned the fog color throughout. In the upper strip the largest change was one level. This places the measured
discoloration near the edges of the lower strip. The fault does not establish the cause of the reported above-horizon grid.

## In-game checks

- **On foot.** Replayed the route from Northern Tundra to the Shiverbane gate on Vulkan at 4× Smooth. The camera can
  still enter Rynn's model and show the translucent figure described in the
  [previous investigation](../2026-11-16/shiverbane-sky-cells.md). No regular sky grid appeared in the captures.
- **In flight.** Started Shiverbane mounted, placed Arokh near the open gate at `620544 128000 565000`, then climbed,
  flew north, and turned east. Vulkan captures at 1920×1080, scale 3, 16:9, FOV 80, view distance 1.23 and terrain
  distance 1.38 showed smooth clouds at both 1× and 4× Smooth. A separate 4× Direct3D 11 flight and a Vulkan sky sweep
  from three sites also showed smooth clouds. A close camera at the gate briefly showed Arokh's translucent model.
- **Cloud textures.** The three cloud textures drawn in the inspected frames matched their disc texels after GS alpha
  conversion. The cloud draws clamp both axes, and terrain drawn later covers their silhouettes. The procedural sky
  ramp and the cloud textures use separate draws.

The existing `texup`, `rt_sampling`, and `rt_sampling_pixels` tests passed. The confirmed ramp fault is covered by the
existing renderer fix. A screenshot and a save at the reported location would let us identify the draw behind the
original grid and verify it against the current build.
