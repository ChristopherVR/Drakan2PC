# Controller Quick Inventory taps and holds

The reporter confirmed that twin-stick strafing works. The follow-up was about needing a confirm press to draw,
sheathe or change weapons when the Quick Inventory contains items.
[Controller follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/18#issuecomment-7078417820)

On Windows PAL, a short press of the live scheme's Quick Inventory button uses the game's own draw or sheathe action.
If Rynn is holding a weapon, the tap selects it before running the action, even if the full inventory last highlighted
another item. Holding the button for 250 ms sends the usual Quick Inventory press. If the player then highlights a
different weapon with the D-pad, releasing the held button equips it. A consumable still needs the usual confirmation.

In a Ruined Village replay with two quick-slotted weapons, the first tap put away the equipped weapon even though an
item in a different cell was selected. The second tap put the same weapon back in Rynn's hand. In Shadowmire, release
equipped a highlighted weapon and closed the bar. Releasing on a highlighted consumable left the bar open and its
quantity unchanged. The private replays are in `build/harness/issue-quick-ootf-equipped-final` and
`build/harness/issue-quick-d2-equipped-final`. A second Ruined Village replay switched directly between two weapons
with one already drawn, in `build/harness/issue-quick-ootf-switch-final`. These runs used the Standard controller scheme.
