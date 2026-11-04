# Multiplayer character picker and original model previews

## Findings

* The main menu used `modelNames()` (`{"Rynn"}`), not the runtime character roster. Its right-hand preview always drew
  `drawFigure`, a procedural hooded fighter unrelated to the selected game character.
* Only co-op lobbies offered a real character selector. `allowedCharacters` split entries by `hostOnly`, and the host
  selector was disabled. Rynn belonged exclusively to the host. This was the prior design, now superseded.
* The runtime roster only admitted humanoid NPCs with idle, walk, run, attack **and** death clips, deduplicated by model.
  This excluded the Mage, Empress, female villagers and all creatures. The clip mapper already has idle fallbacks.
* Actual character studio turntables existed, but only after a manual capture run. The main setup menu never used them.

## Changes

* One character catalogue / class-id selection for setup, versus lobbies and co-op lobbies, for hosts and joiners alike.
  Remove fictional placeholder character ids. Save the class id in `multiplayer.ini`; preserve the environment override.
* Expand the PAL metadata catalogue to 165 entries (Rynn, 163 animated NPC / creature classes, Arokh). Keep named aliases.
  Map flying behaviour names and use only each skeleton's own clips. Skip humanoid weapon attachments on other skeletons.
  Arokh uses SYSTEM records / clips, with the bank prefix adjusted for the current level.
* Generate actual model turntables from MOD / TXD with the standard library, through the existing build/deployment path.
  Bind-pose geometry, original textures, depth / alpha-tested two-sided rasterization, 12 views, 160 x 240 cells. Cache
  outputs by script / disc signature. Respect texture dependency banks. No original assets or renders are tracked.
* Both panels show the original model, never the procedural stand-in. Missing previews are explicitly labelled. Preserve
  image alpha instead of deriving it from brightness. Keep the optional game studio cache as a compatibility fallback.
* Reserve the character panel in every lobby and compress large non-team rosters above it rather than overlapping it.

## Verification

* Synthetic Python tests cover fractional model coordinates, depth occlusion, transparent texture cutouts, LOD separator
  polygons and non-combat / flying clip fallbacks.
* `character_roster` covers every entry's unique id, selection, same-bank clip mapping / fallback and finite animation
  times, plus Mage / Empress / Orc / Spider / Arokh availability and skeleton-safe weapon metadata.
* `gameui_multiplayer_test` covers main-menu cycling / wrapping / host action, unrestricted host and joiner selection,
  versus selection, loading lockout, preview image requests, labelled missing art and obsolete saved-id repair.
* Both C++ tests and all four synthetic Python preview tests pass. Ruff / clang-format checks pass on changed code.
  The full Python suite (`python scripts/run_tests.py`) also passes: 229 tests.
  Repository source audit: no prohibited tracked files. The incremental PAL build deployed successfully through
  `Build-LocalGame.ps1`; all 165 deployed PNG strips match the catalogue and expected 1920 x 240 dimensions.
* **[game]** A direct run from `dist/DrakanPC` selected Rynn -> Spider -> Rynn -> Arokh in the main multiplayer menu.
  All three original turntables loaded; screenshots under `build/harness/character-menu-originals/` show the actual
  textured meshes, not the former hooded figure. The game exited cleanly through the input recipe.
* **[game]** `coop_characters.py` with host Mage (`fbc`) and joiner Purple Orc (`c19`), using the deployed executable
  and an existing Surdana fixture: both exited cleanly, both spawned the other player's chosen body and mapped its own
  clips (`514c7` / `514c2` for Mage, `50ddc` / `50de7` for Orc). Three harness checks passed. Evidence:
  `build/harness/character-roster-mage-orc/`. The harness's default `gw-net` fixture no longer exists on this checkout;
  the successful run explicitly used `build/harness/char_prev1/card/mc0` instead.

See `docs/reference/netcode.md` for deployment and gameplay limitations. In particular, the controlled local actor remains
Rynn. Expanded creature factories are not all live-match verified; exposing a class does not implement its abilities.

## Follow-up: local appearance and 15-character initial roster

The user requested fixing the local-body limitation and restricting selection to 15 characters. This follow-up
supersedes the initial 165-character selection and the local-Rynn-only limitation above.

* `CHARACTER_IDS` / generated `kSelectableCharacters` now define exactly 15 human-sized characters: Rynn, Northmen
  Archer, Male Villager, Mage, Blacksmith, Denman, Zola Dane (Passive), Empress, Ranwulf, Guard 1, Captain, Sevoth,
  Nichols the Bold, Atlus, Capn Serris. Both `roster()` and `known()` enforce this list; old out-of-roster selections
  fall back to Rynn. Preview generation now targets this same initial roster.
