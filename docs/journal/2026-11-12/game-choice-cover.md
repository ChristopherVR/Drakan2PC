# Original "Choose your game": the sharp title picture behind The Ancients' Gates

Report: in the Original menu's Switch Campaigns / Choose your game list, The Ancients' Gates has a blurry background
and not the upscaled picture the menus show.

Scratch (not committed): `build/review/menu-cover/` (`evidence/` comparison pictures and frame sheets, `shots/` and
`runs/` the raw captures and logs, `drive.py` / `sheet.py` / `plan_*.txt` the scripts that made them). Play folder: a
copy of the owner's (`title_menu = native`, `game = ask`, `renderer = d3d11`, 3840 x 2160 borderless).

## What was blurry and what is sharp

- Blurry: `previews/background-d2.png`, which the build copies from the tracked `scripts/buildtool/kit/art/d2-cover.png`
  (1280 x 720). That cover is the disc's title art (768 x 432 crop of the six 256 x 256 tiles) scaled up with a
  bilinear filter, so at 3840 x 2160 every source pixel covers five screen pixels. The Modern game card shows the same
  file in a box about 1590 pixels wide, where it looks fine.
- Sharp: the picture both title menus show in a wide window, the texture pack's wide title art
  `<texture_dir>/wide/screen8_16x9.png` (1821 x 1024; also `_7x3` and `_32x9`). It lives in the player's `textures/`
  folder, is not tracked and is not made by the build (widescreen.md "Wide menu backdrops"). The renderer draws it
  behind the native title; it is used whether `hd_textures` is on or off.
- The Order of the Flame entry already used its own 7680 x 4800 `background-ootf.png` through the menu picture path
  (`mapPreviewId`, a texture of the overlay renderer, centre-cropped to fill the view).

Commit 198574b5 gave the disc game a background because the native art is hidden while the list is up (it names The
Ancients' Gates and must not show behind another game); it took the cover since that was the file at hand.

## Change

- `drakan_settings_host.cpp`: for the disc game the list's background is the wide title picture
  (`titleArtPictureId`: the `screen8` file picked for the view's aspect by the renderer's rule, registered as a menu
  picture like any other). `background-d2.png` remains the fallback when the pack has no such file or it does not
  decode. Rules and tests: `drakan_title_picture_policy.h`, `title_picture_policy_tests.cpp`.
- Same draw path as the Order of the Flame entry (centre crop to fill, 110 / 255 black veil under the text), so both
  entries behave alike and nothing is renderer specific: the picture is an overlay texture on Direct3D 11 and WebGPU.
- `TitleScreen.cpp`: the chosen game's picture that stays up for 0.25 s no longer has an opaque black layer under it.
  While the host faded the list out, that layer darkened the title art below (first capture: one presented frame at
  86 % brightness between the held picture and the title). Now it is a plain cross-fade. Test: the new block in
  `testTitleClassicGameChoice`.
- No new files to ship and no tracked binary: the picture is the player's own.

## Checks (12 November, machine shared with five other game instances, GPU at 99 %)

- Before / after at 3840 x 2160 and in a 1280 x 960 window, both entries, Direct3D 11:
  `evidence/before_after_4k_{d2,ootf}.jpg`, `before_after_43_{d2,ootf}.jpg`, 1:1 crop
  `crop_1to1_before_after_4k_d2.jpg`. The Order of the Flame entry is unchanged.
- `renderer = vulkan` (log: `NVIDIA GeForce RTX 5080 via Vulkan`): `before_after_vk_4k_*.jpg`,
  `after_vk_43__choice_d2.png`.
- Consecutive presented frames (the renderer's `shot`, one per present, 15 to 30 ms apart here), 1280 x 720 window:
  Up, Down and Enter on Direct3D 11 (`seq_d2_720_*.jpg`) and Vulkan (`seq_vk_d2_720_*.jpg`), Enter on Order of the
  Flame (`seq_ootf_720_enter.jpg`), 4:3 window (`seq_d2_43_*.jpg`), and choosing The Ancients' Gates with the pad while
  Order of the Flame was the running game (`click_720_pad.jpg`). Each sheet goes from one state to the next with no
  black frame, no frame of the other game's art and no frame of the old cover.
- Input: hover and click on a list row (scripted pointer), a real left click on the Switch Campaigns button (OS
  pointer on the focused window; log `Switch Campaigns pressed (click)`), Up / Down keys, pad Cross:
  `click_720_steps.jpg`.
- Fallback: `GSR_TEXDIR` at an empty folder, and at a folder whose `wide/screen8_16x9.png` is 5000 random bytes: both
  show the cover as before (`fb_missing__choice_d2.png`, `fb_corrupt__choice_d2.png`).
- Modern game card: the same before and after (mean difference 0.04 of 255 over the frame).
- Text: the heading now sits under the logo instead of on its plaque; rows read over the dimmed clouds.

## Limits

- In a 4:3 window the title menu itself draws the pack's replaced tiles, not the wide image; the list shows the centre
  of the 16:9 image. The two are the same art but sit a few pixels apart, so confirming The Ancients' Gates at 4:3
  shifts the picture slightly (`hold_vs_title_43.jpg`). At 16:9 the held picture and the title differ by about one
  level of 255.
- A player without a texture pack sees the cover as before, still soft. A sharper tracked cover would need a new
  picture; none was added.
- A `previews/background-d2.png` put there by the player no longer wins over the pack's title art (the build overwrites
  that file on every build anyway).
- Not run: 21:9 and 32:9 windows (the `_7x3` / `_32x9` choice is covered by the unit test only), the USA disc, Linux,
  the browser, a physical pad, the first frames of the list at start-up with a cold disk cache (every picture of the
  screen is black until decoded, as before).
- The frame sheets were taken at 30 to 60 presents per second because each frame is written as a PNG; a real session
  presents faster. The 0.25 s hold and the choice path themselves are as in 198574b5.
