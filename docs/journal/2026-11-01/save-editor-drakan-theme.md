# Save editor reskin: Drakan's own inventory look

Feedback: the Save mode looked too much like AbioticEditor. The workflow stays (character panel, world lists with
pictures, pending edits until SAVE, `.bak`); only the skin changed.

Reference: the game's inventory screens (`build/harness/final-inv/inv_equip.png`, `inv_skills.png`): weathered grey
stone slabs, pewter rims with small diamond rivets, round pewter medallion tabs, recessed stone cells with a plain cream
frame on the selected one, cream-gold spaced serif lettering.

- `editor/packages/app/src/save/drakan.css` (new, imported by `saveui.ts` after `save.css`): own `--dk-*` tokens over the
  `--sv-*` ones, everything scoped under `#save-main.sv`, so the level editor stays UE5-styled. Dark app theme = dark
  iron and stone; light app theme = parchment and ink. Stone and parchment grain are inline SVG (feTurbulence) tiles, the
  rivets and tab rule are inline SVG diamonds; no game art is used or committed.
- Fonts come from the `@fontsource/cinzel` and `@fontsource/crimson-pro` npm packages (SIL OFL 1.1; no font binaries
  in the repo): Cinzel (display, numerals) and Crimson Pro (text). `@font-face` points into the packages and Vite
  bundles the files, so no network is needed at run time.
- Item cells are a seamless grid of recessed stone cells; equipped/quick-slot marks are bronze/pewter studs; the shared
  dropdown popup is restyled inside the save scope.
- Fixed a leak: the legacy `#sv-details` rule in `style.css` painted a white block in the light theme.
- Screenshots (before/after, both themes, 1600x900 and 1100x760): `build/harness/save-theme/`.
