# Controller combat layout: thumbs stay on the sticks

The reporter (a former game designer) followed up on the quick-inventory change.
[Controller follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/18#issuecomment-7229911635)
Weapon selection felt better, but in combat the layout still made the thumbs leave the sticks. Directional attacks
are on the D-pad, so the left thumb gives up movement to attack. Browsing the quick bar takes extra buttons. Crouch or
dodge is better on a stick click. The proposal: a held modifier turns stick + attack into the directional move, and
while the quick bar is held the right stick steps it, with release equipping.

Changes (all on foot; Arokh and menus get the pad unchanged):

* `[gamepad] directional_attack = modifier` (default) with `directional_modifier = L2`: hold L2, push the left stick
  and press the scheme's Attack. It uses the keyboard's Left Alt path (`PadSynth`): one neutral sample, then Attack
  plus the D-pad bits, with the left stick centered for that press. L2 passes through. In scheme A that is Next Target,
  so the move faces the target. `tap`, `always` and `off` behave as on the keyboard.
* `[gamepad] stick_click_crouch = 1`: L3 also presses the scheme's Crouch.
* `[gamepad] quick_bar_stick = 1`: while the Quick Inventory hold is active and the bar is shown, the right stick X
  sends D-pad Left / Right steps (`QuickBarStickStepper`) and is centered in the game's sample.
* PC Settings > Controls > Gamepad has the four rows.

Attack and Crouch come from the live keymap (`nativePadButton`), so schemes B and C get R1 / Cross and L2 for them.

Verification: `gamepad_combat_test` (81 checks) covers the config text, stick quantizing, modifier / tap / always /
off, schemes A and C, no melee, mounted, menus, L3 crouch, the stepper's single step, repeat, edge gaps and
hysteresis, and the gesture's hold state. All 190 runtime tests pass.

Not verified in game. `scripts/harness/directional_attack_check.py --pad` adds controller controls (`pad`: L2 + stick +
Square must match the arrow move; `padplain`: stick + Square must not). Four runs on copies of `build/base-dist/mc0`
(`build/harness/issue18-pad-dir1` .. `4`) never reached scripted control. The newest Catacombs quicksave returned to
the title after 17 s. With Surdana-only and Shadowmire-only cards, the title's Continue child (screen 4) never took
the harness Cross. Another session rebuilt `dist/DrakanPC` between the runs (three different executable hashes), so
run the check again on a stable build. The right-stick bar stepping and L3 crouch have no harness yet.
