# Converted levels with all objects never gave Rynn control: cause and fix (2026-11-01)

Symptom: Ruined Village and Volcano converted with the full object set stayed at `gameplay=0`, no `autostart`, even with skinned
conversion off; `DRAKAN_DBG_NOOBJ=1` fixed it. Reproduced on the post-class-count-fix build (`build/final-game`).

## Bisect (RV, `build/ootf-run/bis.sh <name> ENV=..`, recipe_long.txt)

| Variant | Result |
| --- | --- |
| full objects | no control (also a heavy slowdown: 800 -> 56 flips / 2 s while the pause menu toggles) |
| `DRAKAN_DBG_DROPNAME=Stomp` | control |
| `--only` without `sequences` (Stomp objects kept, no SSD records) | control |
| `DRAKAN_DBG_STOMP=0` (Initial State of every Stomp Player = Triggered) | control |

`inputGameplay` needs camera mode, `scripted == 0` (0x4fbafc) and no GUI screen. RV has a Stomp Player (obj 2217) with Initial State
1 (On = starts at level load), chain 7 -> 8 -> 6. The scene played but never released the camera / player.

## Root cause

`ootfseq.ts` zeroed every type 5 (run/stop AI) action ("every PS2 one is 0"). That is wrong: on the disc the camera and Rynn actors of
every scene have `5 = 0` at time 0 and `5 = 1` at the end (LEVEL1ON.SSD: Denman, Queen, 1.01 .. 1.34, Skeleton Boss Stomp ...), and the
closing 1 is what hands control back. With all values 0 the scene never released the player (scripted flag stays set).

## Fix

`SequenceConvertOptions.keepAiResume` now defaults to on (PC values kept, `false` restores the old behaviour). Verified in game:
RV `autostart: player under control at frame 10049` (about 2000 frames of intro scene first), Volcano frame 8979. Unit test updated.

## Test aids added (`ootfpipeline.ts`, env at conversion time)

`DRAKAN_DBG_OBJSPLIT=n,k` (keep index % n == k plus essentials), `DRAKAN_DBG_DROPRFL=a,b`, `DRAKAN_DBG_DROPNAME=<regex>`,
`DRAKAN_DBG_STOMP=<n>` (list Stomp Players, `n >= 0` sets Initial State). Harness: `build/ootf-run/bis.sh` (private).
The tsc error in `test/terrainblob.test.ts` was already gone (another session fixed it).
