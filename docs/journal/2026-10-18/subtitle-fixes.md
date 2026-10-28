# Subtitle rendering and visibility fixes

The native dialogue bar and the narration overlay have separate renderers.
Both were checked after the tutorial-only correction.

Native dialogue formerly sent individual words/glyphs through the gameplay
corner and previous-frame cluster transforms. A live injected native subtitle
at 16:9, HUD size 0.55 reproduced left/centre/right fragments and clipped
letters. The runtime now publishes ordered Subtitle_Draw entry/return markers,
including interior resumes after guest yields. The renderer observes the
native full-width bar (a 32x32 solid tile), then uses one centre/bottom map
for its 256x256 font cells. The final font DMA may follow the guest function
return, so that bounded text map persists through the display-frame tail.
Other HUD textures retain their corner maps; explicit phases reset the scope.

Narration now draws the actual wrapped lines used to size the band. Explicit
paragraphs and UTF-8 boundaries are preserved, line spacing matches band
measurement, and unusually tall cues reflow at a smaller size. This fixes
large-font horizontal clipping and incorrectly sized backgrounds.

The narration toggle controls visibility without stopping the media timeline.
Soundtrack requests still arm synchronization while hidden, so enabling during
playback shows the current cue. Movie completion continues to stop the track.
The audio clock and shipped cue timings were not changed.

Validation artifacts are private under `build/subtitle-review-20261018/`.
The render regression covers all 40 shipped English narration cues and
synthetic paragraphs/long words at four aspects, five sizes and three output
heights: 1,074,240 glyph-bound checks, with every non-space glyph retained.
The real visibility API passes hide/re-enable, expiry and stop checks with a
deterministic clock. Existing settings and HUD suites also pass.
Live native subtitle runs completed with exit 0 at 16:9 and 21:9. Direct
renderer images confirm intact centred text and corner orbs; the 21:9
typography check used internal scale4 (scale2 undersamples the small glyphs).

This does not add missing non-English narration transcripts or establish
whole-game dialogue/audio timing parity. The live dialogue test invokes the
game's own SetSubtitle path; it is not a full NPC conversation playthrough.
