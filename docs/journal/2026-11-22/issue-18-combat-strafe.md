# Twin-stick strafing after using Next Target/Strafe

Reproduced the reported loss of strafing in Ruined Village with the Automatic layout and a drawn blade. In the Standard
button scheme, releasing L2 while holding the left stick sideways made the game restore the native turn mapping on the
left stick. The temporary right-stick turn mapping remained after that update. The next input sample therefore saw two
turn sticks and stopped applying twin-stick. [Issue report](https://github.com/ChristopherVR/Drakan2PC/issues/18#issuecomment-7069798902)

The input layer now restores the untouched right-stick mapping when the game changes just the left stick. The selected
twin-stick layout also handles the native aim mapping in Dual Control, where the right stick's turn axis can temporarily
be reassigned to strafe. Horizontal sensitivity stays positive when vertical look is inverted.

In a replay on the deployed Windows build, the left stick produced a strafe value of 0.64 and zero turn before, during and
after the L2 sequence. A subsequent R2 press kept that behavior, and the right stick produced a turn value of 0.23 and zero
strafe. Before the fix, the left stick produced 0.23 turn and zero strafe on the samples after releasing L2. The before and
after RAM captures are in `build/harness/issue18-combat-before` and `build/harness/issue18-combat-after`.

A separate run held the left stick fully sideways for 1.7 seconds from the same position and heading with no target
locked. Rynn moved 1,570 horizontal units with the blade sheathed and 1,798 with it drawn. The reported slowdown did
not appear in those two samples. The captures are in `build/harness/issue18-strafe-speed`.
