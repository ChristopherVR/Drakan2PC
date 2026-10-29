# Saves & updates

Save files are part of your local game data. Back them up before moving to another installation or making manual configuration changes.

![Modern save browser with a Surdana save preview](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/save-browser.jpg)

## Windows and Linux save locations

On Windows, saves normally live under `dist/DrakanPC/mc0`; a second virtual card may use `mc1`. On Linux, card folders sit beside the deployed executable in `dist/DrakanPC-linux`. Custom launch overrides can change the location.

With the game closed, copy the whole `mc0` folder, any `mc1` folder and your `*.ini` configuration files to a separate backup location. Restore the folders to the corresponding playable directory while the game is closed.

## Quicksave and quickload

**F5** quicksaves during controllable gameplay. **F8** quickloads during gameplay or from the title. These actions can be rebound.

- Quicksave uses the reserved `mc0/BESCES-50006Slot33` slot, outside the manual save slots (PAL only; the experimental USA build saves only through the game's own save slots, as `BASCUS-97128…`, and does not see PAL saves).
- A new quicksave replaces the previous quicksave without an overwrite prompt.
- Quickload discards unsaved progress without a confirmation.
- If no quicksave exists, quickload does not fall back to a manual save.
- The title's Continue action selects the newest manual save; F8 selects the quicksave.
- Quickload from a death dialog is not currently supported.

<!-- media:quicksave -->

Use manual saves as well, particularly before experimenting with settings. Where save paging is available, PgUp/PgDn or controller L1/R1 move between pages. See the [save reference](../reference/save-slots.md).

<!-- media:title-continue -->

<!-- media:load-game -->

## Browser saves

The browser build stores its virtual filesystem in **IndexedDB**, tied to the browser profile and origin. A hosted GitHub Pages game, a localhost game and a different browser profile do not automatically share saves.

Keep using the same profile and site. Do not clear site storage to troubleshoot a launcher problem unless you intend to remove that profile's stored progress. Private browsing is unsuitable for persistent saves. When the game reports a save-storage error, use **Retry** and resolve it before leaving the page.

The browser player currently has no separate save export/import interface. Do not assume a desktop folder backup includes browser saves. File selections and saved games are different pieces of browser storage.

## Rebuild or update

For installer users, **Rebuild Drakan** resumes compilation from the installed source snapshot. It is a rebuild command, not a guarantee that it downloads a newer release; obtain a newer setup or updated source when changing versions.

For source users, rerun `Build-LocalGame.ps1` with **the same WorkDir** and `-Incremental`. The existing development directory is `build/final-game`:

```powershell
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "D:/MyDiscs/Drakan.iso" -WorkDir build/final-game -Incremental -Jobs 8
```

The builder refreshes the playable folder and keeps card folders, INI settings and supported optional pack directories, including textures, movies and subtitles. Do not manually copy executable files into `dist`; let the builder stage a consistent installation.

## Moving the game or ISO

The game still reads its ISO during play. A moved or renamed ISO can leave an old path in `drakan.ini`. Update the configuration to the new file location or restore the ISO to its previous path. Keep your backup until you have launched the game and loaded a save successfully.
