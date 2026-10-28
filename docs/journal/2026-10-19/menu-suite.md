# Shared menu presentation — 19 October 2026

The title-menu improvements now extend to all six scenic menu backdrops:
Surdana fields, snowy mountains, the vaulted interior, the mountain lake,
the snow village and the stone fortress. Each scene is restored as one image,
then packaged into its six original 1024×1024 texture replacements. Tile order,
bottom-up storage and each original constant alpha value are retained. The
private artwork and generation records live in `build/menu-suite-20261019/`;
they are excluded from the public source patch.

The renderer recognizes the exact 42 immutable title/scenic tile hashes.
These images close a stale startup world phase and use the same centered
native canvas as the menu controls. Their replacement images remain available
when a streaming upload reuses a previously animated VRAM slot. Unrecognized
effect textures retain the existing volatility protection. Wide gameplay
captures behind Pause, Save and Load retain their full width. Inventory keeps
its existing left anchor and live character preview.

Selected native menu labels now draw directly from the same HD font atlas and
native metrics as unselected labels. A gold gradient indicates focus, replacing
the retail 128px fire-text intermediate. The wrapper applies only to active
widgets on title, options, controls, audio, pause, save/load and alert screens;
it restores colors and the focus flag after drawing. Existing checked boxes,
sliders, hitboxes, input navigation and translated text remain native.

Validation includes title-side Options/Controls/Audio captures, in-game Pause,
Save, Options and confirmation captures, inventory section navigation and its
help dialogs, plus reopening the prior high-resolution preview companions.
The full WARP renderer suite passed 246 checks. The final renderer also passed
82 high-scale loading/font checks across 4:3, 16:9 and 21:9. Existing menu and
inventory coordinate tests passed. The private harness uses copied cards and
settings. Runtime image alpha, original artwork hashes, installation backup,
and executable hashes are recorded with the build evidence.

Coverage is shared menu presentation, not a playthrough of every shop,
quest-specific dialog or late-game screen. Credits and movies retain their
existing dedicated aspect policy. New-save preview creation and the centered
Loading group from the preceding menu update remain in place.
