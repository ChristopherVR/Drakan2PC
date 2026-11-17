# New Blender meshes for Delon, the Boss Blade Dragon and the Ebon Dragon (2026-11-17)

Owner request: the OotF models that have no Drakan 2 swap are to be built in Blender (the Boss Blade Dragon and the Ebon
Dragon as new meshes, not recolours of disc models), and Delon finished. Scratch, previews and runs are under
`build/authored/` and `build/ootf-run/{delon-rv4,boss-wc,ebon-isn}` (not tracked). Only names and counts are quoted here.

## How a Blender mesh reaches the game

New formats stage `src/ootfauthored.ts`, hooked in `OotfAssetBuilder.addModel` before the skin upgrade (reference:
`docs/reference/modding.md` "Authored models for converted OotF characters"). The committed file holds geometry only;
textures, UVs and weights are taken at convert time from the player's own PC model, so nothing from the game is in the
repository and the 1999 identity (Delon's face, the boss's green skin) is kept.

* The mesh replaces the PC LOD 0 on the PC skeleton: the PC clips, hit spheres (their centres come from the original
  vertices), scene channels and the synthetic jaw all still work.
* Placement by `fit` (`height`, `extent`, or `joints`: a piecewise per-axis warp onto the PC joints).
* UVs: each triangle projects onto the nearest PC triangle; head triangles along rays from the head joint.
* `keepPcJoints`: Delon keeps the 1999 head, smoothed once (the Drakan 2 method is a simple head with a painted face, and
  his painted face is the 1999 texture).
* Files: `scripts/buildtool/kit/models/*.json`, exported by `analysis/scripts/blender/authored_export.py`; the build passes
  the folder (`extras.py`, `--authored`) and converts again when a file changes. Preview without the game:
  `analysis/scripts/blender/preview_authored.sh`.

Bugs found on the way, all fixed and covered by `test/ootfauthored.test.ts`:
* **Winding.** PC meshes are clockwise from outside (they measure a negative signed volume); the (x, y, z) to (x, z, y)
  swap already mirrors, so the Blender order is kept as it is. Reversed, Delon's new body drew nearly black in game while
  his kept 1999 head was lit.
* **Scaled joints.** The Boss Blade Dragon's inverse binds carry a uniform scale of about 0.79; `-(t R^T)` put its joints
  at 62 % of their place. The origin is now `-t R^-1`. Found by the subagent that built the dragon.
* **Landmark warp.** A landmark far from its PC joint (Delon's clavicle 7.5 cm out against a collar joint 1.5 cm out; his
  neck 4 cm lower than the PC neck joint) made a piece of the warp steep: the waist widened, the arms doubled in
  thickness. Those landmarks are left out.
