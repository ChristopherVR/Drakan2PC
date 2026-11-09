# OotF Ruined Village Book: black 3D view after the level-change save

User report: using Atimar's book in Ruined Village opens the "In order to progress beyond this point the game must be saved"
prompt, the player saves, and the next level loads with most of the screen black and nothing playable.

## What the log said

`dist/DrakanPC/drakan.log`: exit `OotF 01 - Ruined Village` at 279392 14761 152908, `ConfirmSave(2)`, level
`OotF 02 - Ruined Village Book` loaded and playable (`gui screen 14 -> -1`, gameplay 1). So no crash and no hang: the guest
runs, the renderer draws, and the image is black. The saved slot (`OotF 02 - Ruined Village .rsg`) loads the same black level.

## Reproduction

Private copy of `dist/DrakanPC` in `build/ootf-run/play-book` (the user's exe and ini), then a scripted run: New Game, `place`
at the staircase exit (recipe `place 279500 15200 152908`), `sync screen 2`, Cross on the prompt, Cross on the modern save
screen. The harness taps do not reach the prompt on their own (autopress stops once Rynn is under control), so the recipe uses
`pad tap CROSS`. The Book then shows black above a purple 15 % strip with the HUD orbs (boots of Rynn at the strip edge).
Stills in `build/review/book/`.

## Bisect

| Pack | Book view |
| --- | --- |
| installed pack (23 levels), Intro + Ruined Village + Book, Intro + Ruined Village + Book + Wartok | black |
| Ruined Village + Book only | fine (void, Rynn, the painting board at the edge) |
| Book alone, New Game | fine |
| 3-level pack, paintings dropped | black |
| 3-level pack, all Faders dropped | fine |
| 3-level pack, only `fader object 2` dropped | fine |

The level data of the Book is the same bytes (apart from class ids) in all packs, so the pack-dependent part is not the level.
`fader object 2` (PC object 2427) has Fade In 1 s, Faded Time -1 (hold until trigger), Fade Out 0, Start Mode 1, colour black:
the end-of-scene fade to black. It and `fader object` (2425, the black start fade) are actors of the Book's only sequence,
"backstory" (250 s, 9 paintings, camera, Rynn). The PC level has no Stomp Player for it (the engine starts it itself), so the
converter converts no sequence for the Book at all (`sequences` stage only converts what a Stomp Player lists). The faders
stayed behind as plain objects; in the larger packs the end fader runs from the level load and holds black for good.
Why a two-level pack does not show it was not found (identical class records, ids differ by a few); the black appears with
the Intro loaded earlier in the same process, so stale state from the Intro is a candidate (a held player scene, see the
attach-bit finding of the hair work), but dropping the faders removes the symptom in every pack tried.

The screen is black but the world runs.

## Fix

Converter (formats `d292c2e`): `unplayedSceneActors` (ootfseq.ts) and a pipeline step after the sequence conversion. A model-less
Fader that is an actor only of sequences no Stomp Player of the level plays is left out of the converted level. Log line:
`<level>: 2 scene fader(s) of unconverted sequences left out`. Test: `test/ootfsceneactors.test.ts`. Only the Book is affected in
the full conversion (the Intro's and other levels' faders are actors of converted sequences).

## Second defect: the Book never ended

With the picture back, the level sat in its void for good. The converter's 28 s Timer (message 5) reaches the exit's class handler,
and `exitHandlerEntry` (drakan_levelchange.cpp) takes such a message through the host path only when the player is more than 8000
units from the exit. The Book's player stands 130 units from it (the PC level puts her there), so the timer was ignored.
`exitHandlerEntry` now also takes it in `Ruined Village Book`. Verified in the full 23-level conversion
(`build/ootf-run/bk2/fullfix2`): prompt, save, Book fills the screen and Rynn moves, the exit fires after about 28 s and
`OotF 01 - Ruined Village` loads again.

## Evidence

`build/review/book/before_black.png` (installed pack), `after_full_pack.png` (prompt, Book at +0..+46 s with the regenerated
full pack), `reload.png`. Private runs under `build/ootf-run/bk2/` (`three` black, `nofade2` / `fix` fixed, `fullfix2`).

## Loading the Book save

Loading `OotF 02 - Ruined Village .rsg` from the title (native load screen) restores the Book, but the level-change prompt
(gui screen 2, "In order to progress...") comes up at once because the save was written standing on the exit and the
arrival guard did not hold on that path (no `touched before the player walked off` line). The pause-menu load in the user's
log did hold. This is independent of the fader and predates it; a separate agent is on OotF save loading.

## Still open

* The Book's "backstory" cut scene (paintings, narration, 250 s) is not played; the player stands in a grey void until the 28 s
  timer returns her to Ruined Village. The right fix is a Stomp Player for the PC level's own start sequence.
* Saves made before the regenerated pack are refused (the pack build is recorded in the save).
