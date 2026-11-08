# OotF scenes: Rynn slides, her sword goes through her, her feet sink, her mouth does not move (2026-11-08)

User report on the OotF campaign: in the Ruined Village opening (after the Intro) Rynn slides across the floor while she and
her brother walk, and her mouth does not move; when she draws her weapon it shows through her body; her feet are under the
terrain. All four come from how converted PC scenes (SSD sequences) address the PS2 player. Fixed in the converter (formats
`ootfseq.ts`, `ootfpipeline.ts`, `ootfsound.ts`); nothing in the runtime changed.

Runs are private under `build/ootf-run/rynn-*` (two-level pack Intro + Ruined Village, `--campaign 1`, template LEVEL1NA,
from a private copy of the play folder, `build/rynn-play`). Nothing from either game is quoted here.

## Sliding: the player's scene clips named the wrong database

A sequence animation ref resolves in the database of the actor model's own ADB. The PS2 player is the PS2 Rynn of
`SYSTEM.MOD` (65 joints), so her clips are `SYSTEM.ADB` ids (615 clips, ids 243 to 2306; the disc's Rynn scene clips are all
db 0 there). The converter mapped the PC Rynn's scene clips through the clip map of the PC Rynn model it had converted as an
own-rig creature (33 joints, clips in `RESOURES.ADB` / `NPCS.ADB`, ids 6270 to 6556): in the user's pack Rynn's walk
(`stompwalk`) was 6383, an id `SYSTEM.ADB` does not have. No clip played, so she glided along the scene's transform keys in
her standing pose. Delon (own converted model, own clips) walked.

Fix: `OotfPs2Context.playerMod` / `playerAdb` (`SYSTEM.MOD` / `.ADB`, kept apart from `mods` / `adbs` so the player is never
name-mapped or overridden, see the note in `ootfdisc.ts`). For the scenes of each level the converter collects the clip refs
of every actor that resolves to the player (also the "Wrynn" alias), retargets those PC clips onto the PS2 Rynn skeleton
(`retargetCreature`: every PC joint has a same-named PS2 joint; face, fingers and sheaths keep their bind pose) and ships them
as new `SYSTEM.ADB` clips past its last id, with its 0x0500 header (count, highest id + 1) updated. Ruined Village: 57 clips,
ids 2307 to 2363. The CLI reads them from `<ps2>/../player` (`--player`); `ootf_level_run.py` extracts them there; the
editor's import dialog takes them from the template's SYSTEM dependency.

## Sword through her chin: attach points are skeleton channel indices

The attach action (type 4) is `(actor slot, attach point)`, and the attach point indexes the attaching actor's skeleton
channel list, as does word 0 of an animation action. The two Rynns list channels differently: PC channel 4 is `Rweapon`, PS2
channel 4 is `head` (PS2 `Rweapon` is channel 21; every disc sword attach uses 21, `Rynn_Trapped` attaches to the head with
4). The opening's "Rynns Sword" attach (PC 4) was copied as is, so the drawn sword hung from her head and passed through her
neck and chest (`hudfix3/run/t080000.png`, `t089000.png`). Likewise her talk clips run on PC channel 2 (`neck`), which is PS2
channel 3.

Fix: `skeletonChannelMap` (PC channel -> PS2 channel on the joint of the same name) and `SequenceMaps.channelMapForActor`;
the pipeline supplies it for the player. Own-rig converted characters keep their PC channel list, so they need no map. An
unmapped channel is kept and reported.

## Feet under the terrain: same cause

The scene keys place Rynn's origin where the PC clip expects it: 500 units over the ground standing (PS2 gameplay: 498 in
both Surdana and Alwarren, `rynn-feet*`, so neither the terrain nor the character origin differs), but 242 while she crouches
in her fighting stance (`1handedSL`, scene 1.1 at 39.7 s). Without the clip she stood upright at that height, with her boots
below the ground (`hudfix3/run/t089000.png`). With the clips she crouches and her feet are on the ground
(`rynn-b/run/t092000.png`). This is not the floating-grunt problem (creatures), which this change does not touch.

## Mouth: lip-sync tracks and the lip-sync bit

The PS2 game moves a speaker's jaw from a 0x0305 track per speech sample. Decoded from the PAL code (loader `FUN_001b3f60`,
jaw tick `FUN_00404e08`, sequence sound handler `FUN_001a4c50`): `u32 0, u32 rate, u32 step (rate / 25), u32 95 (skipped),
u32 n, n x u16` mouth openings, 25000 = the class's `Max Jaw Angle` (Rynn: -10 degrees on `Jaw Channel` 11), read 2500
samples ahead and interpolated. A value is the peak |sample| of the first half of its 1/25 s frame: rebuilding all 757 disc
tracks from their own samples this way gives a mean error of 1.9 % of full opening (89.8 % of frames within 5 %). The old
reference text ("4-bit mouth shapes") was wrong and is corrected in `reference/sound-debug.md`. A sequence sound action binds
the track only when its flags have bit 0x20 (disc speech 0x38 / 0x28); bit 0x10 means "not attached to the object".

Converted speech had no track and its PC flags (0x10 / 0x18) no 0x20. Fix: `encodeLipSync` writes a track for every
non-looping converted sound (`Ps2Sound.lipSync`), and the player's sound actions with the PC speech bit 0x10 get 0x20
(`SequenceMaps.lipSyncForActor`). Only the player: other converted characters' classes were not checked for a jaw channel.

## Evidence

