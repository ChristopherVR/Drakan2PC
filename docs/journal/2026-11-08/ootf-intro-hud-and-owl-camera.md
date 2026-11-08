# OotF Intro: health and mana HUD, and the owl shot framing

Two reports from a New Game with Drakan: Order of the Flame chosen at the title: the health and mana orbs show during the
Intro, and in the owl shot the owl's head is out of frame. Both are converter defects; both are fixed in the converter
(formats `ootfseq.ts`, `ootfpipeline.ts`). Runs used a private two-level pack (Intro + Ruined Village, `--campaign 1`,
template LEVEL1NA) under `build/ootf-run/<name>/`; screenshots and contact sheets are private under `build/hud-owl/`.

## HUD during the Intro

**Evidence.** Baseline pack (`hudowl`, `hudowl2`): the orbs draw from the Intro load to the hand-over to Ruined Village, over the
black start, the Surreal logo scene, the owl close-up and the landscape, not just in gaps between scenes. The Ruined Village
opening scenes (same pack) draw no HUD.

**Cause.** The PC Intro has no player at all (its "Rynn" is an animated prop, engine class 17), so `ensureSingletons` clones the
template's Rynn into it. None of the Intro's three converted scenes names that player. The disc's own `Level 0 - Intro` has a Rynn
too, but its scene plays her as an actor (`Rynn`, ref 1, next to `camera`, ref 2), and every Ruined Village scene plays the
converted player (ref 1740, mode 1). The Intro scenes do carry a camera actor with a type 9 camera setup, so the camera action
is not what hides the HUD; the player being held by a running scene is. The HUD draw `0x3ac540` takes its hidden path on
player flags `+0x90` bit 3 / `+0x94` bit 1 (`0x4569a0` / `0x456990`), which fits a scene-held player; the flag writer was not
traced.

**Fix.** `SequenceConvertOptions.holdPlayerObjectId`: every converted scene of a level without a PC player gets one more actor,
`Rynn`, ref = the synthetic player, mode 1, a single run/stop-AI action 0 at t = 0 (the level ends before it would be
released). The pipeline predicts the synthetic player's id (next free id, the one `ensureSingletons` gives it) and warns if
the placed player ends up with another id. Only the Intro is affected: every other campaign level has a PC player.

**Result** (`hudfix`, `hudfix2`, `hudfix3`): no HUD at any point of the Intro. In `hudfix3` (New Game and Solo pressed by the
script, no other input) the Intro loads at 24.2 s, stays black for about 11 s, then plays the owl face, the Surreal logo,
the owl close-up, the perched owl and Arokh's fly-by, fades and opens Ruined Village at 68.3 s, all without the orbs.
Ruined Village opens as before and the orbs come
back with control (`hudfix`, from about 110 s with the harness taps, and visible in every later shot to 340 s). Side effect, as in
the disc Intro: a scene that holds the player can be skipped with Start / Cross. Skipping one Intro scene starts the next (their
trigger messages still fire); after the last one the camera rests on the landscape until the 44 s hand-over timer fires, with
no HUD.

## Owl shot framing

**Evidence.** Baseline `hudowl2` at 14 s and 20 s after the title: the camera sits in the owl's chest and later shows only
its lower body on the branch; the head is above the top edge. The other converted scenes are equally tight (Ruined Village
faces cut at the frame edges).

**Cause.** PC sequences have no field of view; they play at the level's `Default Field of View (deg)`. That field is 80 in the
Level Properties class of all 23 PC levels, the Intro included (and 80 is also the PS2 class default and the measured PS2
gameplay field of view, `reference/widescreen.md`). The converter inserted the PS2 camera setup (type 9) with 40, the most
common disc value, so every converted shot was about 2.3 times narrower (tan 40 / tan 20). The type 9 value is a field of view in
degrees: the scene tick `0x1a68d8` passes it (interpolated between keys) to the render object's FOV setter (vtable `+0x54` of
`*(0x5565c8)+0x124`). The camera keys, the owl's position and its model were not wrong: positions and quaternions are copied
unchanged and the shot frames correctly once the angle is right.

**Fix.** `pcLevelFov(pc)` reads the PC Level Properties field of view (else 80) and the pipeline passes it as `cameraFov`; the
default in `ootfseq.ts` is now 80 (`PC_DEFAULT_FOV`).

**Result.** `hudfix2` at 13 s and 15 s: the whole owl with its head, then the perched owl in the landscape with the waterfall,
the framing the PC shot was authored for. Ruined Village scenes now show both characters in the conversation shots.

## Verification

* formats: `test/ootfseq.test.ts` (default field of view 80, `pcLevelFov` from a Level Properties class, held synthetic player
  added once with mode 1 and one run/stop action 0); the full formats suite passes.
* Game: the user's `dist/DrakanPC/drakan.exe`, private packs only, clean exits (exit 0, scripted quit).
* `scripts/harness/ootf_level_run.py` took the whole `levels = [...]` list as the start title for a two-level pack, so it
  printed FAIL for runs that loaded both levels; it now takes the first entry.

## Open

* The PC Intro could also be skipped as a whole on the PC (not checked); here each scene is skipped on its own and the last view
  waits for the timer.
* The owl close-up shows some ghosting on the tree trunk next to the owl in a fixed-pack shot; not examined (renderer lane).
