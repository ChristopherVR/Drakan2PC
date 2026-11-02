# Story in the level view (2026-11-02)

The user's complaint: "Make a cut scene" sat in a Story sub-tab with typed coordinates, and nothing showed where cut
scenes, shops, quests or conversations are in the map. Now the level view draws them (UE5-style viewport sprites with
labels) and new cut scenes are made where you are looking.

## What the level view shows

Show menu, new "Story" section (all on by default, stored with the view options as `story`):

| Group | Badge | Also drawn | Double-click opens |
| --- | --- | --- | --- |
| Cut scenes | pink clapper, first sequence name | camera path of every sequence in its Sequence List (green start, red end), dashed lines to the objects the sequences move, its trigger boxes (translucent pink), dashed line from any non-box starter | Story > Cut scenes > Timeline on that sequence |
| Shops | violet bag, "Blacksmith, 16 items" | | Vendors, with that shop selected |
| Quest areas | green "!", quest title | start boxes green, finish boxes amber, each labelled "Starts: / Finishes: <title>"; lines from other starters (talking characters, levers) | Story > Quests on that quest |
| Conversations | blue speech bubble, "Talk: Farmer" (speakers of the object's own first-meeting lines, Rynn left out) | | Story > Conversations on that handler class |

Hover gives a card in plain words ("Starts when the player walks into the box", "Moves 6 objects", "Sells 16 items").
Click selects the object; double-clicking the badge or the object itself opens its editor. Far badges collapse to the
icon; labels return when near, hovered or selected. Lines and boxes of the selected / hovered mark are bright, the rest
dim.

Data is what the editor already reads: logic roles (`cutscene`, `quest`) and volumes from editor-core, vendor schemas and
`readVendor`, the conversation field `Beginning Dialog Exchange`, sequences and handlers from the content session,
quest titles from the story session. Quest-role objects of the story-event class (0xdc, many in Surdana) only get a badge
when something links to them; journal entries (0x68) always do.

Found on the way: the story folder is per chapter, not per level. Surdana (`LEVEL1NA.LVL`) keeps its sequences in
`LEVEL1ON`, one of its ODBs. The Story editors used the file stem, which silently opened the first folder (`LEVEL0RO`).
`storyStemCandidates` / `openLevelStory` (story/storyactions.ts) pick the file stem, else the first ODB stem that is a
story folder; the Story tab and the overlay both use it now.

## Making things in place

- Right-click the ground: "Add cut scene here". Right-click an object: "Add cut scene at this object" (the camera looks
  at it and it becomes an actor of the new sequence). Command `story.place.cutscene` (palette "Add cut scene") does the
  same at the viewport centre ground point, or at the one selected object.
- Story > Cut scenes > Timeline has "New cut scene" (primary button): same, at the camera target.
- A new cut scene is one undo step through the `cutscene-trigger` template: a cut scene player (Triggered) with the new
  sequence and a 3 x 2 x 3 cell trigger box. The sequence gets the level's Camera object (most used by its other
  sequences) and a 4 s camera move from the viewport camera 40% towards the spot (`starterCameraPoints`). It is
  selected, drawn at once, and the timeline opens on it. The old form is still there, renamed "Build from camera points".
- Quests: with one quest object selected (click its "!" badge), right-click the ground for "Add quest start area here" /
  "Add quest finish area here" (`volume-message` template, Quest Initiate / Quest Complete). Commands
  `story.place.quest-start` / `story.place.quest-finish`.
- Shops: "Add shop here" and `story.place.vendor` are listed but greyed: no template exists, and a shop also needs the
  character who opens it (that link is not decoded). Edit existing shops in Vendors.

For the left Place panel (another agent): `storyPlaceables()` in `story/storyactions.ts` lists the entries (category
"Cut scenes", "Quests", "Shops", title, hint, glyph, `command`, `unavailable`); `runCommand(entry.command)` places one;
`storyGlyph(group)` in `story/storyview.ts` draws the matching icon.

## Proof (static `vite build`, private Playwright context, real disc, Surdana)

Shots in `build/scratch/story-viewport/` (script `shot.mjs`):

- `1-level.png` overview, 103 badges; `3-cutscene.png` #7809 "Revised Heuge Thieves": pink trigger box, 6 actor lines,
  hover card (34.3 s); `3-cutscene-open.png` timeline on sequence 1354.
- `4-quest.png` "Brekk the Blacksmith" (journal title from SYSTEM.XDB) and `4-quest-open.png` the Quests page on 4149.
- `5-vendor.png` "Blacksmith, 16 items", `5-vendor-open.png` Vendors.
- `6-talk.png` "Talk: Farmer", `6-talk-open.png` the conversation.
- `7-menu.png` the ground menu (quest entries greyed with "Select one quest first"); `8-new-cutscene.png` new sequence
  1411 "New cut scene 1" in the timeline; `8c-new-zoomed-out.png` its badge, box and camera move in the level.
- `9-show-menu.png` the Story section; switching Conversations off removes the talk badges (0 left).

Tests: `packages/app/test/story-map.test.ts` (synthetic classes: marks, tones, lines, hidden / switched-off groups,
starter camera move, view option parsing).

## Not done / open

- The Story editor opens as its own tab, which hides the level view; "Dock in Level Editor" in its header shows both.
- Conversation focus opens the handler class; a placed handler that overrides its phase arrays (all 52 in Surdana) is
  not shown per object there (known since 2026-11-01).
- Quest start / finish areas need a quest object in the level first (Story > Quests > Place a quest marker).
- No new shop placement (see above). No in-viewport editing of camera keys yet (edit them in the timeline; the path
  redraws).
- Not run in game: a cut scene made with one click (same writer as the proven "Make a cut scene", untested start pose).
