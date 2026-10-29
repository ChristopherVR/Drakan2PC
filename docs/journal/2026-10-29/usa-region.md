# USA disc (SCUS-971.28): investigation, region profiles, first build

Reference: [regions.md](../../reference/regions.md). Inputs: the user's PAL ISO (3,453,943,808 bytes) and USA ISO
"Drakan - The Ancients' Gates (USA).iso" (1,867,776,000 bytes). Extracted ELFs and Ghidra project stay under
`build/usa-probe/` and `tools/ghidra_proj_usa/` (ignored).

## 1. What differs

* `SYSTEM.CNF`: `BOOT2=cdrom0:\SCUS_971.28;1`, `VER=0.10`, `VMODE=NTSC` (PAL: `SCES_500.06;1`, `VER=1.04`, `PAL`).
* USA root: `SYSTEM.CNF`, `SCUS_971.28` (LBA 570429), `DRAKAN2.BIN` (LBA 572, 1,167,065,088 bytes), `IOP\IOP.IMG`,
  `IOP\VSSVER.SSC`, `IOP\MODULES\*.IRX`, `ENGLISH\...`. The ENGLISH tree's file records point inside the
  `DRAKAN2.BIN` extent, so name lookups work unchanged. PAL has five language trees and `IOP\IOPRP23.IMG`.
* Build stamps `Dec 17 2001` (USA) vs `May  3 2002` (PAL); Sony SDK 2.2 vs 2.3. Strings only in PAL: jaw/speech
  animation members (`m_fDoJawWiggling`, `szSubtitleText`), `FlankingAI`, `PlayerControlState`, "Load Settings";
  only in USA: `FlyingAI`, older libcdvd messages.
* `.text` +0xe70, `.data` −0x1aa8, `.bss` +0x1380. VU1 microcode identical (same VU image SHA-256 `8a2dceea…`).

## 2. Matching

Ghidra (same scripts as PAL, project `tools/ghidra_proj_usa`): 12,128 functions, 99.422 % coverage, 185 jump
tables. `region_map.py`: 12,144 of 12,173 PAL functions mapped (6,815 exact, 5,241 neighbour, 47 call site, 41
fuzzy). 11,994 mapped starts coincide with a Ghidra USA function of the same size. All 171 PAL jump tables in
matched functions reappear at the mapped `jr` with the same target count.

The first data pairing paired immediates from "callsite" bodies too and produced nonsense (0x56df00 → 0x97e6c0);
pairing is now limited to exact/neighbour bodies and forgets a register once anything but `lui` writes it: 3,198
data addresses, 5 with conflicting references (float constants, not addresses).

Struct layouts: inside matched bodies 2,407 memory accesses use an offset 4 lower in USA (179 offsets in
0x94..0x1118; e.g. `+0x1024` → `+0x1020`, `+0xab0` → `+0xaac`), 95 byte accesses 2 lower. So the PC features need
an offset audit, not just address translation.

## 3. Inventory of PAL-specific places

Unique guest-image addresses in non-test runtime code (excluding comments): 299 — 216 code (198 inside mapped
functions), 83 data. Largest users: `drakan_input_glue.cpp` 38, `drakan_inventory_views.inl` 40,
`drakan_overrides.cpp` 35, `gameui/Addresses.h` 33, `drakan_scene_camera.cpp` 26, `drakan_menus.cpp` 24,
`drakan_frame_rate.h` 21, `drakan_dual_wield.cpp` 21, `drakan_object_motion.cpp` 21. Renderer backends compare PAL
`sourcePc` values (0x139010, 0x161a78, 0x437170, …) fed by the scene-camera hooks. Disc identity: `prepare_iso.py`,
`prepare_local_runtime.py`, `toolchain.lock.json`, `Build-LocalGame.ps1` (`SCES_500.06` file name),
`main.cpp` / `drakanExtractBootElfFromIso` (ELF name), the game-override registration (ELF name), the web
`--preload-file` and `runtime/web/pre.js`. Save ids: `BESCES-50006` in `drakan_menus.cpp`,
`drakan_menu_screens.inl`, `drakan_quick_save.inl`, `drakan_save_previews.cpp` (all PAL-only features). Effort
estimates per area: regions.md "What is left".

