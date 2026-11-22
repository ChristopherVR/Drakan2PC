# Issue 17 HUD fills in Order of the Flame

The lower-left crop in [issue 17](https://github.com/ChristopherVR/Drakan2PC/issues/17) shows the health orb's frame with
the world visible through the area that normally contains the red fill. The crop covers the lower-left HUD. The air
gauge renders at the upper right while Rynn is swimming. The converted campaign disables spells, so the right orb is
hidden when it would show mana. It can still show arrows or item charges.
[Campaign spells behaviour](../../reference/modding.md#campaigns-without-spells)

## Local results

All runs loaded Ruined Village directly on a fresh private card using the deployed PAL executable
(SHA-256 starts with `73c502c6aec9`), converted campaign build `493b53de681b8791`, and Direct3D 11 on an RTX 5080.
The window was 1280×720 at internal scale 2 with DLSS off. All three runs exited cleanly via the input script.

| Run folder under `build/harness/` | Observed |
| --- | --- |
| `issue17-d3d11-modern` | English resources and the installed texture settings. A red orb in `gameplay.png` and a filled Health bar in `inventory.png`. |
| `issue17-d3d11-native-water` | Same settings with the Original inventory. A red orb in `gameplay.png` and `inventory.png`. The upper-right gauge fills blue at the surface and drops progressively across `diving.png`, `deeper.png` and `air_low.png`. |
| `issue17-d3d11-german-unscaled` | German PS2 resources, modern inventory, `GSR_TEXREPLACE=0` and `GSR_TEXUP=off`. Gameplay and inventory health stay filled. The air gauge appears and drains after submerging. |

Each folder contains the exact input recipe and session logs. The water check places Rynn at (248832, 11800, 105472)
over the Ruined Village ocean and holds C and W after she settles at the surface.
`gameui_test`, `gameui_pause_test`, `sprite_footprint` and `inventory_views_policy` passed in the existing runtime test build.

## Follow-up on the affected installation

Gameplay and the Original inventory share the guest HUD. Its orb fill uses a texture whose memory overlaps the render
target used for the orb liquid. The modern inventory reads the live health values and renders its own bar, so comparing
the two layouts on the affected save would show whether the problem also reaches the host-drawn bar.
[Orb texture handling](../../../runtime/gs/src/gs_targets.inl#L436)
[Modern health bar](../../../runtime/gameui/src/Screen.cpp#L1257)

The reporter's renderer and level details are still needed to isolate the fault. Collect both session logs
(`drakan.log` and `drakan.1.log`), `drakan.ini`, the release or build used, Original versus Modern inventory, and
the affected level or save. An uncropped frame while submerged would capture the air gauge area. The local runs
did not reproduce the transparent health orb or missing air fill.

## Log review and renderer fix, 22 November 2026

The attached logs show two sessions. `drakan.log` has English PAL resources on Direct3D 11 on an RTX 5070 Ti.
It records Ruined Village gameplay and Original inventory draws after a change to 21:9 with DLSS Quality. The
rotated `drakan.1.log` ran on WebGPU over Vulkan and stayed in the title and Options screens. The inventory
screenshots also identify Vulkan in the overlay. Both logs register the native font atlases. The interface
resource misses also appear in a local run with working HUD and text.
[Reporter logs and screenshots](https://github.com/ChristopherVR/Drakan2PC/issues/17#issuecomment-7062183569)

Local Vulkan runs at 21:9 showed the health fill and Original inventory text with a fixed or automatic internal
scale, with the frame queue enabled, with GPU vertex processing, and with texture replacement and scaling off.
Direct3D 11 with DLSS Quality showed the health and air fills. Those runs used the local game and campaign pack.

A synthetic GS scene reproduced a transparent fill with an exact texture pack match. The CPU copy still held the
uploaded atlas, while the render target held newer fill pixels. The resolver copied the live pixels into the
texture and then selected the static replacement using the old CPU hash. The resolver now keeps the GPU result
whenever it needs live target blocks. Blocks proven identical after a host upload can use the ordinary replacement
path. The regression checks that static art is replaced, the live fill renders, and a later upload restores the
replacement. It passes on Direct3D 11 WARP at scale 1 and Vulkan at scale 5. All seven selected CTest suites passed.
The rebuilt game shows the health fill, inventory item details and help text in Ruined Village on Vulkan and in
Surdana on Direct3D 11 with DLSS Quality. The Ruined Village run also shows the air gauge drain underwater. Captures
are in `build/harness/issue17-fixed-d1` and `build/harness/issue17-fixed-d2`.

The synthetic failure establishes one way to lose a fill. The screenshots and logs do not identify the texture pack
match or draw state in the affected frames. Confirmation on the reporter's installation and the cause of its missing
inventory text remain open.

## Retest with the reporter's v0.1.36 settings

The follow-up confirms that inventory text and subtitles are visible and shows that the health orb is still transparent.
The newer logs cover Direct3D 11 in both games. During play they show DLSS Quality and ray-traced diffuse lighting.
The renderer exit counters show zero aliased texture copies. The remaining cause is open.
[Follow-up screenshots, logs and settings](https://github.com/ChristopherVR/Drakan2PC/issues/17#issuecomment-7069458879)

The local retest used the reported `5028d2c84dfcdae2` Order of the Flame pack and the attached settings at 3440×1440.
On an RTX 5080, Direct3D 11 rendered the health fill in gameplay and Original inventory in Ruined Village and Surdana.
A separate Vulkan run at 1920×1080 with the same campaign pack also rendered both fills. The Direct3D 11 underwater
replay with ray-traced diffuse lighting showed the air gauge draining. Captures are in `build/harness/issue17-ootf-title-pad`,
`build/harness/issue17-surdana-title`, `build/harness/issue17-vulkan-reporter-pack` and
`build/harness/issue17-reporter-gi-before`.

The reporter's installed `textures` directory and affected saves were unavailable. A comparison on the same save with
HD textures and automatic texture upscaling switched off would isolate whether either replacement path is involved.

## Preserve fills after render-target readback

The earlier fix covered a texture assembled directly from the GPU. A separate regression forced the same atlas through
GS memory. The native red pixels were present after readback, but a matching HD pack image with a transparent fill
still won the texture lookup. The capture showed black where the red fill should be.

The cache now keeps the original pixels when a single-level 32-bit texture still contains render-target readback data.
A later host upload can use its usual replacement even if the uploaded bytes are identical. The regression checks both
transitions and passes on Direct3D 11 and Vulkan. The rebuilt Windows game showed health in gameplay and Original
inventory on both renderers using the reported campaign pack. The Direct3D 11 run used the reported ultrawide settings
with DLSS Quality and also showed the air gauge draining underwater. Captures are under
`build/harness/issue17-readback-fill-{live,vulkan}`.

## Reproduce the empty gauges with the uploaded HUD mask

The guest uploads `INTERFCE.TXD` entry 128 as a 128×128 texture. The draw trace shows the health fill sampling
the white mask in its bottom half and tinting it red. The frame comes from a separate indexed texture.
[HUD atlas and draw trace](../2026-10-18/hud-orb-edge.md)

I gave the renderer an exact-match HD image named `0aca2ff6ae460ac2.png` with its alpha cleared. With the readback
fix already present, Direct3D 11 accepted the image and drew empty health and air gauges in Ruined Village. The
Original inventory kept its text and frame, and the health fill was transparent there too. The renderer reported
zero aliased texture copies in that run, matching the reporter's counter.

The shared texture cache now uses the original pixels for this mask across both renderers. Replaying with the same
replacement showed the red health fill in gameplay and Original inventory, and the blue air fill shrank over the
course of the dive on Direct3D 11 and Vulkan. Direct3D 11 also showed the health and mana fills in Surdana.
All nine selected renderer and UI tests passed. The replay logs and captures are in `build/harness/issue17-mask-before`
and `build/harness/issue17-mask-after-*`. The reporter's installed texture pack still needs a retest.
