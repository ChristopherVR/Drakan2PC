# Debug menu and campaign return

The previous PC build entered native camera mode 7 for Fly. A 1.5-second W hold and a mouse movement left its pose
unchanged in Surdana. The native camera tick at `0x423338` copies the pose from the target selected by
`camera+0x25a` every tick. The PC controls now steer that target using the bound movement keys or left stick and
mouse or right stick. PadMgr receives gameplay buttons with neutral analogue axes so native steering does not
compete with the PC controls.

The final PAL build loaded an Order of the Flame quicksave in Ruined Village. W moved the camera 22,036 world units,
the left stick moved it 20,087 units, and Space raised it 16,503 units. RAM snapshots showed rotation after both
mouse movement and right stick input. Rynn's position stayed fixed until Fly was toggled off.

In the same run Crosshair showed and hid the centre reticle over the world. Suicide closed the debug menu and F3
could not open it on the death prompt or the title. With campaign choice enabled, the player selected Order of the
Flame at startup and reached its main menu after waiting through the death prompt. The Modern title showed the
campaign name and Continue from its save card. The Original title also went directly to its main menu and kept
Switch Campaigns available. Starting The Ancients' Gates in the chooser and waiting through a Surdana death
returned to its own Modern title with its Surdana quicksave. A separate Surdana run selected debug Quit and the
process completed normal shutdown with exit code 0.

The Order of the Flame test gave a fresh character 1,000 XP on a private card and then used Level Up. The character
reached level 2 at 2,182 XP. The old pulse drew outside the gauge at both 4:3 and 16:9. The port now
uses the gauge fill texture for a translucent pulse inside the frame. A test with 3,500 total XP showed the
actual progress fill under the pulse and after it. Increase Gold added zero in its default disabled state.
With the setting enabled, Left, Right, and Enter each awarded 10,000 gold.

The runtime screenshots, logs and camera samples are in `build/harness/debug-ootf-final`,
`build/harness/debug-d2-final`, `build/harness/debug-native-title-fixed` and `build/harness/debug-quit-fixed`.
The XP and gold checks are under `build/requested-fixes` and `build/harness/requested-gold-*`.