* **Texture bleed.** UVs extrapolated up to 50 % past a PC triangle reached the gore patches beside the clothes on the
  same sheet (red specks on Delon's tunic). Now 10 %.

## The models

| Model | PC (LOD 0) | New | Script |
| --- | --- | --- | --- |
| Delon | 282 vertices, 526 polygons | 1,367 vertices, 2,638 triangles (new body below the neck, 1999 head smoothed) | `delon_model.py --authored` |
| Boss Blade Dragon | the Blade Dragon mesh, 105 / 181, green skin | 1,592 vertices, 2,656 triangles | `boss_blade_dragon_model.py` |
| Ebon Dragon | 216 / 384 | 1,527 vertices, 2,712 triangles | `ebon_dragon_model.py` |

* Delon's body was reworked against the 1999 model: belt, buckle and boot tops moved to the 1999 heights, the tunic hem
  ends where the 1999 one does and no longer flares, torso, legs and arms slimmed by about 12 %.
* The dragons were built by two subagents in parallel, each to the same brief: original geometry from hand-measured
  proportions (no vertex lists, no textures), about 2,600 triangles like the Drakan 2 dragons, a head with horns, jaw and
  teeth, clawed legs, wing bones and membranes, spines; every slot names its PC textures (the wing textures are cut-outs).

## In game (`ootf_level_run.py --campaign --convert-arg=--authored ...`, harness window settings)

| Run | What the frames show |
| --- | --- |
| `delon-rv4` (Intro + Ruined Village, `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1`, the opening plays) | Delon walks and gestures beside Rynn in scene 1.1 with his 1999 face, the new body lit like Rynn (after the winding fix): V-neck tunic, belt and buckle, cuffs, leggings, boots |
| `boss-wc` (Wartok Canyons, boss pulled with `DRAKAN_DBG_PULL`) | the new Boss Blade Dragon flies at Rynn, wings beating with their cut-out feathers, clawed feet, green skin |
| `ebon-isn` (Islands Night, Ebon Dragons pulled) | the dragons attack (their sonic rings hit Rynn); the level opens in a dark chamber and no frame shows a dragon clearly |

## Open

* Ebon Dragon: no clear in-game frame yet (the bind-pose previews are clear).
* Delon: pale patches at the top of the legs (the 1999 hem strip is on the trouser texture) and dark patches where the new
  neck meets the kept head. The body textures are the 1999 ones projected; painted Drakan 2 style atlases would need a
  bake whose result is original art (not done).
* Weights are transferred from the nearest 1999 surface; a dragon's membranes and ears were only seen in flight, not
  checked frame by frame.
* Not checked with the owner's own `drakan.ini`.
* The editor's in-browser converter does not load authored models yet (the CLI and the build do).

## Second round: the rest of the cast (six builders in parallel)

Owner: continue with the characters that have only PC models. Six subagents, each with one body type and the lessons
above; previews only, the lead ran the game. Results judged on `preview_authored.sh` comparisons (1999 top, new bottom).

| Model(s) | Script | Verdict | Where |
| --- | --- | --- | --- |
| Goblin (`goblin(h)`, 120 placed) | `goblin_model.py` | clear gain: hunched muscular body, teeth, horn crest, clawed feet, belted loincloth | `kit/models` |
| Giant Tick | `tick_model.py` | clear gain: segmented spined body, the plate pattern on the plates | `kit/models` |
| Drone | `drone_model.py` | modest gain (body); the wings keep the 1999 card shapes | `kit/models` |
| Julian, the farmer, Rimril and the wounded wizard, the wounded warrior, the rack victim, the two male villagers, the three mine slaves | `human_kit.py`, `humans_model.py` | modest gains: rounder forms, modelled belts, cuffs, boot and greave tops on matching paint; Julian about even (a few texture teeth on the robe) | `kit/models` |
| the two female villagers | `female_model.py` | modest gain (puffed sleeves, cuffs, figure); the bell skirt can open at the centre front and back when the legs part | `kit/models` |
| Crimson and Ebon Knights | `knight_model.py` | better armour silhouette, noisier texture (smears on the greaves and skirt, stray band colour on the pauldrons) | `kit/models-draft` |
| Succubus, Queen Shillah | `succubus_model.py` | better legs, feet and wing panels, blotchier netting and chest | `kit/models-draft` |
| Death Mage (and shaman, ghost) | `deathmage_model.py` | worse: the robe loses the 1999 separate ribbons | `kit/models-draft` |
| Dr. Evil | `drevil_model.py` | mixed: horns and jaw, but the eyes are lost and the head texture smears | `kit/models-draft` |
| Tuiri | `female_model.py` | about even (leg pattern noisier) | `kit/models-draft` |

`kit/models-draft` is not loaded by the converter; a draft moves to `kit/models` when it beats the 1999 model. The scripts
write to `kit/models` by default, so a rerun of a draft's script puts it back in the shipped set.

In game (full 23-level conversion: 18 authored models used, none skipped or dropped):
* `gob-alw` (Alwarren, goblins pulled): goblins burn and fall in their death clips with the new body and loincloth
  bending cleanly, no torn polygons.
* `hum-rv` (Intro + Ruined Village opening, scenes played): the new villagers at the cart, a kneeling villager, the female
  villager in her dress and Julian pinned by the spear all animate with their 1999 faces.

### What limits the rest

The silhouette is always better; the surface is limited by the method. Each new triangle takes the 1999 texture of the
nearest 1999 triangle, so a denser, differently shaped surface shows the low-resolution 1999 paint stretched, split at its
seams (teeth along colour borders) or smeared where several 1999 parts meet (knights' greaves, Death Mage robe, Succubus
netting). The builders also noted: radial head projection paints the face on a hood's inner wall (Death Mage), a slot
cannot be limited to the 1999 triangles of given joints (Succubus wing bones took skin from the feet), and transferred
weights cannot blend a skirt across both thighs. Lifting the drafts needs the models' own UVs and textures (painted or
baked in Blender: original art, committed like the kit pictures) rather than the 1999 paint.

## Third round: own textures (original art baked in Blender)

Owner: continue with own textures. New support: a slot can be `texture: "image"` with the model's own UVs and a PNG
beside the file (formats `ootfauthored.ts`, `png.ts`, `OotfAssetBuilder.addImageTexture`; 8 bpp square, 256 px in game);
`analysis/scripts/blender/own_texture.py` unwraps and bakes (base colour x occlusion x a soft top light); the PNGs are
pinned by `scripts/buildtool/pin_model_textures.py` (`kit/models.sha256`, `kit/models-draft.sha256`). Four builders
retextured the drafts, painting procedurally (no 1999 pixels sampled); kept 1999 heads stay projected.

