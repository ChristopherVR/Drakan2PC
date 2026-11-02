# Install and share mods

A **mod pack** is a small folder or `.zip` that the PC version loads on top of your disc: changed levels, new level copies, shop stock, story text and cut scenes, multiplayer maps or menu styles. A pack never contains the game itself and never changes your disc image; remove it and the original game is back.

## Install a mod someone gave you

1. Close the game.
2. Open your play folder (`dist/DrakanPC` inside the installation) and its `mods` folder. Create `mods` if it does not exist yet.
3. Copy the pack into it: the `.zip` as it is, or the unzipped folder. Do not unzip a pack into the `mods` folder itself; each pack is one zip or one folder.
4. Start the game, press **F1** (PC Settings) and open the **Mods** page. New packs are listed but switched off.
5. Turn the pack **Enabled**, then choose **Restart game now**. Packs load when the game starts, so changes always apply after a restart.

The Mods page shows each pack's name, version, author and status:

| Status | Meaning |
| --- | --- |
| Active | Loaded and in use. |
| Disabled | Installed but switched off. |
| Enabled, loads after restart | Restart the game to use it. |
| Skipped: ... | Not loaded, with the reason: made for another disc region, a missing pack it depends on, made by a newer editor, or a damaged pack. |

When two packs change the same thing, the one lower in the list wins. **Load position** moves a pack up or down, and the page lists which packs override each other.

## Play a mod

Most packs change something you meet in the campaign: start a **New Game** or load a save and go to the level the mod describes. Multiplayer maps appear in **Multiplayer > Host Game** under the level list (marked "(custom)"); see [Multiplayer](multiplayer.md).

## Make and share your own

1. Make your changes in the [Level & mod editor](editor.md).
2. Fix every error in **Validation**, then press **Export** (Ctrl+E). Set the mod's name, id, version and author in the dialog.
3. Tick the box that also installs the pack into the game's mods folder and turns it on to try it yourself, or just write the `.zip` and share that file.
4. In the desktop app **Play** (F5) does all of this in one step and starts the game straight in your level, using a test memory card so your own saves are not touched.

A pack only works with the disc it was made for: packs made from the PAL disc need the PAL disc. Tell players which disc you used. Share what you made, and do not add files extracted from the game (textures, sounds, movies) to a pack by hand.

Technical details of the pack format, `mods.ini` and the merge rules: [Mod packs reference](../reference/modding.md).
