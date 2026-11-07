# Issue 5: the mount after the awakening scene, and the chain from Wartok Canyons

Follows [the Soul Crystal note](ootf-ruined-village-crystal.md). Runs are private under `build/ootf9/` (own copy of the
16:27 executable and of the `build/ootf8/pack` pack, direct start of "OotF 01 - Ruined Village", god mode, one game at a time
through the harness slot lock). The recipe starts Rynn on the lair ledge (482600 25200 201196), holds W for about 17 s until
the awakening scene plays, then waits on the engine's own screens instead of tapping Cross on a timer. Nothing from the
game is quoted.

## (a) Rynn is mounted on Arokh after the scene: yes

Run `a4`: scene, message box (screen 2), save screen (screen 5), one Cross tap to confirm, then `sync play`.

| Sample | Controlled actor | Rynn | Arokh |
| --- | --- | --- | --- |
| 2 s after the load | class `0x1001c` (Arokh) | 76833 11091 14003 | 76810 10835 13961 |
| 6 s after the load | Arokh | 76759 11118 13974 | 76810 10835 13961 |
| after W for 4 s | Arokh | 76333 11086 18231 | 76279 10835 18144 |

The controlled actor is Arokh, Rynn sits about 260 units above him, and W moves both (4,200 units in 4 s). The lifecycle
log shows `mountChanged 1->2` at the scene and no change back. The player is mounted on arrival, as at a direct Wartok start
(Rynn 87671 14483 39256 on Arokh 87660 14215 39210, controlled actor Arokh).

A trap that cost three runs: **a Cross tap after the Wartok load dismounts Rynn** (runs `a1` to `a3`, which kept the old
every-4-seconds Cross taps running through the load). Rynn then stands on Arokh's back, the controlled actor is Rynn
(`0x10009`), `mountChanged 2->1` shows 3.5 s after the load, she cannot walk off (W moved her 15 units in 4 s), and the camera
sits point blank on her in the dark. That is the "dark close-ups" of the previous note; it is a test artefact, not a converter
or scene fault. Recipes must stop tapping Cross once the save screen is confirmed.

Caveat: `a4` ran while another run of mine (`chain9`) was still holding the game slot, so two games shared the GPU for its
duration; the positions above do not depend on timing.
