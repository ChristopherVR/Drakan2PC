# Editor: Unreal-style UI pass and Quick Add

Direction: a level/mod editor that looks like UE5 (dark neutral greys, one accent, compact toolbar, docks, Outliner and
Details on the right, content drawer at the bottom) but is simpler: guided ways to build things.

## Done

- **+ Add (Quick Add)** at the left of the main toolbar (`editor/packages/app/src/editor/quickadd.ts`). Rows: Arena (PvP
  map), Puzzle (plates open a door), Cut scene, Dialogue line, Quest, Vendor, Menu page, Level copy. Each row forwards to
  an existing command (`task.multiplayer`, Logic template `pressure-plates`, `story.new-cutscene`, `story.new-dialogue`,
  `story.quest-marker`, `panel.vendors`, `panel.menus`, `task.newlevel`) and every row is also a palette command
  `add.<id>`. Features add their own row with `registerQuickAdd({...whenCommand})`; Vendor and Menu page show up as soon
  as their panel commands exist.
- The start page leads with the same rows (cards), then Edit a campaign level, Change the story, Edit a save and the OotF
  import. It redraws when features register.
- Viewport overlays (display menus, transform tools, view cube) are hidden over the start page.
- The "?" guide button of the bottom-dock panels moved into the dock tab strip (it opened over the panels' own buttons,
  for example Open folder in the Project panel). It opens the guide of the tab being shown.
- One corner radius (3px; 4px for floating cards), small-caps menu section labels, a shorter default bottom dock in
  windows under 800px high.

## Checked

Screenshots under `build/editor-shots/ue5/` at 1280x720 and 1920x1080, dark and light, against the player's ISO (Level 1A):
start, Add menu, level, selection, every bottom tab. Tabs go icon-only when the dock is narrow (11 tabs at 1280).

## Open

- The bottom tab strip is icon-only at 1280 wide with 11 tabs; grouping rarely used tabs (History, Mod) under a "More" tab
  would keep labels.
