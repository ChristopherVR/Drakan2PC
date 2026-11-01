# Story and Logic workflow redesign (2026-11-01)

User feedback: the story / logic flow is not user friendly. Goal: easier in the spirit of UE5 (graph for logic, timeline for cut
scenes, a clear quest and dialogue editor) but simpler. Screenshots: `build/editor-shots/storyux/before` and `.../after`
(not committed). The probe scripts are in `build/sux/` (own Playwright browser, own static server on port 5261).

## Friction found as a new modder (Level 1A, before)

Logic

* The graph started empty ("Select a trigger, door, cut scene...") and lived in a bottom dock about 200 px tall. "Whole level"
  on Surdana drew 654 tiny cards in a tangle.
* Cards were named by class and id ("Trigger Detector", "#18254  Trigger volume"). There was no role badge, and links were
  labelled with raw game message names ("Unlock", "Play Sequence +1").
* Dragging from the dot linked silently. The message was changed later in a side panel from a list of 33 raw names
  ("0  Off, 1  On ...") for a message the source owns.
* Making a plate open a door: finding the door meant Outliner search, expand Usable, expand the class, click (4 steps). The
  "Pressure plates puzzle" form then had a Target list of about 400 "#id Class" lines (hundreds of Timers and Barrels) and a
  Plate class that said "none in this level" on Surdana, so the setup could not be created there at all.
* Problems (a trigger linked to nothing, a message the door ignores) only showed in the Validation tab, not on the object.
* No way to add an object to the graph, and no drag from the Outliner.

Story

* Eight tabs in one row (Strings, Quests, Dialogue, Cinematics, New dialogue, New cut scene, Quest marker, Story pack):
  editing and creating were separate places, and four of them each had their own "Load ..." button.
* The level-folder select ("LEVEL0RO") was cryptic and always started on the first folder, not the open level.
* Dialogue: a flat list of lines by class id, details such as "Class 4167 - plays 1 sound - sound mode 0 - camera position 2".
  A conversation's stages were called Beginning / Incomplete / Complete with no hint when each plays.
* Quest: the usage text told the modder to "link a trigger to it with message 1" (message 1 is On, not Quest Initiate).
* Cinematics: the timeline was a read-only canvas with 14 px rows. Retiming meant typing a number in each action row.

## What changed

Model (`editor-core/src/logic-plain.ts`, tests in `test/logic-plain.test.ts`)

* Plain message words chosen from the target's role: a door "opens / closes / unlocks / locks", a cut scene player "plays the
  cut scene", a quest "starts / completes the quest", a counter "counts one down" ... (`messagesFor`, `plainMessage`).
* Role names (Trigger, Door, Timer, Counter, NPC, Enemy, Quest, Cut scene ...), pin sentences ("when the player steps in"),
  card summaries.
* `linkWithMessage`: add the link and set the message the source sends in one edit.
* `graphProblems`: per-card problems in plain words (trigger linked to nothing, message the target ignores, counter nothing
  counts down, quest nothing starts, cut scene nothing plays). Known only for targets whose reactions are known (door,
  cut scene, quest, conversation, counter), like the level validation.
* `matchScore` for the searchable menu and `NEW_NODE_KINDS` for what it can make.

Logic panel (`editor/logicgraph.ts` is new, `logicpanel.ts` keeps the toolbar, setups form, exit picker and validation)

* Cards: role band, object name, one line saying when it fires; round input and output pins; a yellow mark with the problem
  on the card. Links carry the plain word ("opens") in a pill; click it to change it. A link that the target ignores turns red.
* Drag from an output pin onto a card: a short list ("Opens, Closes, Unlocks, Locks, Something else...") links and sets the
  message in one undo step. Drop on empty space: the add menu opens, and what you pick is made and linked.
* Add node (button, A, double-click or right-click on the canvas): one searchable menu with the level's objects (what is on
  screen first), new triggers / plates / levers / timers / counters / doors / cut scene players / relays, and the ready-made
  setups. Objects can also be dragged in from the Outliner.
* Clicking a card selects the object in the viewport and a viewport selection redraws the graph around it (the layout is kept
  while you click cards of the drawn graph). Tidy up lays out left to right; Fit zooms.
* Side panel in words: "When it fires, it tells ...", "Told by ...", per link the plain word and a remove button, "Pick in the
  level", "Add a node to tell", and the raw fields folded under "All settings".
* The viewport link handle, "Link to..." and the edge pill all use the same message list (`editor/linkmenu.ts`).

Story (`story/*`)

* Four places with a second tab row: Quests (Quests, Place a quest marker), Conversations (Conversations, Write a new line,
  All lines), Cut scenes (Timeline, Make a cut scene), Text and export (Texts, Export). The old `story.<id>` commands still
  work (Quick Add uses them). Showing a place reads the disc, so no page asks for "Load" first.
* `story/conversation.ts`: a conversation as a tree. Each stage is a branch of line cards (speaker, editable subtitle,
  recording with a play button, up / down / remove); add a line from the level, or write a new one.
* `story/timeline.ts`: Sequencer style timeline for cut scenes: ruler you can scrub, one lane per actor and kind (moves,
  animation, sound, message), draggable keys that retime with one edit on release (snapped to 0.05 s), playhead follows play.
* `story/queststeps.ts`: for a quest, "Starts when" and "Completes when" lists read from the open level, with "The player
  enters an area" and "Something in the level fires" to add one, and problems for the quest object.

## Counts: clicks and steps, before and after

See the table in the final report of this task (also repeated at the end of this note).

## Open items and findings

* `QUEST_MESSAGE.initiate = 31` in `editor-core/src/story-create.ts` (quest giver wiring) disagrees with the message list and
  the ordinal table in `level-format.md` (30 = Quest Initiate, 31 = Item PickedUp). The new quest steps use 30. Whoever proves
  quests in game should settle which one the game honours.
* Items do not send messages, so "complete the quest when an item is picked up" is wired as an area around the item. The
  quest steps panel says so.
* The dock height (about 300 px) still makes the graph and the timeline cramped. The shell work that moves Story and Logic into
  their own editor areas should give them room; both views are self-contained panels registered through `registry.ts`.
* The Story folder select follows the open level only for the first load.
