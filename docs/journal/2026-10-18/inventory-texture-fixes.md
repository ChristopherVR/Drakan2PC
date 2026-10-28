# Inventory texture and text fixes — 18 October 2026

The inventory help OK label now draws from the generated decorative font atlas
instead of the small retail fire-text intermediate that clipped the K. This
changes the label's appearance to ordinary sharp text. Native focus, hitbox,
outside-click handling and confirmation behavior are preserved.

The GS texture replacement path now accepts an exact content-hash pack match
for indexed artwork even when streaming has marked the reused VRAM slot volatile. The inventory
dialog border (`INTERFCE.TXD` 118) was an actual affected texture. Unknown
animated images and volatile full-color orb/effect masks still use native pixels. User exclusions and the HD toggle
still apply; runtime-generated font/prompt content remains protected from
external replacement. RGB-only alias matching is not newly enabled for volatile
slots.

Inventory ASCII font shadows use a one-native-pixel offset and half opacity
instead of the opaque two-pixel duplicate. Host font registration carries
explicit glyph-atlas metadata. Complete black glyph triangles are classified
within a draw, because the engine batches shadows and colored foregrounds
together. World draws, colored foregrounds, other textures and the retained
keyboard pictograms keep their original rendering.

## Validation

- Focused pixel suite: 45 checks pass on WARP at scale 1 and the hardware
  renderer at scale 3. Includes recycled-slot HD lookup, unknown animated
  content, the HD toggle, full-color effect masks, mixed foreground/shadow batches, world/nonfont
  controls, protected pictograms and runtime atlas reuse.
- Existing inventory alpha-mask suite: 22 checks pass.
- Native HUD page policy passes; font atlas 19 checks and prompt suite 68
  checks pass against the owned disc and installed font.
- The native game run verifies the help and inventory presentation. A DLSS SR
  Quality plus Neural Rendering run verifies all seven category assertions,
  outside/inside OK clicks and return to gameplay. SR and NR evaluations both
  completed successfully. Direct renderer PNGs were inspected; hidden desktop
  screenshots were not used as visual evidence.

Private logs, executable hashes, screenshots, saved RAM checks, installation
receipt and rollback location are under `build/inventory-texture-fix/`.
No disc assets or fonts are distributed. Artwork fidelity still depends on
the user's local texture pack. This is not whole-game graphics certification.
