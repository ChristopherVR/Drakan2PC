# Play in your browser

The browser player opens a **browser build you created locally** plus your own ISO. The website is a launcher; it does not provide a prebuilt game or upload the files you choose.

![Rynn in Surdana in the browser player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/browser-gameplay.jpg)

## What you need

- A current desktop Chrome or Edge browser with local folder access, WebAssembly threads and SIMD support.
- Your locally built `DrakanPC-web` folder, containing `index.html`, `drakan.js` and `drakan.wasm`.
- The same supported PAL ISO used to build the game.
- Optionally, a locally generated HD texture folder.

Mobile browsers and browsers without the required folder-access API cannot use the hosted launcher. The launcher will show a compatibility message.

## Create the browser build

On Windows, select **browser version** in the installer; this requires WSL with Ubuntu. From a source checkout, use the existing build pipeline:

```powershell
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "D:/MyDiscs/Drakan.iso" -TargetPlatform web -WorkDir build/final-game
```

Use `-Incremental` when rebuilding an already prepared work directory. Build one target at a time. Linux instructions and additional requirements are in [Build from source](../../BUILDING.md#linux-and-browser-iso-builds).

## Open your local files

1. Open the [Browser player](../../runtime/web/pages/index.html) on the project website.
2. Choose the `DrakanPC-web` build folder, usually under `dist` in your installation.
3. Choose your ISO. Optionally choose an HD texture folder.
4. Press **Play** and allow read access if the browser asks again.
5. Follow the game's initial click prompt to enable audio and mouse interaction.

The website remembers file handles in this browser profile. Access can be revoked by browser settings or after moving files; reselect the folder and ISO if needed.

## Play through the local launcher

You can also run `Play-Browser.cmd` in `dist/DrakanPC-web` on Windows, or `Play-Browser.sh` on Linux. Use that launcher rather than opening the built HTML as a `file://` URL: the threaded build needs the HTTP headers supplied by the launcher.

## Saves belong to the browser profile and site

Browser saves and settings live in IndexedDB. Keep the **same browser profile and origin** when returning to them. The GitHub Pages player and a localhost player have separate storage; switching between them does not transfer saves. Clearing site data or using a temporary/private profile can remove progress.

When a storage error appears, use **Retry** and resolve storage access before closing the page. See [Saves & updates](saves.md).

## Feature differences

The browser uses the software renderer. Modern menus and PC Settings work; DLSS, ray tracing and the modern Tab inventory are Windows only. See [Platforms & portability](../reference/portability.md).
