# Old saves bring back old object state: save revisions (2026-11-16)

Follows [final-check-2.md](final-check-2.md): on a new game everything passed, but the owner's 15 November saves
(`mc0_ootf/Slot13`, `Slot33`, card root; record build `da1e598e3d3adfdd`) loaded old object state. The pack build stayed
`da1e...` because the save layout only grew ([save-compat.md](save-compat.md)), and the owner wants those saves to keep
working, so the build was **not** changed. Private work, never tracked: `build/review/savefix-1116/` (play folder `play/`
with its own `elf/` copy and a junction to final-1116b's textures, the owner's files in `before/`, private exe build `gb/`
linking a copy of `build/final-game`'s `ps2_generated.lib`, packs in `packs/`, runs in `runs/`, pictures and dumps in
`ev/`, tools in `tools/`). Nothing from either game is quoted here beyond ids, classes, offsets and field names.

## How a save holds objects

From the 2026-11-01 / 11-05 loader notes and the owner's files (`tools/uid2.py`, `recs.py`, `rechex.py`): slots
`0..K-1` are the saved records (0x102, entry id = slot + 1, first word `(0x22 database index << 16) | class`), the
level's untouched objects follow at `K + position in 0x24`; 0x21 holds the total, then `(uid, slot)` pairs in uid order,
then four counted slot arrays. **Destroyed, taken and killed objects are simply absent**: Slot33 leaves 332 level uids
unnamed, among them the felled tree 1106. So "create every unnamed object on load" would bring those back. References
between saved objects are slot numbers, so a record can never be removed or inserted.

## The mechanism: save revisions

* The pack gets `save_revision = N` and per-revision lists in `mod.toml` (outside the layout and the build):
  `save_added_objects`, `save_replaced_effects`, `save_reset_togglers` (and `save_relinked_powerups`, which the game
  reads but the converter does not write yet, section 4) ([modding.md](../../reference/modding.md#saves-and-pack-builds) "Save revisions").
* The save record (`.rsg.mods`) gets `revision = N` in the pack section (absent = 0, older games ignore it, the build
  check does not read it). `.rlt` level states now get a record too; an older `.rlt` without one takes the lowest
  revision of the `.rsg` records beside it.
* `drakan_save_guard.cpp` installs a memory card read filter (`ps2_stubs::setMcReadFilter`, `MemoryCard.cpp`: a
  read-only open of an existing file can be served from memory through a temporary file). A `.rsg` / `.rlt` of a pack
  level whose revision is older is read upgraded by `hle::mods::upgradeSave` (`runtime/hle` `SaveUpgrade.h`); the file on
  the card is never written, and the game's next save writes the upgraded state with the current revision. A save it
  cannot parse is read as it is and logged.
* The converter (`editor/scripts/ootf-convert.ts`, formats `saverevisions.ts`) computes the lists. The installed pack
  already contains 3353 but has no revision lines, so the added objects of this first revision come from
  `--save-baseline` = `build/review/pack-1115/ootf-levels.before-1116.zip` (build `da1e...`, 1,882 Ruined Village
  objects, no 3353, the pack of the 22:32 conversion the saves were made with). Without a baseline the converter now
  warns. Later conversions inherit the lines from `--previous` and open revision N + 1 only for something new.

## 1. Missing statue

Cause: uid 3353 (class 3:18644, the blow-up prop, `ootfblowup.ts`) is the only object the current pack adds to Ruined
Village over the save's pack; no save names it, so the load never creates it. Prototype first (`tools/addstatic.py`,
`proto/s33statue`, current exe): uid appended at the end of 0x24 (slot = old total), 0x21 total + 1 and pair in uid
order; the statue stood on the pedestal at 4.4 s of the awakening and broke apart (`ev/proto_statue.jpg`). Fix:
`save_added_objects = ["1 | OotF 01 - Ruined Village | 3353@2855"]`. Guards: an id the save already names (any pair) is
skipped, so saves made with the current pack (which name 3353) are untouched; and the prop is only added while the save
still names its generator 2855 (Slot13 / Slot33 hold it as a saved record; the owner's `Slot14` Ruined Village `.rlt`,
written after the awakening under the old pack, no longer has it, so a statue does not come back to an empty lair).

## 2. Stray flames

Decoded: Slot13 holds two records of class word `0x13a2b` at (321687, 24172, 197611), Slot33 two at
(321687, 23703, 197611), both without a uid pair; Mover 3374 stands at (321687, 24172, 197611) (Slot13: untouched,
Slot33: saved record at y 23703, felled). Class 14891 is the disc RESOURES "Flaming BoltFire Effect" (engine 67);
no level object of the converted Ruined Village has that class, and the campfire 702 is class 11:2415, so the link to
the tree Mover holds and dropping by class alone would hit Drakan 2. The flames are not registry handles (RAM dump
`runs/fx5_s13`: no handle uses resource 0x33a2b, `ev`/`tools/flamescan.py`), and moving the records' position did not
move them (`ev/mv.jpg`: they follow their owner), so a post-load kill was dropped. Swapping the two records' class to an
empty effect class (ZapLoop, `proto/sw13`, `sw33`) removed both flames (`ev/sw.jpg`). Fix: the converter adds an inert
twin of the Mover's effect, RESOURES 18645 "Flaming BoltFire Effect (off)" (no texture, particles, length or sound),
and `save_replaced_effects` swaps uid-less records of 0x3a2b at the Movers' x / z (Ruined Village 3374, Wartok Canyons'
tree and follower). The records keep their slots.

