# Native subtitle font and spacing

Investigated the two subtitle renderers and captured a real Surdana farmer
conversation using the existing input harness. The current play configuration
selected German; before images and logs are private under
`build/harness/subtitle-font-dialogue-before/`. The earlier injected subtitle
was queued before gameplay initialization and is not evidence of text rendering.

Scanning the English, German and French LEVEL1ON sound descriptors found no
embedded ASCII control characters. English includes repeated sentence spaces,
including three/four-space runs. A previously captured English conversation
also shows doubled sentence spaces. These are source text rather than proof
of a renderer transform fault.

Changes:

- Normalize whitespace in the already wrapped native subtitle display buffers.
  Source sound resources and timed pages are untouched. Unterminated or
  out-of-range guest buffers are left untouched.
- Extend font 97's installed-TTF replacement to the Latin-1 alphabet. Private
  cell-sheet inspection confirms the accented alphabet's locations, including
  e-acute at native x=81, y=154. Preserve the preceding legacy symbols and final
  engine/controller glyphs. Native advances and glyph rectangles are retained.
- Remove the background-width exemption from long subtitle text batches in
  both D3D11 and WebGPU. This is an identified code-path inconsistency; the
  captured farmer conversation alone does not prove it triggered that case.

CPU validation: 21 disc/font checks pass with Arial, including all 157 source
ink cells in supported ranges, GS alpha limits and exact preservation outside
editable cells. Prompt/text suite passes 72 checks with the ISO. Narration's
existing render suite passes 131,040 glyph bounds across four aspects, five
sizes and three output heights. Repository source audit passes.

The eight relevant CTest suites pass (font atlas, prompts, HUD page, settings,
settings schema/menu, narration render and subtitle visibility). The required
incremental build/deploy completed successfully using `build/final-game` and
`dist/DrakanPC`; concurrent test runs initially locked the destination DLLs,
so deployment waited without terminating those processes.

The German farmer conversation completed with exit 0 using the generated
157-glyph plain atlas, with intact centred dialogue in renderer captures under
`build/harness/subtitle-font-dialogue-after/`. This is a scene check rather
than whole-game or all-language coverage. WebGPU compiled successfully; live
capture validation here used D3D11.

The English conversation also completed successfully. Its original SetSubtitle
input contains doubled spaces after sentences; the rendered display uses single
spaces and remains centred on both lines. Captures and logs are private under
`build/harness/subtitle-font-english-after/`. Audio/page timing is still driven
by the guest controller.

Private probes and source-only analysis output are under `build/subtitle-font*`.
No disc artwork, transcripts or generated game code are added to the repository.
