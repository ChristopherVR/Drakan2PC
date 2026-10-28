# Controls & combat

Default PC bindings. Rebind anything under **F1 → Key Bindings**. Linux and browser input differences are in the [platform reference](../reference/portability.md).

## Essential controls

| Input | Action |
| --- | --- |
| W / A / S / D | Move forward/back and strafe |
| Mouse | Look and turn |
| Left Shift | Walk |
| Space | Jump |
| Left Ctrl / C | Crouch |
| Left mouse | Attack; with modern bow aiming, hold to aim and release to shoot |
| Right mouse | Parry / magic / original bow sight, depending on equipment and mode |
| F / middle mouse | Toggle target lock |
| E | Use / call Arokh |
| Q | Quick Inventory |
| 1–5 | Use or equip a quick-slot item |
| Mouse wheel | Previous / next weapon |
| Tab | Full inventory |
| I | Inventory strip |
| M | Full-screen map |
| Esc | Pause / back |
| F1 | PC Settings |
| F5 / F8 | Quicksave / quickload |
| F10 | Release / recapture mouse |
| F11 / Alt+Enter | Borderless fullscreen on Windows |

Quickload discards unsaved progress without asking and uses its own slot, not your newest manual save. See [Saves & updates](saves.md).

## Mouse capture and menus

The pointer is captured during gameplay and released in menus or when the window loses focus. **F10** toggles capture. Clicking back into the game recaptures it without attacking.

![Rynn turning and looking up and down with the mouse in Surdana](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261029/mouse-look.jpg)

In menus, use the mouse or arrows/WASD to select, **Enter / Space** to confirm and **Backspace / right mouse** to go back. **PgUp / PgDn** switch inventory sections or save pages where supported.

## Rebind keys and adjust the mouse

Open **F1 → Key Bindings** to change actions, and the mouse settings for sensitivity and inversion. Windows bindings are stored in `drakan_input.ini` next to the executable. Close the game before editing the file by hand.

The Windows controller path uses XInput and maps the original DualShock layout onto the controller. Controller and keyboard prompts can be selected in PC Settings.

## Modern bow aiming

Enable **Modern bow aiming** in the mouse settings. With a bow equipped, **hold left mouse** to aim with a slight zoom and a crosshair; **release** to shoot toward it. Turn the setting off for the original controls: left click shoots and right click opens the original sight.

The corresponding setting in `drakan_input.ini` is:

```ini
[mouse]
bow_aim = modern
```

Use `classic` for the original behavior.

![Rynn drawing the bow, aiming at the crosshair and firing two arrows](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261029/bow-aim.jpg)

## Optional dual wielding

In **PC Settings → Gameplay → Combat**, enable **Dual wielding**. While Rynn holds a one-handed weapon, equip another one-handed weapon to put it in the left hand instead of a shield. Strikes alternate between the two hands. Use it again to stow it.

In `drakan.ini`:

```ini
[general]
dual_wield = 1
```

Set it to `0` to turn it off; the off-hand weapon goes back to the inventory. Details: [combat reference](../reference/combat-controls.md).

![Rynn holding a one-handed weapon in each hand](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/dual-wield.jpg)

## Tab inventory, I strip and M map

Choose **Gameplay → Inventory screen → Modern / Original** for the full Tab inventory. The dedicated I and M views are independent. To return these shortcuts to ordinary inventory pages, edit `drakan_input.ini`:

```ini
[menu]
map_view = 0
inventory_strip = 0
```

Use `1` to turn each view back on.

<!-- media:tab-inventory -->

<!-- media:inventory-strip -->

<!-- media:map-view -->
