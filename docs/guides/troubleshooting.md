# Troubleshooting

Start with the symptom below. For a report, include your platform, build revision, reproduction steps and relevant error text.

## The builder rejects my disc

The **PAL SCES-500.06** release in plain ISO format is validated; the **USA SCUS-971.28** release is accepted as an experimental build with fewer features ([USA disc support](install.md#usa-disc-support-experimental)). If the game says "wrong disc for this build", `drakan.ini` points at the other release's image: build again from that disc or point it back. Other releases, a modified image or BIN/CUE are not interchangeable with them. The extractor checks the disc and executable identity. Use your own supported disc image; do not bypass the check.

## The build looks stuck

The first build can take one to two hours. Individual generated files can compile for about ten minutes without new console output. Check that the compiler is still running before interrupting it.

If the build exits, use **Rebuild Drakan** to continue. Close memory-heavy applications if compilation runs out of memory. The guided builder already reduces concurrency on a failed retry. Source builds should reuse the same WorkDir with `-Incremental`; reduce `-Jobs` and compiler concurrency if memory is exhausted.

For the guided installer build, inspect `build-log.txt` and `build/game-output.log` inside the installation. The first contains the console log; the second holds detailed tool output.

## The game cannot find files or has different settings

Launch from `dist/DrakanPC` or the installed shortcut, not the bare build output directory. Confirm that the ISO still exists at the configured path in `drakan.ini`. A bare build executable may lack the deployment's settings and optional packs.

## The mouse is stuck or does not look around

Press **F10** to release or recapture it. After returning from another window, click once inside the game to restore capture, then move or click again to play. The first recapture click intentionally does not attack. In browsers, permit pointer lock when requested and use the game's initial activation gesture.

## Performance is lower than expected

Lower the internal render scale and turn off ray-traced effects first. Compare 30, 60 and Uncapped world modes (restart after changing), and check VSync and the frame limiter.

<!-- media:pc-settings-windows -->

Turn on the FPS counter with **F2** to see the world rate and the presented frame rate while you change settings.

<!-- media:fps-counter -->

Linux and browser currently use the software GS path. They do not have the same performance or feature coverage as the Windows D3D11 renderer. See [Platforms & portability](../reference/portability.md).

## HD movies or narration subtitles are missing

A normal build does not supply HD movies or narration transcripts. Confirm that you generated the movie pack or provided the language-specific local SRT files. Check `hd_movies`, Narration subtitles and the game's Show Subtitles setting. See [HD textures, video & subtitles](extras.md).

## The browser cannot choose a folder

Use a current desktop Chrome or Edge browser. If the required folder-access API is missing, the launcher disables the picker controls and displays a message. Alternatively, run `Play-Browser.cmd` or `Play-Browser.sh` from your built browser folder. Do not open its `index.html` directly from disk.

<!-- media:browser-unsupported -->

## The browser build folder is rejected

Choose the browser output folder containing **all three** of `index.html`, `drakan.js` and `drakan.wasm`, not the Windows build folder, the repository root or its parent `dist` folder.

<!-- media:browser-wrong-folder -->

If access was revoked or files were moved, choose the folder and ISO again. This is preferable to clearing site data, which can remove browser saves.

## Browser saves are missing or storage fails

Return to the same browser profile and site you used to save. Localhost and GitHub Pages storage are separate. Private sessions and cleared site data may lose progress. If the game displays a storage error, retry after resolving browser storage restrictions. Read [Saves & updates](saves.md) before changing storage settings.

<!-- media:browser-title -->

## Report a reproducible problem

Use [GitHub Issues](https://github.com/ChristopherVR/Drakan2PC/issues). Include:

- Your operating system, CPU, GPU, RAM and browser/version if applicable.
- Your release or commit and whether you used the installer or a source build.
- Exact steps, the expected result and what happened instead.
- Relevant error messages, settings and whether the problem remains with optional effects disabled.

Review logs before sharing personal paths. Do not attach an ISO, ELF, generated game code, built game, saves or extracted assets. Those are not public repository material.
