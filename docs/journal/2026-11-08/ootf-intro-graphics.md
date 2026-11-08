# OotF Intro: mirrored logos and doubled text

User report: "the intro videos when running new game seems to have major graphical issues". There is no movie: New Game of
Order of the Flame plays the converted level `OotF 00 - Intro`, three scenes of `Intro Level.ssd` (Intro 1 Psygnosis owl,
plate and "Presents"; Intro 2 Surreal Software; Intro 3 owl, DRAKAN title, Arokh), then Ruined Village. See the
[Intro black start](ootf-intro-black-start.md) note for the scene list.

Runs: a private copy of the play folder (`build/ootf-run/introgfx-play`, the player's `drakan.ini` with DLSS, HD textures,
scale auto, windowed 1600x900; textures and movies by junction), private two-level packs (Intro + Ruined Village,
`--campaign 1`, template LEVEL1NA), `DRAKAN_GAME=d1`, `DRAKAN_AUTOPRESS=0`, New Game by script, shots every 0.5 s or 1 s.
Frames and contact sheets are private under `build/introgfx/`.

## What was wrong

With the installed pack (`user1`) and a pack from the current converter (`base`):

* **Intro 1 (0 to 14 s):** below the owl, a row of small Surreal Software logos and a large one, all **mirrored**.
* **Intro 2 (Surreal):** "a Production" printed as `RodduboA`: the word and its mirror image on top of each other.
* Intro 3 looked as authored (lightning comes from the PC's own Lightning Gen objects).

## Cause

The PC culls back faces; the PS2 draws converted models from both sides. Only the inward "Black Background" box had been
handled ([Intro black start](ootf-intro-black-start.md)). The Intro's title cards rely on culling too:

* The Surreal Software cards ("Sureal Soft 2" x4, Surreal Tile, Outline, Small, Soft 3, the Surreal Logo morph plug) are
  single-sided planes placed behind the first shot's camera path, turned away from it. Intro 2 keys them in front of its
  camera. On the PC they are invisible in Intro 1; on the PS2 their backs, i.e. mirrored logos, showed.
* "a" (model 109) and "Production" (model 128) are flattened boxes: two triangles facing -Z and two facing +Z in the same
  plane, plus eight degenerate side triangles. The PC draws one side; the PS2 drew both, the mirrored word over the word.
* The polygon double-sided flag is not the cause: a pack without the "alpha textures are double sided" rule
  (`dsa0`) drew the same picture.

## Fix: culling in the data (`ootfcull.ts`, formats)

Only levels with no PC player (`isCinematicLevel`: the Intro). For every scene a Stomp Player of the level plays, in play
order (a scene that sends the play message 4257 to a Stomp Player is followed by that player's list):

* Samples at 30 per second plus every authored show / hide instant: camera position and view direction, each placed
  model's pose (its placement, stored Y minus 90, or its transform keys) and its authored visibility.
* A polygon counts as drawn when its front faces the camera and a corner, edge midpoint or its centre lies within 84
  degrees of the view direction.
* **Polygons never drawn while their object is shown are left out** of the converted model (only classes of the level's
  own database that no playable level of the batch places, not morph models, not models wholly unseen). "a" loses 10 of
  12, "Production" 10 of 12, the title letters and the owl tree some back faces.
* **Objects with no drawn polygon are hidden** for that stretch: the scene's own show / hide actions of such an object
  are replaced by a timeline (`applyCulling`), and an actor is added for objects the scene does not name. Only drawn
  classes (Ground Object 17, Rock 35, Morph Plug 99, model faders 136): sound objects carry an editor "Sound" box model,
  and a show action put those boxes on screen in a first attempt.
* Conventions, checked on the Intro data: a transform key turns an object by the conjugate of its quaternion (the D letter,
  stored Y 261, is keyed at its placed position with (0, 0.997, 0, -0.078); the owl face, stored Y 0, with (0, 0.707, 0,
  0.707)). A camera looks along its local -Z turned the same way (yaw -90 cameras look down -X at the owl; the Intro 3 key
  (0, 0.996, 0, -0.086) looks at the title letters). A first try with +Z turned by the quaternion itself hid the title.
* `OOTF_NO_CULL=1` turns it off (test aid). Converter log: `cull <class>: n polygon(s) never seen from the front left
  out`, `cull <scene file>: n show / hide timeline(s) in 3 scene(s)`.

## Result

`base` (culling off) and `cull4` (on), same working tree, same executable, same settings:

| Shot | base | cull4 |
| --- | --- | --- |
| Intro 1, 3 to 13 s | mirrored Surreal logos under the owl | owl, plate and "Presents" on black only |
| Intro 2, "a Production" | word and mirror image overlaid | "a" over "Production", readable |
| Intro 2, Surreal logos | flying logos | the same |
| Intro 3 | owl, title, Arokh | the same picture (a few twigs of the owl tree that face away are gone) |

Both runs load the Intro and Ruined Village and quit by script. Pairs: `build/introgfx/base_vs_cull.png`,
`build/introgfx/pair_t005000.png`, `build/introgfx/pair_t031000.png`; Production close-ups `build/introgfx/crop_prod.png`
(before) and the `cull3` / `cull4` frames at 21 s.

Tests: formats `test/ootfcull.test.ts` (key and placement conventions, camera direction, a title box loses its back, a plane
seen from behind is hidden and shown when the camera comes round, authored show instants kept, an actor added, play order).

## Open

* Model faders still pop instead of fading (Psygnosis plate 4 s, Surreal Mask 2 s, title letters 2.5 s); no PS2 class
  that fades a model's alpha has been found.
* The pool under the Intro 3 waterfall shows as a flat pale surface and the shot is framed wider than in the installed
  pack of 17:53; both come from converter changes made since (seen with culling off too), not looked at here.
* Aspect ratio switching between 4:3 and widescreen while loading (separate user report): not looked at here; the runs
  were windowed and only sampled every 0.5 to 1 s.
