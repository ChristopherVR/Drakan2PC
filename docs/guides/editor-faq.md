# Editor & mod FAQ

Answers for the level editor, the Save Game Editor and mod packs. Problems with building or running the game itself are in [Troubleshooting](troubleshooting.md).

## Do I need the disc to use the editor?

Yes, for almost everything. The editor contains no game data: levels, models, textures and text are read from your own disc image while you work. Without one you can look around, read the help guides and open Project Settings, and every task asks for the disc image first. The Save Game Editor works without a disc, but shows item names, pictures and quest text only when you give it one.

## Does the editor change my disc image or my saves?

No. The disc image is only read. Your work goes into a mod project and the packs you export. Play testing from the desktop app uses a separate test memory card, so your own saves are untouched. The Save Game Editor writes a save only when you press **Save**, and keeps a `.bak` copy beside it.

## Which disc do I need?

The same disc you built the game from. Packs record the disc they were made for, and the game skips a pack made from another region (the Mods page says why).

## Play does nothing, or asks for a folder

Play needs the desktop app and the folder that contains `drakan.exe` (`dist/DrakanPC` inside the game installation). Set it with **File > Game (Play) Folder**. In a browser version of the editor, export the pack and copy it into the game's `mods` folder yourself ([Install and share mods](mods.md)).

## My change does not show up in the game

- Check the game's **Mods** page (F1): the pack must be **Active**. If it says "Enabled, loads after restart", restart the game.
- If it is **Skipped**, the reason is shown: usually a different disc region or a missing pack it depends on.
- If two packs change the same thing, the one lower in the list wins. Move yours down with **Load position**.
- Saves remember the state of a level. Changes to creatures and items are easiest to see from a **New Game**, or with the editor's **Play** button, which starts the level fresh.

## I fall out of the world after taking an exit

The destination level has no matching exit leading back, so the game has nowhere to place you. Open the exit in the **Logic** panel: it lists where the exit leads and warns when the way back is missing. The **Campaign map** in Project Settings shows these exits in orange.

## Export says there are errors

Open **Validation** at the bottom of the Level Editor and fix every error it lists (a trigger linked to nothing, an exit to a missing level, a map with fewer than two spawn points). Warnings are advice and do not block the export.

## Where is my project?

In the desktop app, in the folder you chose when you created it (**File > Open Recent Project**). In a browser, in that browser's storage: use **Save project** regularly to keep a file copy, because clearing the browser's site data deletes it.

## Can I add new voice lines, music or models?

New dialogue lines can use any recording already on the disc, with your own subtitle. New recordings and music cannot be added yet, and the editor cannot import new models. Packs written by hand can go a little further; see the [Mod packs reference](../reference/modding.md).

## The Save Game Editor cannot save in place

Some browsers cannot write files directly. **Save** then downloads the edited file instead; copy it over the original save (keep a backup first). The desktop Save Game Editor always saves in place.

