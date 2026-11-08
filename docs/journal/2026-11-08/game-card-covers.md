# Choose-your-game covers from the player's disc

The card reads `previews/campaign-<id>.png`, else `previews/game-<id>.png`, but nothing shipped them.

* **Drakan 2** (done): the title backdrop is the 3x2 mosaic of 256 x 256 24-bit textures 72..77 in the English
  `INTERFCE.TXD` (rows stored bottom-up; the mosaic is 768 x 512). `analysis/scripts/title_cover.py` rebuilds it, keeps the top
  768 x 432 (16:9, without the copyright lines) and scales to 1280 x 720 with a bilinear filter, standard library only.
  `Build-LocalGame.ps1` runs it in the PAL branch next to `previews/chars` and copies the result to the play folder as
  `previews/game-d2.png` explicitly (the `previews` folder is on the keep list, so the generic copy skips it). Missing or
  unexpected tiles (another disc layout) write nothing and exit 0, so a build never fails on it. Test:
  `scripts/tests/test_title_cover.py` (synthetic tiles).
* **Order of the Flame** (not done): the Drakan 1 install used for the converter (`build/ootf/cab_disc`) holds no title or
  logo picture. `Dragon.rrc` has only level parchment maps, loading and menu frames, weapon icons and a Surreal logo;
  `Drakan.exe` resources are small icons; the other TXDs are level/model textures. The title is therefore not
  extractable from that install (the retail title art is likely on the CD as video). Until a source is found, the Order of
  the Flame cover is supplied by hand as `previews/game-ootf.png`, or by a pack as `previews/campaign-ootf.png`.
