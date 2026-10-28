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
| DLSS Super Resolution / DLAA | Reconstruction or anti-aliasing | Compatible NVIDIA RTX GPU and a DLSS-enabled Windows build |
| Ray-traced water and shadows | Optional rendering effects | Compatible DirectX Raytracing hardware and a build with DXR support |
| HD movies | Replace selected FMVs with locally upscaled video | Local movie pack and `hd_movies` enabled |

Every feature can be turned off. An option marked unavailable needs hardware or a build component you do not have.

See [HD textures, video & subtitles](extras.md) for local packs, and the [DLSS reference](../reference/dlss.md) for current integration status and historical notes.

## Gameplay and interface choices

Under Gameplay, select the **Modern** or **Original** inventory, title, pause and save/load screens where supported. The Tab inventory setting applies the next time that screen opens. Dedicated I inventory-strip and M map views are configured independently.

Dual wielding is optional. Modern bow aiming belongs to the mouse settings. See [Controls & combat](controls.md) for both.

## Sound, language and subtitles

PC Settings includes master volume and output-device selection where available. Language selects the matching text and voice tree from your disc and may require restarting.

Gameplay dialogue uses the game's **Show Subtitles** option. **Narration subtitles** controls the added intro/outro SRT overlay, with an independent text-size setting. No narration text appears unless matching local SRT files are installed.

## Original-style setup

Choose 30 FPS, 4:3, original menus and inventory, Classic bow aiming and no dual wielding, and turn off HD textures, upscaling, DLSS and ray tracing.
