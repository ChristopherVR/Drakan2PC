# Controls freeze and native menu typography — 18 October 2026

Installed the tested executable in `dist/DrakanPC/drakan.exe`. Settings and memory cards were preserved; rollback details are in `build/controls-font/receipt.json`.

Mouse radio clicks now synthesize native confirm input after focusing the radio. The earlier synchronous press callback entered the checkbox sound resource wait on a copied, non-preemptible context; the baseline stalls inside `0x1b7f30` after selecting Arokh.

The SCES-500.06 Controls handler widens Rynn/Arokh radio widgets to 102 units on the 512-unit native canvas. Decorative font 96 now receives a 2048×2048 runtime atlas rasterized from installed Palatino Linotype, preserving native cell bounds, advances and markers. Missing fonts fall back to original artwork. Rynn/Arokh focus text bypasses the retail small intermediate fire-text texture and draws directly from the sharper atlas. Their checkboxes retain the selected-state cue. No font files or disc pixels are distributed.

Validation: 3,178 input checks, 19 font-atlas checks and 68 prompt checks passed. The final private live fixture verifies screen 10 selection 0 → 1 → 0; Escape returns to screen 9. Presented-frame captures show both names fit and selected Arokh remains sharp. Evidence: `build/controls-font/final-04/`.

Executable SHA-256: **ED83541BCD6D81EE743A85FB30F2FE6874C027BF962D853FF358FED8790CBA51**.

The installed build was checked on the English title-menu Controls page at 1280×720. Accented glyphs retain original artwork. Other active local edits were preserved in the shared working tree and build. No whole-game validation is claimed.
