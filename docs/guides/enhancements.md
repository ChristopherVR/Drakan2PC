# PC enhancements compared with PS2

Drakan2PC keeps the original adventure and adds PC controls, presentation options and configurable gameplay improvements. The Windows build has the fullest feature set. Linux and browser builds have their own [platform limits](../reference/portability.md).

![Modern Tab inventory on Windows](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/modern-inventory.jpg)

## At a glance

| Area | PS2 experience | Drakan2PC enhancement | Your choice |
| --- | --- | --- | --- |
| Frame rate | Original world update cadence | **60 FPS and uncapped 60+ FPS support**, subject to hardware and scene complexity | Select 30, 60 or Uncapped; the original cadence remains available |
| Graphics | Original console presentation | Higher resolutions, widescreen, improved filtering and optional HD texture replacement | Adjust or disable enhancements in PC Settings |
| Advanced rendering | Original lighting and rendering | Optional DLSS Super Resolution / DLAA, ray-traced water and shadows | Windows, compatible GPU and build required; independently optional |
| Controls | DualShock controller layout | **Direct mouse look, WASD movement, rebindable keyboard and mouse controls**, controller support and keyboard prompts | Rebind controls, adjust sensitivity, or use a gamepad |
| Bow aiming | Original bow controls and sight | **Modern crosshair aiming**: hold left mouse to aim with a slight zoom; release to fire | Modern or Classic bow aiming |
| Combat | Original weapon / shield equipment | **Dual wielding** with a second one-handed weapon; attacks alternate hands | Configurable; disabled by default in the settings model |
| Inventory | Original inventory pages | Optional **modern full-screen Tab inventory** with PC navigation | Modern or Original inventory screen |
| Map and quick access | Map and equipment within inventory pages | **M** opens a full-screen map; **I** opens an inventory strip over the world | Map view and inventory strip can be disabled independently |
| Settings and menus | Original console menus | **PC Settings**, plus configurable modern title, pause and save/load screens | Choose modern or original menus where supported |
| Movies | Original low-resolution video | Optional **4× AI-upscaled intro, outro and logo movies**, generated locally from your disc | Use an HD movie pack or original video |
| Subtitles | In-game dialogue subtitles; no intro/outro narration text | Added **intro and ending narration subtitle support**, with scalable text | Supply local SRT files; narration subtitles can be disabled |
| Saves | Original memory-card save interface | PC save folders, configurable manual save pages, **F5 quicksave / F8 quickload**, and optional modern slot previews | Keep manual saves; quicksave uses its own slot |
| Platforms | PlayStation 2 | Native Windows, native Linux and a locally built browser player | Same supported PAL ISO; graphics features differ by target |

This is an experimental port, not a promise of a locked frame rate or complete feature parity across platforms. The illustrations below are separately hosted documentation captures. No playable game files, generated game binaries, movie packs or subtitle transcripts are included in the repository or site.

## Higher frame rates and enhanced graphics

The Windows renderer supports resolutions beyond the original console output, borderless fullscreen and widescreen presentation. World frame rate can be set to **30**, **60** or **Uncapped**. VSync and the presentation limiter are separate: changing only the limiter does not change the world's update cadence.

HD texture replacement, live texture upscaling, DLSS/DLAA and ray-traced effects are optional. Turn them off individually for a presentation closer to the original. See [Graphics & PC settings](settings.md) and [HD textures, video & subtitles](extras.md).

![Side-by-side texture comparison, original on left and HD replacements on right](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-textures.jpg)

![Display settings in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/display-settings.jpg)

## Mouse, keyboard and configurable combat

The port adds direct mouse look, WASD movement and strafing, key rebinding, sensitivity/inversion controls and mouse navigation of menus. **F10** releases or recaptures the pointer. XInput controllers remain supported on Windows.

**Modern bow aiming** is the optional crosshair-based aiming style: with a bow equipped, hold left mouse to aim and release to shoot. The setting is named “Modern bow aiming”; it changes the existing bow's controls rather than adding a new crossbow weapon. Choose Classic to restore the game's own bow controls.

**Dual wielding** lets Rynn equip another one-handed weapon in her left hand instead of a shield. Attacks alternate hands. Enable it under **PC Settings → Gameplay → Combat**. Turning it off returns the off-hand weapon to the inventory when gameplay resumes.

The [controls guide](controls.md) covers bindings and configuration keys.

![Rynn aiming with the modern bow crosshair](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/bow-aiming.jpg)

![Rynn holding a one-handed weapon in each hand](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/dual-wield.jpg)

## Inventory, map and PC menus

The **modern Tab inventory**, **M map** and **I inventory strip** are three separate choices. Switching Tab back to Original does not disable the dedicated map or strip. The modern inventory is currently a Windows feature; portable inventory/map overlay parity remains unfinished.

PC Settings gathers display, sound, input and gameplay choices in one place. Modern title, pause and save/load screens are also configurable. On Linux and browser, the shared modern title, pause, save/load and settings models have a portable rendering path; available graphics settings still depend on the target.

![Full-screen map showing Surdana and objectives](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/fullscreen-map.jpg)

![Inventory strip over gameplay with a bow selected](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/inventory-strip.jpg)

![PC Settings categories in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg)

![Modern save browser with a Surdana save preview](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/save-browser.jpg)

## Movies and subtitles

The Windows HD movie path can replace the intro, ending and logo with **4× upscaled video generated from your own ISO**. This is separate from texture upscaling, and a fresh install without a movie pack plays the original video.

The original game already has gameplay-dialogue subtitles. The new narration overlay adds support for the intro and ending, where the PS2 release had no subtitle text. It needs local language-specific SRT files; these are not distributed with the project. See the [extras guide](extras.md) for setup and the distinction between dialogue and narration subtitles.

![A dragon illustration in the locally upscaled intro movie](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-movie.jpg)

![Intro narration with the added subtitle overlay](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/narration-subtitles.jpg)

## Prefer the original style?

Select **30 FPS**, use the **Original** inventory and menu options, choose **Classic** bow aiming, and disable dual wielding and optional graphics effects. Set `map_view = 0` and `inventory_strip = 0` in `drakan_input.ini` to use normal inventory pages for those shortcuts. This is an original-style configuration, not a claim of pixel-identical PS2 emulation.

## Implementation references

The feature descriptions above are grounded in the [settings schema](../../runtime/settings/src/Schema.cpp), [default input configuration](../../runtime/input/src/Config.cpp), [save implementation notes](../reference/save-slots.md), [HD movie implementation](../reference/fmv.md), [subtitle implementation](../reference/subtitles.md) and [platform reference](../reference/portability.md). Settings preserved from an older build can differ from fresh defaults.
