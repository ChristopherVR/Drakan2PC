# UE5-style PBR material pipeline (2026-11-01)

Direction: textures built into UE5 style from today. Reference: `docs/reference/hd-textures.md` section 10.

* Tool `analysis/scripts/pbr_pack.py` (Python, ruff clean, tests `scripts/tests/test_pbr_pack.py`). Chosen over a TS tool because it
  reads the HD pack pipeline's `index.json` and PNGs directly, needs numpy/PIL only and runs offline on the whole texture set; the
  editor does the preview from the JSON, so no heuristic is duplicated.
* Surdana run: `--select LEVEL1ON,LEVEL1TS` over `build/hdpack` + `dist/DrakanPC/textures` (HD present for all 287): 287 materials,
  8 contact sheets `build/pbr/contact_00..07.png`. Blend classes 282 Opaque / 3 Translucent / 2 two-sided Opaque.
* Editor: Files tab Materials view. Screenshots (browser dev server, pack subset served locally and loaded through the file input):
  `build/pbr/preview_rocks_sphere.png`, `build/pbr/preview_stone_plane.png`.
* Observations: normals read well on stone / brick / cobble, are subtle on smooth areas (flat ones are written flat). LEVEL1TS atlases
  contain a magenta filler region (unused by the game); the material is correct for the used regions. Metallic is 0 everywhere
  until an override says otherwise: nothing in the textures identifies metal.
* Contract for the glTF / UE5 export agent: read `Materials/M_<Name>.json` (field list in the reference doc), look the texture up by
  `textureHash` (HD pack file name) or `sources` (`<TXD path>#<texture id>`), honour `blendMode` / `twoSided` but OR in the model
  polygon's own double-sided flag; normals are DirectX (UE5 native; glTF wants OpenGL, so flip green when writing glTF).
* Not done: game renderer use of the normal map (needs vertex normals / tangents; see the reference doc).
* Verification: python test 5/5, vitest `materialpack.test.ts` 4/4 (full vitest run: 548 pass, 2 failures in `animlayer` unrelated to
  this work, a 9 s timeout under load), `vite build` of the app ok. `tsc` on `editor-core` reports duplicate `Vec3` / `Transform`
  exports from other agents' in-progress work.
