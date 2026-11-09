# Builder: Order of the Flame and HD textures (2026-11-09)

The guided builder (`DrakanPC-Setup.exe`, `Build-Drakan.cmd`) now offers both extras; before this a user with an ISO got
neither, and both needed a developer checkout (npm workspaces, numpy, Pillow, Real-ESRGAN by hand).

## What was built

* `scripts/buildtool/extras.py` (standard library only): source detection for Drakan 1 (install folder, disc folder, `.mdx`
  / `.iso`, zip), unpacking, the converter run with resume state, pinned on-demand downloads, HD index and upscale passes,
  per-step time and disk estimates, `build/extras-timings.json`.
* `Build-Drakan.ps1`: `-OotfSource`, `-HdTextures`, `-HdGpu`; the pack is converted before the game build and installed
  through `-ModDirectory`; HD runs after it. A failure of either is reported and does not fail the build.
* Installer: an "Order of the Flame" page (edit plus folder / file browse buttons) and an HD checkbox; compiled with ISCC.
* `analysis/scripts/ootf_ps2_data.py` (template extraction, moved out of the harness), `ootf_cab.py` gained
  `extract_image` / `find_cab_file`, `ootf-convert.ts --hd-sources-only 1` (write the lossless sources, skip the converter's
  own HD run), `hd_pack.py` reads `DRAKAN_PY_SITE` for a private numpy / Pillow folder.
* Tests: `scripts/tests/test_builder_extras.py` (50 cases: GPU classes, estimates, sources, downloads and hash refusal, zip
  and tgz traversal, node stubs resolved by real Node, converter state and failure paths, HD passes, wizard text equals the
  estimates, script parameters); the kit smoke test also runs `extras.py estimate` under the embeddable Python.

## Measurements (this PC, RTX 5080, 16 logical cores)

| Step | Time |
| --- | --- |
| Level template from the ISO | 0.1 s (127 MB) |
| Conversion of all 23 levels, kit-like tree, kit Python and Node, no `node_modules` | 137 s to 168 s; the pack is byte-identical to the developer-tree pack (158,631,976 bytes, 11,224 files) |
| HD tools download (Real-ESRGAN 45 MB, numpy, Pillow) | 13.5 s |
| `hd_pack.py extract` (2,962 unique disc textures) | 12.9 s |
| `modextract` of the pack (5,371 unique with the disc, 1,080 lossless sources) | 25 s |
| Upscale | 40 textures in 9 s (about 0.22 s each, as the 616 s / 2,962 of 2026-10-17) |

numpy 2.0 imports from a private folder under the embeddable Python (`-I`, no `site`), through `DRAKAN_PY_SITE`.

## Findings

* The embeddable Python's TLS stack failed on the GitHub release redirect here ("unable to get issuer certificate") while
  `curl.exe` worked; `download()` falls back to curl / PowerShell and always checks the SHA-256.
* `@dataclass` with `from __future__ import annotations` needs the module in `sys.modules`; a test that loads a script by
  path must register it.
* Inno Setup's `/D...` switches are mangled by Git Bash; run ISCC from PowerShell.

## Not done

* The HD upscale step itself was timed only on the reference card; other classes are multipliers, not measurements.
* A full installer build and a first-run walk-through on a clean machine (Build Tools download, 1 to 2 hour compile) were
  not repeated; the new steps were exercised in a kit-like tree with the kit's tools and through `Build-Drakan.ps1
  -CheckOnly`.
* Multi-GPU laptops: the Vulkan device index is the user's choice (`-HdGpu`); the builder does not detect it.
