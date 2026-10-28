# Resident model reflections follow-up — 18 October 2026

This continues the [global water review](water-global-review.md).
The tested executable is SHA-256
`a5ed601a91791a6191131f2e85a2524413a38cc892ecd1e6be5923ec1af96ade`.

## Changes

- Export live native model instances before camera culling. Rigid meshes share
  immutable geometry and cached DXR bottom-level acceleration structures; each
  instance retains its native transform and opacity. Constructor/destructor
  hooks track lifetime. Level replacement and off/on transitions reset capture.
- Preserve semantic visibility. Native `15f7c8` checks object flag bit 0 before
  its distance/frustum path. Applying the same check removes hidden helper and
  trigger boxes which initially appeared as large white shapes in the water.
  Camera invisibility alone does not remove a visible prop from reflections.
- Decode native source faces, prepared strips, UVs and material references.
  Decoded strip triangles must match original source topology. Unsupported
  animated ownership is rejected rather than exporting an unproven pose.
- Resolve never-viewed texture materials using the live database namespace and
  exact registered TXD path, through the existing read-only ISO filesystem.
  This does not force guest resource loads or change the game's resource order.
  Materials already resident in RAM take precedence; file caches clear on a
  level change. Stored row orientation, BGRA palette conversion and native CLUT
  lane permutation are handled explicitly.
- Ray traversal handles alpha-tested holes, four transparency layers and native
  instance fading. A native color-key cutout is no longer mistaken for a blended
  surface. Original framebuffer alpha remains unchanged.
- Water status distinguishes requested enablement from output in the last
  displayed image and includes the last pass diagnostics when output is absent.

The changes use native resource identities and layouts, without a Surdana
material-name or tint allowlist. Geometry still depends on resident model
resources; read-only material loading does not supply unloaded model geometry.

## Verification

Final-build captures and logs are under `build/reflection-scene-review`:

- `reflection-verified-props-pond-models-off.png` / `-models-on.png` isolate
  resident model contribution at the same world epoch, with water otherwise on.
  World pixels outside the pond are exact when the HUD region is excluded.
- `reflection-verified-coast-water-off.png` / `-water-on.png` check the snowy
  riverbank location. Both locations are in Surdana, not separate levels.
- `reflection-verified-combined-nr-off.png` / `-nr-on.png` compare neural output
  with DLAA and modern water enabled. `reflection-verified-quality-*` repeats
  the combined path with SR Quality, the distribution's configured SR mode.
  These pairs toggle NR; they are not a native-resolution versus SR comparison.
- Both combined runs verify the final 1920×1080 swapchain against the evaluated
  world at epoch 200: **zero differing bytes**. The neural validation reports
  zero non-finite RGB values. Native HUD and host overlays are excluded from
  off-image captures and from the world presentation audit.
- The longer combined run lasted 130.5 seconds, including a paused settings
  inspection, and quit normally. `live-paused-status.png` shows the truthful
  enabled-but-no-new-gameplay status. All four final runs preserve original,
  source-fixture and private memory-card files.
- The source-RAM/ISO test exports **1,216 visible instances, 99 unique meshes,
  48,904 unique triangles and 48,779 textured triangles**. Seventy-nine animated
  instances lack proven pose ownership and are rejected. The test checks
  transforms, immutable snapshots, destruction, level reset, same-level
  re-enable and disk/RAM palette equivalence for a known native texture.
- DXR hardware tests pass for cold off-frustum geometry, actual textured hits,
  equivalent explicit/instanced geometry, alpha holes, layered transparency,
  opacity changes, moving instances and scene retirement, with D3D12 validation.
- All 18 selected CTests pass. The separate synthetic renderer regression also
  passes; its first broad CTest invocation reported missing executables for
  unbuilt targets, which were then built before the final selected suite.
  Optional DLSS/neural-disabled renderer compilation passes.
- The runtime patch applies cleanly to pinned upstream, with all 11 refreshed
  runtime files matching. Full runtime sources compile fresh while reusing the
  previously validated generated-game archive. Existing linker warnings remain.

## Limits

This fixes substantial omissions and removes the helper-box artifacts. It does
not complete every native material or provide a full lighting remaster.
Unloaded model resources and unproven animated poses remain unavailable to the
resident export. Native additive/special materials, dynamic lighting, authored
environment maps and materials without usable UVs remain incomplete. Texture
sampling uses the reflection pass's 128×128 material tiles. Transparent traversal
is bounded to four layers. Terrain material projection retains the approximation
described in the preceding review.

The effect remains an experimental DXR water pass. DLSS reconstruction and
neural rendering are separate stages; ray reconstruction and frame generation
are not implemented by this work. Only the available Surdana save has gameplay
validation. Other maps, extensive playthroughs and device-loss recovery remain
unverified. The snowy river capture still has an abrupt foreground surface
transition; this work does not claim to have resolved that visual limitation.

See `docs/journal/2026-10-18/data/reflection-scene-20261018.json` for machine-readable
evidence and `build/reflection-scene-review/deployment.json` for deployment,
backup and protected-file hashes. Existing distribution settings are preserved.
