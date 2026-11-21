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