## 4. Build and run

* Full build: `Build-LocalGame.ps1 -IsoPath <USA iso> -WorkDir build/usa-game -Compiler <final-game ps2_recomp.exe>
  -Jobs 2 -SerialVuSlices -NoWarnings`, exit 0, deployed to `dist/DrakanPC-usa`. Recompiler report: 12,128
  functions, 11,935 recompiled, 193 stubs, 0 unhandled instructions, 0 warnings/errors; jump tables "210 non-ra JR;
  181 ELF-verified, 1 csv-only, 28 unresolved". The four VU1 AOT slices took most of the time (the machine was shared
  with another build). Incremental rebuild after the `ps2_pad.cpp` / table edit: seconds.
* Run 1 (no input, 95 s, `GSR_WINSHOT`): boots, HLE opens the ISO (232 files, root `\ENGLISH`), the memory-card
  warning renders and reports the (empty, `DRAKAN_MEMCARD_DIR`) card as fine.
* Run 2 (`DRAKAN_AUTOPRESS=2000`, `DRAKAN_AUTOPRESS_UNTIL=14000`): past the warning and the logo to the **title
  screen** (Load Game / New Game / Options / Credits, "Sony Computer Entertainment America"), stable to the last
  screenshot at frame 4800.
* Run 3 (`DRAKAN_AUTOPRESS=1500`, `DRAKAN_AUTOSTART=1`, 170 s): New Game, intro, **Level 1A Surdana loads** with
  HUD; the autopress Start leaves the pause menu open at frames 4800-9000. No crash.
* The log shows `baseline overrides only`; the HLE's optional-file probes that fail (`Interface.ssd`,
  `PS2 Music.adb/.ssd/.vdb`, …) fail the same way on PAL.
* PAL regression: `Build-LocalGame.ps1 -WorkDir build/final-game -Incremental` reported "Recompiler inputs unchanged"
  (same config/metadata stamp), rebuilt, and a run from `dist/DrakanPC` installed the full PC feature set (input,
  menus, memory-card skip, logo movie).

Not tested on USA: keyboard/mouse or gamepad play by hand (the Win32 input module is part of the PAL-only input glue,
so keyboard input is expected not to reach the game; the harness used the pad stub's autopress), saving/loading,
audio quality, movies beyond the logo/intro, later levels, Linux and browser builds.

## 5. Follow-up: input, wrong-disc guard, disclaimers

* Before: with the USA baseline, injected arrow keys did not move the title cursor (the Win32 input module is part
  of the PAL-only glue; the stock pad stub reads raylib keys, which the D3D window does not feed).
* `installRegionInput` (`drakan_overrides.cpp`, USA only): libpad entry points → the input module, menu layer from the
  GUI screen pointer, mouse look as emulated right stick. Runs (`keybd_event` / `mouse_event` into `DrakanWindow`,
  screenshots via `GSR_WINSHOT`): `build/usa-key2` two Down presses moved New Game → Credits; `build/usa-play2`
  (autopress Cross+Start until 80 s, then keys) Esc closed the pause menu, W walked forward, 40 mouse steps of 25 px
  turned the camera, W walked again. Not tried: gamepad, custom bindings.
* F1 on USA opens nothing (the overlay's key handling lives in the PAL glue).
* Wrong-disc guard: the USA build with `DRAKAN_ISO` = PAL ISO logged the mismatch and showed the dialog
  "Drakan: wrong disc for this build" (process window title checked); the USA ISO still boots to the title.
* Widescreen, frame rate, PC settings, Tab/M, quicksave/save pages: not ported (regions.md "What is left": the
  widescreen hooks map cleanly, but `ctx+0x4b4` and other offsets differ per class and the feature is coupled to the
  renderer's PAL scene ids in files other work is changing).
* PAL after each step: incremental rebuild of `build/final-game` (generated code not recompiled) and a run from
  `dist/DrakanPC` with the full feature set to the title.