| Run | Pack | What |
| --- | --- | --- |
| `hudfix3` (earlier session) | before | Rynn glides in a standing pose (`t072000`, `t074000`); sword through her neck (`t080000`); boots under the ground (`t089000`) |
| `rynn-b` | fixed, audio on | Rynn walks with stepping legs beside Delon (`t060000` to `t065000`), kicks, holds the sword in her right hand in a crouched stance with her feet on the ground (`t087000`, `t092000`); comparisons `rynn-b/before_after_walk.png`, `before_after_draw_feet.png` |
| `rynn-c` | fixed, audio on | RAM at 69.5 s: Rynn's lip controller (data object + 0x210) has jaw channel 11, max -0.175 rad, a voice handle and a bound converted 40-entry track (values 12726, 6439, ...); at 68.5 s the 157-entry track of her first line |
| `rynn-feet`, `rynn-feet-d2` | user's pack / no pack | player origin over the collision terrain: 498.9 (Alwarren), 498.0 (Surdana) |

Not verified: the jaw motion itself in a frame (the speech in scene 1.1 plays in wide shots; the close-ups come between
lines), and drawing a weapon in ordinary play (no weapon was in the inventory in the direct-start Alwarren and the Ruined
Village runs, `rynn-weap`, `rynn-d`). The sword fix concerns the scene attach; the gameplay hold uses the PS2 Rynn's own
attach and was not reported as different from Drakan 2.

## Open

* Arokh in scenes may have the same database problem (his PC scene clips map to converted own-rig clips while the actor is the
  PS2 Arokh); not checked.
* Lip sync for other converted speakers (Delon, the Wartok, NPCs) needs their PS2 template class's `Jaw Channel` checked
  against the converted rig before the bit is set for them.

## Follow-up: the Intro's Rynn, the sword's grip, the jaw level (same day)

User test of a pack converted from formats `98661d6`: "the cinematics don't seem to contain PS2 Rynn with lips moving" and
"the weapon is now equipped but facing the wrong way". Fixed in formats `d69bec4` (plus `c7fd626`, see the note at the end).

**Which scene still showed the PC Rynn.** A census of every campaign level's scene actors named Rynn (PC object, engine class,
model) finds one that is not the player: the Intro's "Intro 3" plays obj 2853, an engine class 17 prop whose model is the PC
Rynn (3713). The Intro has no PC player, so its scenes only held the synthetic PS2 player. Now, in a level without a PC player,
a prop of a class named Rynn is replaced by the synthetic player: the scenes address the player (with the clip retarget,
channel map and lip-sync bit), the player stands where the prop stood (Intro: 200448, 207035, 164352) and the prop is dropped.
The scene still holds the player (a held actor that is also a PC actor gets the run/stop action 0 at t = 0). Every other
campaign scene Rynn already is the player.

**Sword facing the wrong way.** PC and PS2 Rynn share every bind frame within a few degrees except the weapon joints: the PS2
`Rweapon` is turned about 104 degrees (mostly about its y axis) from the PC one. The retargeted scene clips carried the PC
deltas on the PS2 bind, so the PC sword in her hand pointed back along her forearm (`rynn-b/run/t087000.png`). As formats
`e5d7f00` does for PS2 motion, `retargetPcAnim` now takes `worldJoints`, and the player's clips give `Rweapon` / `Lweapon` the
PC joint's model-space orientation: the blade comes forward out of her fist
(`build/ootf-run/rynn-e/sword_before_after.png`; test `ootfretarget.test.ts`).

**Jaw too small.** The converted tracks were half the disc level (90th percentile median 16,683 against 29,226 / 30,935 for
SYSTEM / LEVEL1ON speech), so the jaw opened about 5 degrees. `encodeLipSync` now scales a track so its 90th percentile is
30,000 (at most 4x).

**Jaw verified in frames.**
* `rynn-g` (unmodified pack, scene 1.3, her line at 87.6 s, 2560 x 1440, internal scale 1): `jaw_strip.png`, mouth closed
  before the line, then open at 248.05 to 248.35 s while the controller reads 26,858 to 27,354.
* `rynn-i` (test fixture, not shipped: her 1.1 line moved from 1.5 s to 8.6 s so it plays over the face close-up that follows
  the walk; in the authored scene the line ends before the close-up): `jaw_closeup_strip.png`, 26 frames 100 ms apart, mouth
  closed before the voice (53.8 to 54.1 s), then opening and closing with the track (54.5 s: 25,465; 57.0 s: 32,767).

**In-game sword (ordinary play).** `rynn-f`: Alwarren direct start with the converted Mithril Long Sword pulled next to Rynn
(test aid `DRAKAN_DBG_PULL`), picked up with E, equipped in the inventory (Tab, close the first-time help, Enter, Tab), attack.
She holds it blade up out of her fist at rest and swings it blade first (`build/ootf-run/rynn-f/gameplay_sword.png`): ordinary
play runs native PS2 clips, whose `Rweapon` motion already holds PC-authored items the right way, so nothing changed there.
The wrong grip was only in the retargeted scene clips.

**Commit note.** `d69bec4` was built from a stale base of `ootfpipeline.ts` and dropped the death-pose placement of `6ab2b0f`
(restored unchanged in `c7fd626`). It also swept in another session's staged `ootfmap.ts`, `ootfpuzzle.ts`, `index.ts` and
`test/ootfmap.test.ts` from the shared index; their `ootfpipeline.ts` part of that work was not in it and stays uncommitted in
the working tree.
