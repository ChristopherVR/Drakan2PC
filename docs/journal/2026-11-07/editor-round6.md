# Editor round 6: closing, landscape, placeholders, focus

Browser build (Vite, Chromium with software rendering), the PAL disc; the desktop app for the window and project checks;
the game from a copy of the play folder (`build/editorplay`, `mods.ini` put back afterwards).

## 1. Data loss on close: fixed

The shell (`closing.rs`, `lib.rs`) now holds a close request of the main window (File > Exit, Alt+F4, the close button)
and emits `app-close-requested`. The page writes its level autosave and, with a project folder, the project file, then
answers with the `close_ack` command; a page that does not answer is waited for 3 s at most. Verified in the desktop
app: an object placed and the window closed at once left the project file with the object (4677 to 4678 objects, the
window gone after about a second). Rust tests cover the wait (answers at once, gives up). Not verified: OS shutdown (the
window receives no close request there that this code can see), and the same hook for the separate Save editor window.

Missing project folder: opening a recent project whose folder is gone now says "... is no longer a project folder; it was
removed from the recent projects" and drops the entry (`recent_project_forget`, tested). There is no Save-as in the
desktop app: Save writes in place, and a copy is made by creating a new project folder; that gap is unchanged.

## 2. Landscape: why the drag "did nothing", and what works

My earlier scripted drag ran while the ground was still loading: the panel then shows "No ground to edit yet" and the
tools are hidden. It now says "The ground of this level is still loading. The tools appear when it is there." when a level
is open. With the ground loaded: Raise, Lower, Smooth, Flatten, Ramp and Noise each change the terrain (sum of height
differences over 81 sample points 8.7k, 8.2k, 20k, 106k, 45k and 1k units), Ctrl+Z restores the heights exactly (0
difference) and Ctrl+Y re-applies them exactly. Three Raise strokes and a Paint stroke gave "9 layers changed"; the export
summary says "9 terrain layer(s) edited in LEVEL1NA.LVL", and the terrain entry parsed back from the pack matches the
editor's heights at all 81 sample points (worst difference 0) while 75 of them differ from the disc. Paint was checked
for the layer count, not by comparing texture ids.

## 3. Gameplay of the exported pack: not demonstrated

A pack with four lever-and-door pairs around Rynn's start (each linked Opens, 8 changes) loads in the game
(`mods: loaded 'my-campaign-mod' (lever-pack)`), the level reaches gameplay in about 60 s with the player placed by
`DRAKAN_PLAYTEST_POS`, and a recorded run (walk forward 1.2 s, tap E, screenshots at 0.6, 2 and 4 s afterwards) shows a
large dark object in front of the camera that did not visibly change. I could not tell from the screenshots whether that
object was the door, whether Rynn was in reach of a lever, or whether the lever fired, so "the door opens in game" is NOT
shown. The first two runs were useless because the pack's file name must equal its `mods.ini` key (`lever-pack`).

## 4. Placeholder rows

`ui.ts` `skeletonRows` plus `loadstate.ts` (a counted loading flag; disc open and level read both set it, failures and
cancels end it): the Outliner and the Content Browser show quiet placeholder rows (role status, aria-busy, no animation
under reduced motion) instead of "No level open". Checked in the browser during a disc open and a level open; the rows are
gone afterwards. Tests: `loadstate.test.ts`.

## 5. Focus, landmarks, dialogs (keyboard and structure)

Checked with the keyboard: the command palette, the export dialog and the help drawer take the focus when they open, close
on Escape and return the focus to the control that opened them. **Fixed:** the palette dropped the focus to the top of the
page. Landmarks: header, nav, main, aside and footer exist, headings run H1 > H2 > H3 on the start page; the template
details and the campaign wizard steps were unlabelled and now have labels. Not done: a real screen reader run, a check that
a modal dialog keeps Tab inside it in every browser (native `<dialog>` modal behaviour is relied on), and two `main`
elements (the editor and the Disc Files view, one hidden at a time).

## 6. Worker test

`readPristineDiscFile` (virtualdisc.ts) is the function behind the worker's `readDiscFile`; its test lays a converted pack
over a synthetic disc and checks the converter's read still returns the disc's bytes, null for a missing file, and an error
without a disc.

## Not done

Trigger to enemy spawn, the shop form to the end with the shop opening in the game, a new level exit chained to a second
level (map and game), and the lever opening the door in the game (see 3).
