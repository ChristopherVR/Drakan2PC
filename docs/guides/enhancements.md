# PC enhancements compared with PS2

Everything here is optional. Windows has the full set; Linux and browser builds have some [platform limits](../reference/portability.md).

## At a glance

| Area | PS2 | PC |
| --- | --- | --- |
| Frame rate | 30 FPS | 30, 60 or uncapped |
| Resolution | Standard definition, 4:3 | Up to 4K, widescreen |
| Textures | Original | HD texture pack generated from your disc, or live upscaling |
| Rendering | Original | Optional DLSS / DLAA, ray-traced water and shadows |
| Controls | DualShock | Mouse look, WASD, rebindable keys, XInput controllers |
| Bow | Original sight | Crosshair aiming: hold left mouse to aim, release to shoot |
| Combat | Weapon and shield | Optional dual wielding |
| Inventory | Inventory pages | Modern Tab inventory, M map, I inventory strip |
| Menus | Console menus | PC Settings (F1), modern title, pause and save screens |
| Movies | 512×512 | 4× upscaled intro, ending and logo |
| Subtitles | Dialogue only | Intro and ending narration too |
| Saves | Memory card | F5 quicksave, F8 quickload, save previews |

## HD textures

<!-- media:hd-compare -->

The HD pack is generated on your PC from your own disc; see [HD textures, video & subtitles](extras.md).

## Frame rate

Choose **30** (the original), **60** or **Uncapped** under PC Settings → Display. Game speed stays the same at every rate.

<!-- media:frame-rate -->

## Ray tracing and DLSS

On a DirectX Raytracing GPU (Windows), **modern water** adds ray-traced reflections and waves, and **ray-traced shadows** add soft sun and light shadows with ambient occlusion.

<!-- media:rt-compare -->

On NVIDIA RTX cards, **DLSS** upscales the 3D world for a higher frame rate, and **DLAA** uses the same model purely for antialiasing.

<!-- media:dlaa-compare -->

All of these are separate switches in [Graphics & PC settings](settings.md).

## Mouse and keyboard

Mouse turns Rynn and tilts the camera directly, with no stick emulation. **F10** releases the pointer.

![Rynn turning and looking up and down with the mouse in Surdana](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261029/mouse-look.jpg)

**Modern bow aiming**: with a bow out, hold left mouse to draw and aim at the crosshair, release to shoot. Choose **Classic** for the original controls.

![Rynn drawing the bow, aiming at the crosshair and firing two arrows](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261029/bow-aim.jpg)

**Dual wielding** puts a second one-handed weapon in Rynn's left hand instead of a shield. Turn it on under **PC Settings → Gameplay → Combat**.

![Rynn holding a one-handed weapon in each hand](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/dual-wield.jpg)

Bindings and settings: [Controls & combat](controls.md).

## Inventory, map and menus

**Tab** opens the modern inventory (Windows), **M** the full-screen map and **I** a quick inventory strip. Each can be switched back to the original screens independently.

<!-- media:tab-inventory -->

<!-- media:map-view -->

<!-- media:inventory-strip -->

![Modern save browser with a Surdana save preview](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/save-browser.jpg)

## Movies and subtitles

The intro, ending and logo movies can be replaced with 4× upscaled versions built from your disc. Intro and ending narration can show subtitles from local SRT files. Setup: [extras guide](extras.md).

![A dragon illustration in the locally upscaled intro movie](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-movie.jpg)

![Intro narration with the added subtitle overlay](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/narration-subtitles.jpg)

## Playing it the original way

Choose **30 FPS**, the **Original** inventory and menus and **Classic** bow aiming, turn off dual wielding and the graphics options, and set `map_view = 0` and `inventory_strip = 0` in `drakan_input.ini`.

## Implementation notes

[Settings schema](../../runtime/settings/src/Schema.cpp) · [input defaults](../../runtime/input/src/Config.cpp) · [saves](../reference/save-slots.md) · [HD movies](../reference/fmv.md) · [subtitles](../reference/subtitles.md) · [platforms](../reference/portability.md)
