# Native inventory refinement — 18 October 2026

The separate inventory sidebar was rejected and removed. This revision keeps
the game's existing tabs, item grid, details, map and character preview.

## Presentation and controls

- The native inventory canvas is left-aligned, with aspect-correct icons and
  text. Extra widescreen space remains available for the world and preview.
- Mouse coordinates use the same canvas transform as rendering. Empty grid
  cells do not equip an unrelated item.
- The native selected-item border has a warm, clearer tint. Hovering an
  unselected category highlights its existing icon without switching pages.
- Generated keyboard-prompt textures can use automatic upscaling while staying
  protected from texture dumping and external texture-pack replacement.
- The recognized plain font atlas receives 8x host-rendered ASCII glyphs from
  the installed Arial font. Native glyph positions, spacing, clipping, colours
  and measurement remain unchanged. Prompt icons and non-ASCII cells retain
  their original pixels at higher scale. This is not new HD panel artwork.

No font or game asset is distributed. If the local font cannot be loaded, the
original texture remains available. `[general] native_font_atlas = false` or
`DRAKAN_NATIVE_FONT_ATLAS=0` disables the generated atlas. A local font may be
selected with `DRAKAN_NATIVE_FONT_TTF`. Texture upscaling must be enabled.

## Candidate

Executable SHA-256:
`0E4BACD82DEF9B38FEF5EB8303B88A617AB238099959D6D2041A38CC20248EB3`

The candidate also contains the uncapped-frame, raw mouse accumulation and
native-menu aspect fixes documented in `docs/journal/2026-10-18/uncapped-fps.md`.

## Scope and remaining issues

The original item artwork, decorative panels and map are retained. Their
source detail is still limited. Thin HUD borders, some world/preview geometry,
hair, smoke and shadow issues remain separate rendering work. Selection and
equipping are tested in the existing early-area save; complete combat and
Arokh parity with Order of the Flame have not been established.

Forced 21:9/32:9 fullscreen captures also expose stale fragments of dismissed
inventory tutorial dialogs beside the panel. This is unresolved. Those captures
check layout proportions and pause/resume presentation, not a claim that all
ultrawide graphics are correct. They use a 16:9 desktop with a forced viewport,
not physical ultrawide hardware. Hidden windowed captures were discarded.

Removing the 30 FPS limit does not guarantee high performance. Auto internal
resolution at 4K can select 8x, which is more expensive than fixed 4x.

Validation receipts are recorded in
`docs/journal/2026-10-18/data/native-inventory-20261018.json`. The earlier rejected sidebar
candidate is not the release candidate.

The final native and combined SR Quality/Neural Rendering runs each passed
23 scripted category, Equipment and movement/combat checks, with audio enabled
and original/fixture/private saves unchanged. The combined run recorded
successful evaluations from both DLSS features.

Matched quiet SR/NR runs measured 30.00 capped versus 32.61 uncapped completed
world submissions per second, about 8.7% higher. Guest vblank remained near
60 Hz. These are scene-specific submission rates, not confirmed scanouts or
whole-game speed parity.

The font generator passed 14 checks, including native UV rectangles, unchanged
prompt/non-ASCII cells and GS alpha limits. Texture policy tests passed 26
checks; five display/scheduler/pacing test suites also passed.
