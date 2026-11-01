# Dark Relic: a redirect clash, not a container limit (2026-11-01)

Follows [ootf-mp-arenas-followup.md](ootf-mp-arenas-followup.md) section 3 ("Not found: which limit ...").

## Bisect (all PAL, `scripts/harness/arena_two_instances.py`, map `relic-ground`, DRAKAN_GAME=d1)

| Run | What changed against a dark pack / a lit pack | Result |
| --- | --- | --- |
| `adbx-noadb` | the 4-level pack (mp5) with every `new/*.ADB`, `overrides/RESOURES.ADB` removed (RESOURES.ADB 8 MB, NPCS.ADB 32 MB gone) | still dark: not the ADB |
| `adbx-p1cls` | the lit arena pack (mp3) + 400 cloned class records in RESOURES.ODB (1344 -> 1762 classes, as many as the dark pack) | lit, 168 actors: not the class count |
| `adbx-p2sdb` | mp3 + 200 extra 30 KB `RESOURES.SDB` entries (+6 MB) | lit, 168 actors: not the sound DB |
| mpco-arena-relic | arena pack with a *small* RESOURES.MOD / TXD beside the campaign pack | dark: not the MOD / TXD size |

What did separate every dark run from every lit one: the **`campaign = "ootf"`** key. mp5 / mp9 / mp12 / mpco-levels carry it; mp3,
mp7, mp8, mp10, mp11 (arena with 0-2 other levels) do not. The world dump says so directly (`arena_before.json`):
`levelName: OotF 00 - Intro` in the dark runs (actors at 148992 4053 185344, scene `layers=18 waterLayers=10 triangles=24686`), the
level's own name in the lit ones (Relic scene `layers=32 waterLayers=0 triangles=672`). The `[arena] level up` / netplay lines still said
Relic because they take the name from the redirect, not from what the guest opened.

## Cause

`drakan_game::configure` calls `applyBase(false)`: in a d1 game with a campaign pack the file layer redirects `Level 0 - Intro` ->
`OotF 00 - Intro`. `drakan_overrides` had already installed the `DRAKAN_START_LEVEL` redirect (`Level 0 - Intro` -> arena level) on the same
single from/to pair, so the campaign redirect replaced it and New Game opened the campaign's Intro. The 3.5k-file packs only correlated.

## Fix

`ps2recomp/ps2xRuntime/src/lib/drakan_game.cpp` `applyBase`: with `DRAKAN_START_LEVEL` set the start-up call keeps the existing redirect
(the clear paths, `select` and the menu `redirect()`, are unchanged; the title-menu arena launch sets its own redirect after start-up and
was never affected).

## Verified

Rebuilt with `Build-LocalGame.ps1 -Incremental`, then the exact failing combinations (`build/harness/adbx-fix4`, `adbx-fixco`):

| Combination | Before | After |
| --- | --- | --- |
| one 4-level pack (Intro, Ruined Village, Wartok, Relic; RESOURES.MOD 9.96 MB / TXD 26.9 MB) | 110 actors, default start, black | `level up: 172 actors, player at 22533 202497 26629`, lit, `levelName` Relic |
| arena pack enabled beside the campaign pack (`ootf-levels` + `ootf-arena`) | 110 actors, black | 142 actors, Rynn's start, lit (`a_arena.png`) |

## Findings kept for later (no engine limit found)

* 9.96 MB RESOURES.MOD, 26.9 MB RESOURES.TXD, 8 MB RESOURES.ADB (1000 clips, 0x0500 count 1023), 1779 RESOURES classes, 8.6 MB SDB all
  load. A 400-class / 200-SDB-entry padding on an arena pack also loads. The converter does not need per-level container splitting for this.
* The campaign pack's RESOURES.* size is therefore only a memory / load-time matter, not a correctness one.
