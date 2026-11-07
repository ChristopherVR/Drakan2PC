# Editor round 5: converted-class names, wiring, desktop projects

Browser build (Vite, Chromium with software rendering), the player's PAL disc and Order of the Flame install; the desktop
app for the project flows. Timings and counts are from that machine.

## 1. The "class 16948" finding: found, fixed

Not a regression of the virtual disc and not the Surdana template. The importer read the template's class tables through
the converted-levels overlay. A second conversion in the same profile therefore saw the first conversion's new classes as
disc classes, mapped its objects onto them, and the level files kept those ids while the new pack (which replaced the old
one) did not define them. Evidence: a fresh two-level pack resolved every class ("237 of 277 decoded"), converting all
23 levels on top of it left 236 of 277 classes of Islands unresolved (FX ids 12176 and up, while the pack's own new FX ids
started at 12660, 874 of them against 160 when the command-line converter runs on the pristine disc), and the first
dozen Logic graphs went nearly empty because classes without names have no roles.

* New worker call `readDiscFile` reads the disc itself, and the importer uses it for everything the converter reads.
* Per level, converting all 23 levels twice in one profile: 0 unresolved classes on every level (before: Islands 236/277,
  Volcanic 176/208, Rift Crystal 70/163, Ruined Village 38/332).
* A class no ODB names reads "Effects class 12180" (kind of database plus id) instead of "class 12180".
* The Intro-versus-Surdana comparison is moot for this finding; Surdana stays the default template for the reasons in
  round 3 (all objects kept, the game runs the level).

## 2. First-user flows

| Flow | Result |
| --- | --- |
| Lever to door by dragging in the Logic graph | placed a door and a lever, selected both, Logic shows two cards, dragging the lever's round pin onto the door card opens the message menu (Opens, Closes, Unlocks, Locks, Something else, Keep its current message); choosing Opens writes the link (lever.links = [door]); the graph then reads "2 objects, 1 link" |
| Exported | the exported `LEVEL1NA.LVL` object list holds the lever with links [door id] (parsed back with the formats package) |
| In the game (copy of the play folder) | the pack loads (`mods: loaded 'my-campaign-mod'`) and the level load starts; reaching gameplay was not confirmed within 75 s on the busy machine, and pulling the lever in game was not done (no input automation for that) |
| Export warning | a class placed with no copy in the level is written with a default body: the export says so (unverified in game) |
| Landscape | the panel has Sculpt, Paint and Manage with Raise, Lower, Smooth, Flatten, Ramp and Noise brushes; a scripted drag in the viewport made no edit in my run (terrain was probably still loading), so painting and sculpting are not verified |
| Level exit | a placed Level Transition lists its 9 fields in Details including "Transition To..."; chaining a new exit to a second level in the campaign map was not exercised (changing an existing exit's destination was verified in an earlier round) |
| Shop and quest forms to the end, trigger to enemy spawn | not done |

## 3. Desktop projects

Two valid project folders and one corrupt one, opened from the recent list.

* Opening works from the start page, the project chip menu and the palette; the chip menu lists the recent projects by name.
* An edit in project A is written to A's folder by the project autosave (file size 3.1 MB after the edit); switching to B
  with that edit in place asks nothing and loses nothing (A's file is unchanged by the switch).
* The corrupt project is refused with "That file is not a saved level or project: it is not valid JSON" and the chip keeps
  the project that was open. **Fixed:** the message appeared only in the title bar; it is now an error toast too.
* The Project panel lists recent projects as folder name plus dimmed path (it showed only the full path).
* Closing the desktop window straight after an edit exits at once: WebView2 does not show the browser's before-unload
  prompt for a native close, so an edit younger than the autosave delay is lost. Not fixed (needs a close hook in the shell).
* Save-as and a project folder that is missing on disk were not exercised.

## 4. Interaction states and screen readers

* Dragging a class over the viewport frames it with a dashed outline (like the Logic graph already did).
* Error toasts use role alert, the status lines are polite live regions.
* No skeleton or loading placeholders exist for the Outliner and the Content Browser while a level loads (the loading card
  covers the viewport only); not added.

## 5. Sweep

See the end of this note for the last full run.