| Model | Verdict | Where |
| --- | --- | --- |
| Crimson and Ebon Knights | clear gain, a redesign: clean plate with brass or silver trims, enamel helm stripes, glowing eye slits, mail, emblem tabards; in game (Alwarren) they fight with no seam holes | `kit/models` |
| Tuiri | gain: black suit with designed gold flames, girdle, gauntlets, knee guard; the bust is plainer than the 1999 gold relief; in game in her scene pose | `kit/models` |
| Succubus | gain: a crimson scaled bodysuit with bronze trims (more covered than 1999), the 1999 cut-out wings kept; in game (Succubus Lair) flying and fighting | `kit/models` |
| Queen Shillah | beats the projected draft, about even with 1999 (plumage reads as soft down at 256 px) | `kit/models-draft` |
| Death Mage, shaman, ghost | readable again (hood, wraps, 30 hanging strips), clean, but reads more like a cartoon mummy; the small painted skull is less menacing than the 1999 one | `kit/models-draft` |
| Dr. Evil | crisp scales and clear eyes, but the snout became boxy | `kit/models-draft` |

Tool fixes found by the builders:
* The bake left partial alpha on island borders; the converter cuts alpha at 50 %, so seams would have opened: the
  helper now grows the colours 6 px and writes the picture opaque.
* The bake let every other object in the scene cast occlusion on the model: only the target is visible during the bake.
* Seam hole under kept heads: an authored triangle was dropped when its dominant joint was kept, so the triangles that
  straddle the neck seam went and left a hole (the Queen's chin, Delon's dark neck patches). Now a triangle is dropped
  only when all three corners belong to kept joints.
* Open: per-part baking (loose pieces shade the body), texel weights per slot in `unwrap`, and an unwrap angle of 45
  degrees instead of 66 (stretching on faces seen edge-on) are done in the builders' scripts, not yet in the helper.

## Fourth round: the drafts finished

Owner: finish the draft items. Three builders reworked the last drafts against the lead's notes; all now ship from
`kit/models` (`kit/models-draft` is empty).

| Model | What changed | In game |
| --- | --- | --- |
| Death Mage (and ghost, shaman) | a 1.2x skull with brow ridge, deep sockets with an ember glow, cheekbones and teeth, set in shadow in a deeper hood; layered irregular blood-soaked rags (about 60 % red-brown, 40 % dirty bandage); 28 twisting strips with torn tips; the shaman plate engraved and worn; the ghost pale blue with the same rags | Volcanic: hooded, strips hanging, floating and animating, no holes |
| Dr. Evil | an organic head (domed brows into back-swept horns, sockets with scowling lids over three eyes a side, rounded snout with nostrils, curved tooth line), layered camouflage, scale size from fine (face) to large (back), pale plated throat and belly | converted (Rift World); not seen in frame (he rises from a pit in his boss scene) |
| Queen Shillah | 86 feather cards in shoulder, arm and flank rows sharing four painted feather tiles (they read at 256 px), skin with painted form, veined wings, a gold collar on the neck seam, girdle and bracelets | Succubus Lair: flying and moving around Rynn |

Still to see in game: Dr. Evil in his scene, the ghost mages (Rift World), and the Queen's feather cards while she flaps.

## Dr. Evil, rebuilt to follow the 1999 head

The owner judged the fourth-round Dr. Evil "insanely bad and childish" next to the 1999 one (cell-like outlined scales,
cartoon eyes with slit pupils, a puffy boxy snout) and asked for it to look like the original. It was pulled from the
shipped models, then rebuilt: the mesh follows the 1999 head view by view (wedge head, gabled forehead plate with a centre
ridge, fins and back-swept skull spikes, cheek and jaw spikes, lean V-keeled jaw), built without a remesh so nothing comes
out puffy, with modelled teeth, gullet and forked tongue; every slot projects the 1999 texture (no own texture) with
`radialJoints: []`, so the six pale eye ovals land intact on the plate-edge crease. The owner approved it and it ships.
Lesson: for a boss with a strong 1999 identity, a faithful remake with the 1999 skin projected beats a redesign.

## Delon with his own texture

Owner: the converted Delon "looks odd and not like the remodelled one". Cause: every slot of `kit/models/delon.json`
projected the 1999 texture onto the new body (blotchy smeared clothes, gore-red specks, pale patches at the top of the
legs from the 1999 hem strip, a hem strip on the trousers, a dark neck seam), so only the silhouette was the remodel.