## 3. Empty camp (real, fixed)

Cheap check first: walking Slot33 through detector 2716 shows the props again (`runs/camp2716`, `ev/camp2716.jpg`); the
same views without the walk show the fire only (`runs/camp_ctl`, `ev/camp_ctl.jpg`). The props (700, 698, 2437, ...)
are untouched statics in Slot33, but the zone toggler 2684 is a saved record whose word after its link list is
`(state << 16) | 121`: Slot13 `0x00000079`, Slot33 `0x00020079`; the restored toggler hides its links again. Patching it to
`0x79` showed the camp (`proto/tog33`, `ev/tog33.jpg`). Fix: `save_reset_togglers` for the zone-culling togglers
(`ootfzonecull.ts`: Ruined Village 2337, 2684) clears that state. The grunts in Slot33 are saved creatures of their own
(they lie dead by the camp after the walk); nothing restores them.

## 4. No breath after the awakening (partly fixed)

Compared on arrival in Wartok Canyons (`tools/breathcmp.py`): the new game's Arokh record holds the breath class
references `0x1340a` / `0x129e4` (FireBall 3 / FlameBreath, the stand-ins of `ootfbreath.ts`) and links the power-up it
carries over (`+0x64` = 0, list (1: slot of a uid-less power-up record)); the Slot33 route had neither. Before the
awakening, Ruined Village's dragon 1011 in a new game links power-up 3157 (`+0x64` 0, list (1: 3157's slot)); in Slot13
and Slot33 the list is empty and `+0x64` = 1: under the old pack the dragon had already taken the power-up, whose breath
was then empty. The level files link 1011 and 3157 the same in both packs (`rv_base.json`, `rv.json`). Re-arming the
power-up alone (`rearm1`) or with the dragon's flags (`rearm2`) did nothing; restoring the link (`relink.py`,
`rl_link`) made the awakening give the dragon the breath references (record as a new game's) and Wartok Canyons shows
the amber breath orb instead of the grey one (`ev/rl_all.jpg`, `acc2_breath.jpg`).

**Still failing, not shipped:** with the relink (runtime rule `save_relinked_powerups`, run `acc2_aw33`) Wartok Canyons
has the amber orb but the attack sets no breath bit (`[dragon]` trace: `f90` never `40002`; the new game 393 ticks) and
no breath projectile is born (breath_born 0; new game `runs/acc_awnew` 9). The new game's Arokh arrives linking a carried
power-up record; `acc2_aw33`'s arrives without one, but `acc_aw33` (an earlier build that relinked twice) arrived with one
and did not breathe either, so that is not the whole cause. The power-up's record keeps a word the new game clears when
the dragon takes it (`+0x84` 0x56 vs 0). Also the relink cannot tell a breathless dragon from one that took his
power-up normally: the new game's Ruined Village `.rlt` after the awakening has the same `+0x64` = 1 and empty list. So
the converter collects the dragons (`res.saveRelinks`) but does not write the lines; the game keeps the rule for a later
fix. The owner's Wartok Canyons save `Slot14` has a breathless Arokh (no breath
references, `+0x64` = 1, empty list) and no power-up in that level to relink: no rule repairs it.

