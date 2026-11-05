# Issue 1: multiplayer character appearance and settings

## Evidence and scope

The follow-up attachment was inspected directly (private analysis copy:
`build/issue-review/remote-lod-followup.jpg`). It is a side-by-side view of two multiplayer Rynns, with nearby local and
remote bodies and visibly different lighting/shadows between the two clients. A compressed screenshot cannot measure
their native triangle counts or identify their renderer settings. The old slot-zero RAM evidence in
[the LOD review](issue-1-remote-lod.md) does not close this appearance report.

Current `dist/DrakanPC/drakan.ini` has renderer D3D11, internal scale 4, aspect 16:9, DLSS quality, neural rendering off,
texture upscaling auto/default filter, anisotropic 16, mip bias auto, HD textures enabled and texture budget 6144 MB.
These are local private settings, not evidence of the reporter's settings. Native options contain no model-detail or
LOD-distance option. `multiplayer.ini` persists player character choice (currently `177d`), colour and legacy model;
it contains no per-remote graphics setting.

Recent acceptance `build/harness/issue1-host-disconnect-lifetime/b/` instead used its private D3D11, scale-auto, 4:3
configuration. Its startup log establishes 1280x720 output and internal scale 2. That run cannot establish the
appearance of current dist settings. Environment overrides can also take precedence over the INI. No running game,
external user's process, settings file, card or deployment was modified for this review.

Source review found no multiplayer-specific reduction of texture quality, render scale, mesh detail or LOD distance.
Native mesh LOD uses camera distance; network pose writes do not select a coarse LOD. Texture upscaling, HD
replacement, anisotropy and mip bias affect surface appearance, not the number of mesh polygons. DLSS quality changes
the world's rendered sampling resolution; local and remote actors share that renderer. These distinctions are
relevant to testing, not proof of what caused the reporter's appearance.

## Corrected configuration defect

The runtime and default INI support `mip_bias = auto`, which applies the internal-resolution compensation. The settings
data model previously contained only a float. Its parser could not represent `auto`; the menu showed numeric zero
instead, and any numeric adjustment called the renderer with auto mode disabled. There was no menu path back to
auto mode. This is a demonstrated settings/UI defect even when the original INI remained intact until an edit.

The settings model now retains the auto flag. Store parsing/serialization and change detection distinguish `auto`
from an explicit numeric zero. Both settings surfaces use the shared schema's Auto choice and numeric presets;
existing custom numeric values remain selectable. The host applies both value and mode through `setMipBias`, so
changing manual zero back to Auto updates the live renderer and persists `auto`. Invalid/non-finite input does not
replace a valid mode/value. This restores control over texture sharpness; it does not claim to increase character
polygon detail or resolve the reported screenshot.

Regression coverage in `settings_test` checks auto -> manual zero -> auto across persistence/reload, meaningful
change detection, custom values and invalid input. `settings_schema_test` checks the actual menu action path and
selection for Auto, zero and custom numeric bias. Parent integration owns compilation/execution and live application
verification after the canonical build. Source formatting and diff checks pass.

The renderer/config accepts texture upscale factor 8, while the current settings menu offers Auto/Off/2/4. That
omission was not changed: it is not established as a cause of polygon appearance, and exposing a more expensive
manual texture setting requires a separate UI/performance decision. Auto remains supported in both paths.

## Pending visual acceptance

Use a fresh paired run with identical private settings/cards and the default Rynn character. Record executable hash,
output/viewport, internal scale, DLSS mode, texture replacement availability and effective mip-bias mode. Capture a
nearby remote Rynn with matching pose and camera distance, native controller slot and model/vertex descriptors. Then
compare manual zero and Auto at one fixed high internal scale, changing only that setting. Confirm Auto survives a
restart and the menu accurately reflects it. This separates surface blur/reconstruction from actual mesh LOD without
forcing global high detail or treating earlier different-configuration captures as acceptance.

## Deployed setting acceptance

`build/harness/issue1-mip-auto-live` ran two clean native title/settings sessions on
`DE57CF5F6BADBC06D234DEBC28DF7B178668660695E3B4C57ADAE9ED7E5A8415`.
The actual settings inbox applied Auto -> numeric zero -> Auto; private saved INIs
confirmed each transition, and Auto persisted through restart. Both runs reached
scripted quit with exit 0. Parent inspected the restored settings screenshot.
The mip row is below the visible scroll area, so this establishes the host apply
and persistence paths, not visual inspection of that row or effective GPU bias.
No installed player INI was edited by this test. Close-up mesh evidence is separate.

## Final native appearance cross-check

The final same-build paired captures `build/harness/issue1-remote-motion-baseline2`
and `issue1-remote-motion-adverse` used SHA-256
`fce25c698b06cc77a233aea741c3f46c318a3c06fa40b565df9813002ed02503`.
The baseline A near screenshot visibly frames the remote Rynn; its matching RAM
identifies model `0x20f25`, native slot 0 and all 1628 vertices from the 1628 / 208
vertex LOD pair at selector distance 2339.1 units. Both peers' near RAM selects
slot 0 in both runs; later far samples select slot 1 near 6600 units. Thus the
current nearby default-Rynn capture does not demonstrate a permanently low-detail
mesh. Original source-model vertex limits remain independent of texture filtering,
HD replacements and reconstruction settings.

Actual descriptor counts, distances, poses and the stale-startup-handle exclusion
are retained in each run's `lod-readback.json` and detailed in
[the LOD journal](issue-1-remote-lod.md). B's near screenshot does not frame its
remote subject and is not used as visual proof. This pair did not compare Auto
versus numeric mip bias or establish effective GPU bias; the earlier settings
apply/restart evidence remains separate. Four native scripted quits exited 0,
with unchanged executable, deployed INIs and source fixture card. No new appearance
or networking production change was justified by these captures.