What `delon_model.py --authored` does now (scratch and runs under `build/delon-model/own`, `build/authored/delon-own*`,
`build/ootf-run/delon-own*`):
* The authored mesh is the body only (2,234 triangles): the Blender head, ears, eyes, lids, scalp and hair are left out,
  the 1999 head is kept (`keepPcJoints: ["head"]`), and the new neck runs up under it. No neck seam is left, in the
  previews or in game.
* Every body slot is `texture: "image"`: one 512 px atlas (`kit/models/delon.png`, pinned in `kit/models.sha256`),
  painted per texel from the 3D position (no 1999 pixels) with the remodel's material colours as they render: linen
  tunic with folds, gathered skirt and a stitched, turned hem, shoulder and side seams, cuffs; a V-neck band painted by
  position; a stitched belt with a brass buckle; leggings with cross lacing up the outer thigh; knee boots with a
  turned-down top, ankle creases and a back seam; dark shoes. The skin tone was matched by eye to the kept face.
* Fixes found on the way: the hem ring is built downwards, so its faces pointed inwards (normals are now recalculated
  outwards before the bake and export); the V-neck cords decimated to a few long triangles that cut zigzags through the
  chest (left out, the trim is painted); the stepped body material border at the V is overpainted by position; the
  inside of the tunic skirt baked near black and showed as dark slivers when a thigh lifted it (the baked light is held
  at 35 % of the albedo, 70 % inside the skirt).

In game (`delon-own2`: Intro + Ruined Village, `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1`, the owner's `drakan.ini` with the
harness overrides replaced by its window 1280x960, `scale = auto` and `texture_upscale = auto`; still windowed, DLSS off as
in the ini): Delon beside Rynn in scene 1.1 and in the fight that follows reads like the remodel: clean beige tunic with
the dark V-neck, belt and brass buckle, brown leggings, boots, his 1999 face; no red specks, no pale thigh patches.

The first run (`delon-own`, before the floor inside the skirt) showed black slivers under the hem when Delon walked; in
`delon-own2` the hem is clean, with one small dark mark left at the back of the skirt in one frame of the fight.

Open: the atlas is many small islands (smart projection on the decimated remesh), so the 256 px game texture is soft;
a hand-made seam layout would give the tunic more texels.

## Julian ("Adamar", pinned by the spear) looked broken in his scene

Owner: "Adamar (the one with the spear in his shoulder) model looks extremely broken". The pinned man is Julian
(`julian (h)`, NPCs 3728): in Ruined Village scene 1.3 he sits against a post with the spear through his right shoulder and
plays the clips `1_3o` to `1_3z`. `warrior2` (Wartok Canyons) and the wounded wizard are other people; no level object or
string says "Adamar", the name only comes from the owner. Not a regression: the 09:27 `hum-rv` run (before the seam-hole
change) already showed it, and the shared helpers have had no later change that touches Julian.

What was wrong (in game, close-ups of 1.3 against the same run with the 1999 model):
* **Collar under the chin.** The authored robe went up to 1.53 m with a rolled collar at 1.52 m, right under the 1999
  chin. When his head drops forward the chin sank into it and the robe stood over the jaw in jagged pieces; the 1999 model
  shows a bare neck there. The seam corners (authored triangles kept along the neck, corners on the head joint) also
  stood up to 1.2 % of his height out of the 1999 neck as a ledge.
* **Weights.** Transferred weights are the barycentric mix of the nearest 1999 triangle. On the long 1999 faces that span
  a joint (the upper arm runs from a ring on the collar to a ring at the elbow, the forearm from the elbow ring to a
  hand-weighted wrist ring) a mixed vertex is pulled inside when the joints turn apart (linear blend), so shoulders,
  elbows and the robe folded in. Measured on his own clips: authored vertices left the posed 1999 surface by up to 4.3 %
  of his height (p99 2.5 %).

Fixes:
* Converter (formats `ootfauthored.ts`, `ootfassets.ts`): the transferred weights are now fitted to the model's own clips
  (`authoredPoses`: 4 samples per clip, at most 240; `poseFittedWeights`: the corner joints and their parents, least
  squares between the skinned vertex and its point on the posed 1999 face over those poses, a 2 % pull towards the
  barycentric weights, non-negative, at most 4). Authored corners of a kept joint are moved onto the kept (smoothed) 1999
  part and take its weights there, which closes the neck seam. `dump-pc-model.ts --authored` (and so
  `preview_authored.sh`) fits the same way. Tests in `test/ootfauthored.test.ts`.
