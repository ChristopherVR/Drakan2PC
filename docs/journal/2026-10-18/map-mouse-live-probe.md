# Frozen-source Map mouse probe

This recipe was prepared offline, then exercised against frozen build06 on
2026-10-18. Both 21:9 and 32:9 click probes exited normally with unchanged
saves and selection. Native captures and direct renderer images show Palace.
The one-location save cannot prove accepted clicks or changed-selection
navigation; 32:9 also lacks a same-run raw bounds certificate. See
`docs/journal/2026-10-18/followup-render-input.md` for the limited result. The procedures
below describe evidence requirements, not additional successful coverage.

## Isolation and discovery

1. Record the exact combined executable SHA256, private INI SHA256 and source
   save-tree hashes. Copy the existing early save's **whole** `mc0`/`mc1` trees
   and `iso/SCES_500.06` into a new private directory, preserving the relative
   layout. Refuse an existing destination. Follow the isolation pattern in
   `scripts/harness/combat_quick_inventory_run.ps1`, without its combat script/fixture.
   Do not relocate actors, change quest flags, or write original cards.
2. Use the parent's existing diagnostic launcher and a private INI copy, with
   the exact released build. Run discovery separately for `DRAKAN_WS_ASPECT=21:9`
   and `32:9`; keep resolution, presenter, pacing and input settings fixed.
   Existing `scripts/archive/2026-10-18/ui_aspect_followup.txt` reaches Map, captures
   `input_ingame_aspect_map.bmp` and `ram_aspect_map.ram`. Confirm the route has
   dismissed help and settled in screen21/section3 before using its timing.
3. Collect `input.log`, stderr/runtime logs, screenshots and RAM. Hook lines
   `[in-map] native marker frame=... section=... index=... image=...` are limited
   to the first eight captures per process. A missing line later is not proof
   of missing geometry. Use a fresh process for discovery.
4. Capture a **bounded** raw Map draw window with `GSR_DRAWLOG=FIRST-LAST`
   and `GSR_DRAWLOGV=6`, chosen from the discovery frame numbers. No all-frame
   trace. Native hook and GS frame counters must be correlated, not assumed equal.
   Preserve complete draw headers (`n`, flags, TEX0), all vertices and draw IDs.
   These raw vertices precede the backend HUD transform. Do not apply the page
   transform twice or mistake the animated border/background for a marker image.

## Geometry certificate

The frozen native hook does **not** log corners or hit/target/ack state. Logs
alone therefore cannot automatically certify image identity-to-rectangle
association. Review the marker image draw at caller `43a330`, its index/image
hook record, raw textured geometry and screenshot together. If association is
ambiguous, stop at discovery and report missing observability; do not guess a
click or claim direct support. A narrow diagnostic change would need coordination.

Record all visible marker rectangles to reject overlaps and choose empty space.
Use final submitted native framebuffer coordinates, clipped to 0..512. For
GS-position coordinates subtract `(2048-W/2, 2048-H/2)` first. Account for actual
image clipping. The hook captures only the native image-marker branch, not every
possible Map artwork path. Re-review independently at each aspect.

Certificate JSON example (replace every placeholder from actual discovery):

```json
{
  "aspect": "21:9",
  "exe_sha256": "ACTUAL_HASH",
  "ini_sha256": "ACTUAL_HASH",
  "evidence": ["discovery/stderr.log", "discovery/input_ingame_aspect_map.bmp"],
  "reviewed_identity_and_complete_visible_geometry": true,
  "view": [0, 0, 640, 512],
  "page_scale_x": 0.5714285714285714,
  "target": 1,
  "markers": [
    {"index": 1, "rect": [100, 90, 120, 110],
     "raw_draw": "REPLACE_WITH_DRAW_REFERENCE",
     "identity_log": "REPLACE_WITH_HOOK_REFERENCE"}
  ],
  "empty": [300, 400]
}
```

The numbers above are illustrative, **not certified click coordinates**.
`view` is the actual presenter rectangle in virtual script-client 640x512 units;
use letterbox offsets if present. `page_scale_x` is measured whole-page X scale,
normally 4/7 at21 and .375 at32, not HUD scale. Verify executable/INI hashes and
source card hashes match discovery before replay. JSON references are a human
review record, not machine validation of the referenced logs.

## Probe and acknowledgment

```powershell
python scripts/archive/2026-10-18/map_mouse_live_probe.py inspect DISCOVERY/ram_aspect_map.ram
python scripts/archive/2026-10-18/map_mouse_live_probe.py recipe CERTIFICATE.json PRIVATE/sequence.txt
# Launch only after exclusive slot grant, using the parent's diagnostic launcher.
python scripts/archive/2026-10-18/map_mouse_live_probe.py check CERTIFICATE.json EVIDENCE_DIRECTORY
```

The generated recipe opens Map with the calibrated early-save route, dumps
before, clicks the unique marker centre, holds the pointer stationary for about
15 seconds, then dumps native selection. It clicks reviewed empty space and
captures selection again. No keyboard arrows occur between the three dumps.
The wait permits the bounded native-arrow acknowledgment (up to771 input updates
at the maximum256-record table); confirm adequate update cadence in live logs.
Do not move the mouse during acknowledgment: movement cancels navigation.

Pass requires stable section/table/region/record identities, native
`section+0x6c == target`, an actual change from the initial selection, and unchanged
selection after the empty click. Inspect screenshots for original art/layout and
the corresponding native highlight. A timeout is a failure/inconclusive result,
not permission to increase the production cap. No native Map Cross action exists;
selection acknowledgment is the intended result, not an activation callback.

The checker cannot prove draw freshness or native dynamic-state eligibility from
RAM alone. Those remain runtime guards and live-review obligations. Compare save
hashes afterward; retain private-run paths and originals-unchanged evidence.
Repeat at32 with a separately reviewed certificate, never stretched21 coordinates.

## Known early-save limit

Read-only inspection of `build/t/inventory-crop21-probe01/ram_aspect_map.ram`
finds count2, region0, selected0. Records contain drawable1, state keys174/104,
positions(110,100)/(0,0). These are table facts, not visibility/eligibility proof.
If only the selected marker is certified, report **LIMITED**: visible-marker click
and empty-space no-change can be observed, but changed-selection/multiple-marker
navigation and later regions are unexercised. Do not modify save progression to
manufacture coverage. No full-map coverage claim.