## 5. Felled tree on a new game

Not run: a direct start has no weapon and the upgraded Slot13 had no weapon drawn either (`runs/fell13`: no swing).
The felled state was seen only from the upgraded Slot33 (no flame over the trunk, `ev/acc_s33.jpg`).

## 6. Mouse pitch on the ground

RAM dumps in flight and on the ground (Rift World, `runs/rwflags`, `tools/flagdiff.py`; and Wartok Canyons): Arokh's
instance data `+0x9c` is 1 on the ground (also in the Wartok cave) and 0 in the air; `+0x94` bit 3 is only set during
the landing. `DrakanInput.cpp` now drops the mouse pitch while `+0x9c` is 1 (layout field `dragon_grounded`, both disc
profiles). Run `pitch_rw2`: 400 counts up and down on the ground and while walking: `pitchIn` 0, pitch stays 0
(`gnd=1` in the `[dragon]` line); after Space the counts pitch him again (0.2 per tick up to the 0.75 limit). Take-off
in open air in Wartok Canyons: not shown (the arrival cave does not lift him, `place` did not move the mounted dragon).

## Acceptance (private exe `bin/drakan.exe` md5 `20c7bab7...` = the committed code, pack `packs/conv5` md5 `17e01067...`)

| Check | Result | Evidence |
| --- | --- | --- |
| Pack: `--previous` installed pack, `--save-baseline`, release flags (`--subtitles`, `--previews`) | `save build da1e598e3d3adfdd kept`, revision 1 (`3353@2855`, three effect swaps, togglers 2337 2684); vs the installed pack only `mod.toml`, class 18645 and the RESOURES field lists differ | `packs/conv5/convert.log`, `ev/pack_diff_conv5.txt` |
| Slot13: no flame at the standing tree, camp with grunts, F5, F8 | Pass | `runs/fin_s13`, `ev/fin_s13.jpg` |
| Slot33: no flame over the felled tree, camp props shown (togglers reset), F5, F8 | Pass | `runs/fin_s33`, `ev/fin_s33.jpg` |
| F5 saves re-parsed: record `revision = 1`, 3353 named, no 0x13a2b record (the two flames kept as 0x148d5) | Pass | `ev/acc_f5_parse.txt` (fix8 / conv4 runs) |
| Slot33: statue on the pedestal in the awakening, then the break | Pass | `runs/fin_aw33`, `ev/fin_aw33_statue.jpg` |
| Slot33: breath after the awakening | **Fail** (with the relink: amber orb, no breath; without it, as before) | `runs/acc2_aw33` |
| Slot14 (owner's Wartok Canyons save): load, its two tree-Mover flames made inert, F5, F8 | Pass | `runs/fin_s14` |
| New game: awakening, Wartok Canyons breath | Pass (9 breath projectiles) | `runs/acc_awnew` |
| Drakan 2 Catacombs `mc0/Slot1`: load, F5 (record names only the motion pack), F8 | Pass | `runs/fin_d2cat` |
| Mouse pitch on the ground | Pass | `runs/pitch_rw2` |
| Tests | ctest 205 / 205 (7 skipped, data-dependent), formats vitest 768 / 768 (77 files), editor-core 373 / 373, `run_tests.py` 562 OK (2 skipped), `save_upgrade_test` 93 checks | `logs/` |

## Not verified / limits

* Breath from the upgraded Slot33 (above); the owner's `Slot14` stays breathless.
* The cemetery toggler 2337 was reset in Slot33 but not looked at in game.
* An old `.rlt` beside a `.rsg` the new game has written (Slot14 after F5: the old Ruined Village `.rlt` stays, the
  `.rsg` record now says revision 1) is judged new; its rules (the statue, which `@2855` holds back there anyway, the
  Ruined Village flames and togglers) are then not applied if the player returns to that level.
* The felled tree on a new game; take-off in open air in Wartok Canyons.
* A failed upgrade ("read as it is") is still stamped with the current revision at the next save.
* `.rlt` files written by the earlier exe take their revision from the `.rsg` records beside them.
* The record offsets the dragon and toggler rules use (`kDragonPowerUpMark` 0x64, the `(state << 16) | 121` word) were
  read from these saves only; a record that does not match is left alone.