* Model (`humans_model.py`, `julian.json`): the robe stops at the base of the neck (1.485 m) with the collar there, the
  neck tube runs from 1.46 m into the head and takes only the 1999 head textures (a new `neck` slot naming PC entries 0 and
  1; `human_kit.export` takes `slots`), so no robe green lands on it.

All 28 authored models, on clip times the fit did not sample (3 per clip, `build/adamar/evalall.ts`, scratch): the gap to
the posed 1999 surface fell for every model, e.g. Julian mean 0.31 to 0.17 % of height, p99 2.5 to 1.4, max 4.3 to 2.6;
goblin p99 8.1 to 3.4; Succubus max 27 to 10; male villagers p99 2.3 to 1.6, mine slaves 2.6 to 1.5. A first version
also stopped a vertex from taking a joint whose 1999 part starts further along the bone; it tore a hand of the wounded
warrior (a vertex on his chest left for the torso) and was dropped.

In game (Ruined Village alone, campaign, the owner's video settings: scale auto, texture upscale auto, 1280x960 window,
DLSS off), scene 1.3: Julian sits pinned with a bare neck between the collar and the bowed head, the head on the neck with
no gap, no robe over the jaw, shoulders and arms like the 1999 ones in the same frames. Frames: `build/ootf-run/adamar-auth`
(before), `adamar-pc` (1999 model), `adamar-fix4` (after).

Open: his hand nearest the camera takes the blood of the 1999 wrist (the authored hand is smaller than the 1999 one and
projects from its bloody end); the other humans were measured, not looked at in game after the weight change.

## Delon back to the 1999 model

Owner, after playing the own-texture Delon: the shoulders still look awkward; revert his model entirely. `delon.json` and
`delon.png` moved to `kit/models-draft` (not loaded), so the converter uses the 1999 `Delon(h)` again. The other authored
models are unchanged.

## Julian back on the 1999 model; kept heads that read as separate

**Julian.** After the neck rebuild the owner still found him "very broken" in the deployed build. The converted mesh does
not explode: every clip of his (`ready`, `1_3o` to `1_3z3`), sampled every 0.1 s with the converter's fit and the jaw
upgrade (`build/adamar/clipcheck.ts`, scratch), stretches no edge by more than 9.2 % of his height (the 1999 mesh itself
16.7 %, the same joint at clip start) and is never more than 2.6 % of his height wider, taller or deeper than the posed
1999 mesh, so no huge triangles come from him (the ray-water stall in that scene is looked at separately). What reads as
broken is the surface at scene distance: the robe's tan and green 1999 halves projected on the new torso break into
sawtooth strips, and the bare right arm and shoulder by the spear are bulkier and blockier than the 1999 ones
(`build/adamar/neck166.png`). The fix for that is an own texture, not more fitting, so `julian.json` moved to
`kit/models-draft` and he is drawn with his 1999 model again. A rerun of `humans_model.py --which julian` writes him back to
`kit/models`.

**Kept heads.** Owner: "the villagers look like their head is separate from the rest of their models". Head close-ups of
every authored model that keeps a 1999 head (`build/heads/<model>/faces.png`, 1999 left, new right) and Ruined Village
opening runs with the owner's video settings (scale auto, texture upscale auto, 1280x960):
* Male villagers and the three mine slaves (one mesh): the cause. The 1999 neck is split between the head pages
  (entries 0 and 1: face, beard, hair) and the chest page (3); the new neck tube took each triangle's texture from the
  nearest 1999 triangle, so it alternated between beard and skin and drew a band of teeth under the jaw, with paler skin
  below. The neck now has its own slot that only takes the chest page (`VILLAGER_NECK`): in game the neck runs as one skin
  from the jaw into the shirt (kneeling villager in 1.2a: `heads-before` v116000 against `heads-after2` v118900; cart
  villager: v84000).
* Tuiri: her kept 1999 neck (with the back of the 1999 gold collar) stood with a ragged lower edge on her painted chest,
  a shade darker. She no longer keeps the neck (`female_model.py` `keep: []`); the new neck carries the painted skin up to
  the jaw, which matches the 1999 face closely (preview: neck 188,99,75 against cheek 175,110,91). JSON regenerated, the
  baked PNG kept (the mesh and UVs are unchanged). Seen in previews only; she was not caught close in game.
* Not changed: female villagers, farmer (keeps the 1999 neck), Rimril and the wounded wizard (hooded), the wounded warrior,
  the Queen and the Succubus: no seam or colour step in the previews or (female villagers) in the opening. The rack victim
  shows a few small teeth under the chin but is not placed in any level.

Open: the villagers' hands are thinner and crumple more than the 1999 ones when they hold them to the face; Tuiri and the
mine slaves still need an in-game look in Alwarren and Grimstone Mines.
