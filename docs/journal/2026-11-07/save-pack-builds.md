# Issue 5: saves remember the pack builds they were made with

Follows [quick-load LRU](ootf-quickload-lru.md), "Still open": the Volcanic sanctum quicksave (`card-sanctum`, written by
an older exe and converter) stalls the restore on the current pack (`[memset] bad size` from `0x405714`, then the
allocator at `0x4acdd0` spins). Converted class ids and logic defaults changed in formats `34ddc63` and terrain blobs
in `5dfe29a`; a player who updates a pack hits the same with their own saves. Reference:
[modding.md "Saves and pack builds"](../../reference/modding.md#saves-and-pack-builds). Runs and packs are private under
`build/harness/issue5-save/` (`run.py` from `ootf-lru` pointed at `build/harness/issue5-save-dist`, `runall.sh`, `recipes/`).

## What a save knew before

Nothing about packs. A save is the native `.rsg` (an SRSC of zlib entries) in `<card>/<saveId>Slot<n>/<level>.rsg`;
the only companion is the HD preview `<save>.rsg.<fnv64>.preview.png`. The restore reads class and object records
straight from it (`43b4d0` -> `23f170` -> the card manager), so a different pack build is only noticed when a size no
longer fits, after the level is already torn down. Campaign packs get their own card folder (`mc0_ootf`), but direct
starts and level copies use the base card, and a pack update keeps the folder anyway.

## What changed

* **Pack build id.** Manifest key `build` (written by the editor's `zipModFiles`: the content hash of `files/`, `new/`,
  `overrides/`); without it the runtime computes the same hash (`hle::mods::packContentHash`), so editor and runtime
  agree (shared vector `8d3872dfe99031fa`). Formats `packbuild.ts`, editor-core `stampPackBuild`.
* **Record.** The memory card's write observer (now one slot per user: previews, records) writes `<save>.rsg.mods`
  with the active content packs (`id`, `name`, `version`, `build`) after every save in a slot folder, and removes it
  when the save is written without content packs.
* **Check** (`hle::mods::checkSave`): a recorded pack missing or with another build refuses the save; an unreadable
  record refuses it; a save without a record is refused only when its level is registered by an active pack that
  declares a build (it was written before records existed, with an unknown pack build). Stock saves and saves without
  mods are never refused.
* **Refuse, not repair**: nothing in the restore can be made safe from the outside (the bad sizes come from the save's
  records), and a guessed migration could carry a corrupt state into the next save. The save is left untouched.
* **Where**: modern Load screen (slot unavailable, the reason in the detail panel), Continue (stops on the Load screen),
  quick load (not started; an 8 s notice band at the top, `HudSlot::Notice`), and as the catch-all the native save check
  `0x23f170` answers "not a valid save" (native "could not be loaded" message, title). Texts in five languages.

## In game (`build/harness/issue5-save-dist/drakan.exe`, direct starts, god mode, `runall.sh`)

Packs: `packnew` = the current converted pack (`ootf-lru/packfix`, formats `5dfe29a`) re-zipped by the new
`zipModFiles`, manifest `build = "a46d89ce1cbcd218"`; `packfix` = the same content without the key; `ootf-volroute/pack`
= an older converter output.

| Run | Card | Pack | Result |
| --- | --- | --- | --- |
| `old-new` | `ootf-lru/card-sanctum` (no record) | packnew | F8 refused before the native load: `[saves] OotF 11 - Volcanic.rsg: made before pack builds were recorded`, the notice band names the pack; no `memset`, no watchdog, W walks, clean exit |
| `mk-new` | empty | packnew | F5: `[saves] OotF 11 - Volcanic.rsg: pack record ootf-levels a46d89ce1cbcd218`, `.rsg.mods` beside the save |
| `ld-new` | `mk-new` | packnew | F8 loads (`beforeLevelLoad ... fromSave=1`), clean exit |
| `ld-fix` | `mk-new` | packfix | the runtime's content hash is `a46d89ce1cbcd218` too: loads, clean exit |
| `ld-old` | `mk-new` | ootf-volroute | `build 6d79f255dee4cac3 (content hash)`: `different build`, F8 refused with the notice, clean exit |
| `st-make` | empty | none | Surdana F5 / F8 without mods: saves, no record written, loads |
| `st-load` | `st-make` | packnew | the stock Surdana save loads with the OotF pack active |
| `ls-old` | the sanctum save in slot 1 | packnew | `DRAKAN_SAVE_MENU=modern`, Pause > Load Game: slot 1 "OotF 11 - Volcanic" dimmed and `Unavailable`, the detail panel gives the reason, Enter does nothing, Back returns, clean exit |

## Still open

* A same-build save can still stall if the engine itself is at fault (the LRU flaw of the previous note); the record
  only guards against pack changes.
* ~~The native slot screens still list a refused save as loadable~~: done, see
  [native-slot-refused-saves.md](native-slot-refused-saves.md).
