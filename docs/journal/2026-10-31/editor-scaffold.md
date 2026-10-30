# editor/ scaffold (TypeScript + Vite + three.js)

Spec: `docs/proposals/level-editor-and-multiplayer.md`. Run instructions: `editor/README.md`.

- `packages/formats`: `ByteSource` (buffer and `Blob.slice` backed, so a full ISO is never loaded), `Iso9660`, `Srsc`
  reader + writer, `Txd` → RGBA8, `LevelList` / `.DB` dependency lists with 8.3 name mangling.
- `packages/app`: inspector. Pick or drop the ISO; parsing runs in a worker. Level list, per-file SRSC entry table,
  paged hex view, textures as quads in a three.js viewport with OrbitControls.
- 21 vitest tests on synthetic fixtures. Versions pinned exactly (TypeScript 7.0.2, Vite 8.3.1, three 0.186.1, fflate
  0.8.2). vitest stays on 3.2.4 because npm 11.3 crashes installing vitest 4/5 ("edgesOut"); bump with npm.

Smoke test against local extracted disc files (not an ISO; the app's ISO path is untested on real data, and the UI was
not opened in a browser yet): all 10 SRSC files re-serialise byte-identically; the 14 levels in `LEVELLST.TXT`
resolve; every texture decodes.

## Differences from the Python tools

1. `analysis/scripts/txd_decode.py` rejects three `DRAKAN2.RRC` textures (16 and 32 bpp) with a size mismatch: their
   pixel plane is `u32 length + zlib`, the layout `hd_pack.py` already handles for 8 bpp. The TS decoder accepts it
   at every depth. `txd_decode.py` should get the same fix.
2. TXD rows are stored bottom-up (`runtime/ui` `Txd.h`); `txd_decode.py` does not flip them. The TS decoder returns
   top-down rows by default.
3. fflate does not verify the zlib Adler-32 and returns empty output on garbage, so `formats` checks it explicitly.
