# Graphics & PC settings

Press **F1** for PC Settings. Some options depend on your platform and GPU; Windows has the most.

![PC Settings categories in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg)

## Apply changes

Use **Apply** or **OK** to keep changes. **Cancel**, F1 or Esc discard uncommitted edits. Display changes can prompt you to keep the new mode; if you cannot see it, allow the confirmation timeout to revert. A restart prompt identifies settings that cannot change during play.

For manual editing on Windows, close the game and edit `drakan.ini` or `drakan_input.ini` in `dist/DrakanPC`. Existing settings survive an incremental rebuild.

## Frame rate: 30, 60 or uncapped

**World frame rate** selects the game's world-update mode. Choose **30** for the original cadence, **60** for up to 60 FPS, or **Uncapped** to allow higher frame rates. The change requires a restart. Actual performance depends on the scene, CPU, GPU and enabled effects.

**VSync** and the **presentation limiter** govern display pacing separately. Setting the presentation limiter to 60 does not by itself select the 60 FPS world mode.

The supplied Windows `Play-Original30.cmd` launcher is another way to run the world at its original cadence.

<!-- media:frame-rate -->

## Resolution, aspect ratio and fullscreen

Choose a window size, internal render scale and aspect ratio. Widescreen widens the 3D view; menus and movies stay 4:3.

Use F11 or Alt+Enter for borderless fullscreen on Windows. Increase render scale gradually: a larger scale increases GPU work and memory demand.

![Display settings in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/display-settings.jpg)

## Optional visual enhancements

| Feature | Purpose | Requirement / control |
| --- | --- | --- |
| Texture filtering and anisotropic filtering | Improve texture sampling | Windows renderer settings |
| Live texture upscaling | Scale decoded textures at runtime | Select the filter/factor; disable for native textures |
| HD texture replacement | Use locally generated replacement images | A texture pack and the HD textures setting |
| Terrain detail | Fine surface detail and large-scale variation on the ground and mountains, fixed in the world | Direct3D 11 renderer; `terrain_detail` (off by default) and `terrain_detail_strength` (0.25 to 2) in the Textures section; off shows the terrain as before |
| Cliff textures | Steep mountain faces take their texture from the side so it is no longer stretched into streaks; patches of grass and rock on such faces move | Direct3D 11 renderer; `terrain_cliffs`, off by default |
| Material detail | Shade the relief inside HD textures (stones, planks, rock) from the level's light; Off, Hard surfaces or All | The Direct3D 11 renderer, the HD texture pack and its material maps (made by the same build step) |
| DLSS Super Resolution / DLAA | Reconstruction or anti-aliasing | Compatible NVIDIA RTX GPU and a DLSS-enabled Windows build |
| Ray-traced water and shadows | Optional rendering effects | Compatible DirectX Raytracing hardware and a build with DXR support |
| Living foliage (experimental) | Grass, flowers, ferns and trees sway in the wind; grass bends away from you | PAL disc; `foliage_wind` in the Display page's Vegetation section, strength 0.25 to 2 |
| Character detail distance, object draw distance | Characters keep their detailed model further away; people, plants and small objects are drawn further out before they fade | PAL disc; `lod_distance` (1 to 8) and `draw_distance` (1 to 4) in the Display page's Models section, 1 = original |
| HD movies | Replace selected FMVs with locally upscaled video | Local movie pack and `hd_movies` enabled |

Every feature can be turned off. An option marked unavailable needs hardware or a build component you do not have.

<!-- media:rt-compare -->

<!-- media:dlaa-compare -->

See [HD textures, video & subtitles](extras.md) for local packs, and the [DLSS reference](../reference/dlss.md) for current integration status and historical notes.

## Gameplay and interface choices

<!-- media:modern-inventory -->

Under Gameplay, select the **Modern** or **Original** inventory, title, pause and save/load screens where supported. The Tab inventory setting applies the next time that screen opens. Dedicated I inventory-strip and M map views are configured independently.

Dual wielding is optional. Modern bow aiming belongs to the mouse settings. See [Controls & combat](controls.md) for both.

Gameplay > Developer > **Developer debug menu** unlocks the game's own debug menu, left on the disc by its developers: press **F3** during play for cheats (invincible, fly, level up, spells, health, gold), teleport and render switches. Up/Down pick an option, Left/Right change it (hold Shift for 10, Ctrl for 100), Enter runs it, PgUp/PgDn turn the page, F3 or Esc closes it. Rynn stands still while it is open. On a controller, hold **L3 + R3** (click both sticks) for half a second to open or close it; the D-pad, Cross, Square/Circle and the shoulder buttons work inside. The key can be changed under Controls (Interface > Debug menu) and the controller shortcut under Controls > Gamepad. Save first: some options can break a game in progress.

## Sound, language and subtitles

PC Settings includes master volume and output-device selection where available. Language selects the matching text and voice tree from your disc and may require restarting.

Gameplay dialogue uses the game's **Show Subtitles** option. **Narration subtitles** controls the added intro/outro SRT overlay, with an independent text-size setting. No narration text appears unless matching local SRT files are installed.

## CPU cores

Gameplay > Performance > CPU cores sets how much of your processor the game may use (Automatic, or 4, 6, 8, 12 or 16 cores). Leave it on Automatic unless you have a reason to change it. It takes effect the next time you start the game. For an exact set, write a hex mask in `drakan.ini` (or the environment variable `DRAKAN_CPU_AFFINITY`):

```ini
[general]
cpu_affinity = 0xFFF   ; the first 12 logical processors (bit 0 = processor 0)
; cpu_affinity = cores:8 ; the first 8 allowed processors
; cpu_affinity = auto    ; let Windows choose (default)
```

A mask is intersected with the processors Windows allows the game to use; a mask with none of them is ignored and the log says so (`[drakan] cpu_affinity: ...`). Only the first processor group (64 logical processors) is addressed, and the setting does nothing outside Windows. To limit the processors used while building the game, use `-CpuAffinity` on the build script instead.

## Original-style setup

Choose 30 FPS, 4:3, original menus and inventory, Classic bow aiming and no dual wielding, and turn off HD textures, upscaling, DLSS and ray tracing.
