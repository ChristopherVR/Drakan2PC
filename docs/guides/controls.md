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
| V | Camera view toggle |
| 1–5 | Use or equip a quick-slot item |
| Mouse wheel | Previous / next item; what it switches depends on what Rynn holds (Arokh's breath in flight) |
| Tab | Full inventory |
| I | Inventory strip |
| M | Full-screen map |
| Esc | Pause / back |
| F1 | PC Settings |
| F5 / F8 | Quicksave / quickload |
| F10 | Release / recapture mouse |
| F11 / Alt+Enter | Borderless fullscreen on Windows |

The keys are bound to PS2 pad buttons, so their meaning follows the game's own controller scheme (**Options > Controls**, schemes A, B and C) and changes while Rynn rides Arokh. The table is scheme A on foot. On Arokh in scheme A, **E** steps to the previous breath / item and **Q** attacks; schemes B and C move most actions to other buttons (in C, for example, E jumps and Q attacks). **F1 → Controls** names every key for the scheme the game is running, with the Arokh meaning next to it, and the controller view lists both layouts.

Quickload discards unsaved progress without asking and uses its own slot, not your newest manual save. See [Saves & updates](saves.md).

## Mouse capture and menus

The pointer is captured during gameplay and released in menus or when the window loses focus. **F10** toggles capture. Clicking back into the game recaptures it without attacking.

![Rynn turning and looking up and down with the mouse in Surdana](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261029/mouse-look.jpg)

In menus, use the mouse or arrows/WASD to select, **Enter / Space** to confirm and **Backspace / right mouse** to go back. **PgUp / PgDn** switch inventory sections or save pages where supported.

## Rebind keys and adjust the mouse

<!-- media:pc-settings -->

Open **F1 → Key Bindings** to change actions, and the mouse settings for sensitivity and inversion. Choosing a key that already has an action asks first: **Replace** moves the key to the new action (the old one loses it), **Use for both** makes one key send both, **Cancel** keeps everything. Windows bindings are stored in `drakan_input.ini` next to the executable. Close the game before editing the file by hand.

The Windows controller path uses XInput and maps the original DualShock layout onto the controller. Controller and keyboard prompts can be selected in PC Settings.

## Riding Arokh

The mouse turns Arokh, in the air and on the ground. **A / D strafe him sideways**, as they strafe Rynn on foot, with either steering mode (**F1 → Controls → Arokh → Steering**: Mouse or Right stick emulation). W / S fly forward and back, **Space** climbs and **Left Ctrl / C** dives. To make A / D turn him as on the PS2 pad, switch off **A / D strafe on Arokh** (`[keyboard] dragon_strafe = 0`); `[keyboard] strafe = 0` turns strafing off on foot and on Arokh. A physical controller's left stick keeps the game's own mapping.

## Modern bow aiming

Choose the bow style in **Options > Gameplay > Combat > Bow combat**:

* **Order of the Flame** (default): **hold left mouse** to draw the bow and aim over the shoulder with a slight zoom; a crosshair shows where the arrow will go and **releasing** shoots. Rynn can walk, strafe and back away the whole time, also while the arrow is released. Right click opens the zoomed sight with a crosshair, where she can still move, jump and shoot. A shot in the air keeps the jump going.
* **Original (PS2)**: the PlayStation 2 game's own bow (click to shoot), built around its zoomed sight.
* **Modern**: the same hold to aim, without the Order of the Flame handling of the zoomed sight.

The corresponding setting in `drakan_input.ini` is:

```ini
[mouse]
bow_aim = ootf     ; or native, or modern
```

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
