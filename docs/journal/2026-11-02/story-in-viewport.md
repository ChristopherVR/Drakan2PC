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

## Follow-ups

### Story placements in the Place panel

The left Place panel lists `storyPlaceables()` under their groups, above the editor set-ups and the classes, with a
"Place in the level" sub-heading: Cut scenes > Cut scene, Quests > Quest start area / Quest finish area, Vendors >
Shop. Each row has its viewport badge glyph (`storyGlyph`) in the badge colour (`STORY_COLORS`).

- Click or Enter places at the view centre (the same as the `story.place.*` command); dragging the row into the
  viewport shows the ground marker and places at the drop point. Placement at a point goes through a small placer
  table in `story/storyactions.ts` (`registerStoryPlacer` / `storyPlacer`; `register.ts` registers one per command with
  the same "why not" check the command's `enabled` uses). The drag is `attachPointDrag` in `editor/assetdrag.ts` (the
  class drag without the class: it reads `view.groundAt` on drop instead of `dragDrop`).
- A row that cannot be placed is greyed and shows the reason in place of its hint ("Select one quest first (click its !
  badge in the level).", the shop's "Not yet: ..."); clicking it says the same in a toast; it cannot be dragged. The
  rows repaint on selection and document changes, so selecting a quest's "!" badge turns the quest rows on.
- Placed rows join "Recently placed". Pure parts (`storyCategory`, `sentence`, `placeableReason`) are tested in
  `packages/app/test/story-place.test.ts`.

### Story beside the viewport

Story's home is now the Level Editor's bottom drawer (`ia.ts`, `home: 'dock'`), like Unreal's Sequencer: the viewport
stays visible while editing. When it is newly docked (or the drawer was shut) the drawer grows to at least half the
Level Editor's height (`dockShare: 0.5`, `Layout.openBottom(share)`, pure `grownSize` in `splitmath.ts`); a drawer the
user made bigger is kept.

- Opened from the level view (double-click a story badge or its object, "Add cut scene here", the Place panel, the
  Quick Add story rows) it uses `Workspace.showBeside('story')`: the drawer, unless Story is open in its own tab right
  now (the user popped it out), in which case that tab comes forward. It does not change the remembered place.
- The dock tab's "Open in its own tab" button pops it out (remembered); the tab's header "Dock in Level Editor" brings
  it back. `openIn` now closes the editor only where it is open in the other place (a pop-out of an editor docked from
  the viewport while its remembered home was "tab" used to leave it in both).

Checked with a static `vite build` and a private Playwright context, PAL disc, Surdana, 1700 x 1000 (shots in
`build/scratch/followup-story/`, script `shot.mjs`): Cut scenes group lists the live row (`1-place-cutscenes.png`);
quest rows greyed with the reason (`2-place-quests-greyed.png`), the shop row greyed (`2b-place-vendors.png`); dragging
"Cut scene" onto the ground made sequence 1411 "New cut scene 1" and opened the timeline docked under a 1032 x 448
viewport (`3a-dragging-cutscene.png`, `3-dropped-cutscene-docked.png`); after selecting a quest badge the quest rows are
live (`4a-place-quests-live.png`) and a click added "Quest start area added for Brakks Icon." (`4-quest-start-placed.png`);
closing Story and double-clicking a cut scene badge opened it docked on "Revised Heuge Thieves"
(`5-badge-opens-docked.png`); pop-out made it a tab (`6-popped-out.png`), a badge double-click then kept the tab, and
"Dock in Level Editor" put it back (`7-docked-back.png`). `npx vitest run` 715 passed, `tsc`, prettier on the touched
files.

Not checked: the desktop app, touch drag, the light theme; dropping a quest area by drag (same placer as the click).