* The selected character is also shown locally during a match. The existing player controller / inventory / HP / camera
  are retained, with a separate inert body driven by the live player pose and the selected skeleton's own clips.
  Rendering suppresses only the original body / replaced held-item draws, without mutating player visibility flags.
  Clearing the render view restores the original body; lifecycle cleanup handles save, restore, leave and level loads.
* **Rejected probe:** swapping a0 at the final `189b38` draw produced stretched vertices and the wrong textures because
  native preparation had already used Rynn's model. **Rejected probe:** unfiling the local body permanently removed it
  from visibility as well as collision; screenshots then showed only the other player's body. Neither probe was treated
  as verified merely because the processes exited cleanly.
* **Working path:** temporarily file the local body in its controller's cell during `139010` RenderWorld, allowing its
  own native material / skeleton / culling path; unfile on return and before simulation. The draw hook waits for the
  selected body's first real native draw before hiding Rynn. Scene capture filters the old model and retains the new one.
* **[game]** `build/harness/local-character-render-cells/`: host Mage, joiner Ranwulf. Both local bodies reached the native
  draw pipeline, both partners spawned, both games exited cleanly. Screenshots were inspected: A's `local_after_save`
  shows the local Mage correctly textured (no Rynn); B's shows the local Ranwulf. Movement and inventory were exercised;
  quick-save removed and recreated both local bodies while preserving controller `a5b6b0`. The earlier screenshots with
  overlapping characters are the two players standing close together, not an extra Rynn body.
* **[test]** Exact roster size / unique ids / excluded creature ids, native draw suppression, reflection filtering and
  fallback while a body is unavailable are covered by `character_roster` and the Python preview tests. Targeted C++ tests
  pass; the full Python suite passes 230 tests. The preceding full runtime run passed all 105 tests (6 data-dependent skips).
* The incremental build/deployment succeeded after the user closed a running game which had locked `avcodec-61.dll`.
  No running user game was terminated by the agent. The play folder remains `dist/DrakanPC`.

### User clarification: equippable combat NPCs, not dragons as main players

The main-player selector is explicitly for NPCs that equip items and fight. Replace the non-combat Mage and Empress
entries with Monk (`16bf`) and Monk Boss (`16c0`), retaining 15 choices. The generator asserts that all 14 non-Rynn
entries are NPC humanoids with idle / walk / run / attack / death clips; runtime `roster()` also requires both combat
and humanoid metadata. Tests explicitly exclude Mage, Empress, Arokh and creatures from the main-player list.
Dragon appearance / control remains separate: Dragon Skin, Dragon's Duel or an explicitly dragon-required mode / script.

**[game] Final combat roster check:** `build/harness/local-character-combat-15/`, Monk host and Ranwulf joiner, both using
the deployed build. Local original models are hidden only after the selected body's native draw. Both inventory previews
show the correct NPC. The Short Wood Bow was equipped, cosmetic bows spawned on NPC left-hand joint 13, attacks fired
arrows (ammo 180 -> 179), and both selected bodies / bows were recreated after quick-save. Screenshots of inventory,
attack and after-save states were inspected. Both games exited cleanly and all three harness checks passed. The native
arrow holder's record (`3379d`) cannot be duplicated by the generic item factory; its original held model is deliberately
not hidden when no cosmetic duplicate exists. This does not prevent firing the real arrow / consuming ammunition.

## Follow-up: live menu geometry and avatar transition defects

The user reported the enlarged frame-strip preview and heavy swap defects (Rynn first, NPC later, invisible Rynn still
present). Changes under verification:

* Replace the active menu strip path with private DCPM original-mesh / UV / material bundles. The overlay draws GPU
  triangles at output resolution with continuous model rotation; no low-resolution GIF-like frames or PNG fallback.
  The mesh parser bounds counts / texture sizes, rejects truncation / non-finite values, and releases textures with the
  overlay device. Tests cover decoding, truncation, output-size projection and rotation at sub-frame-strip intervals.
* Remove the fake seven-colour dragon selector. Show disabled `Dragon Appearance: Arokh (original)` instead.
* Hold gameplay input and cover the world while the selected local avatar prepares, independently of other input holds.
  Remove the first-draw Rynn fallback. Synchronize the body transform at RenderWorld entry, not just before movement.
  Prepare the original gameplay skeleton before scoping off its model visibility for the whole world render; restore
  it on exit / lifecycle cleanup. The NPC remains the only visible avatar, without deleting the controller needed by
  native inventory, camera and combat.
