# Level & mod editor

The Drakan editor lets you change the game you own: move creatures and treasure, build puzzles, change what shops sell, rewrite dialogue and cut scenes, string levels into a campaign of your own and turn a level into a multiplayer arena. Everything you make becomes a **mod pack** that the PC version loads on top of your disc. Your disc image is only read, never changed.

<!-- media:editor-start -->

## What you need

- Your own **Drakan: The Ancients' Gates** disc image (`.iso`), the same one you built the game from. The editor reads levels, models and text straight from it; nothing is uploaded and nothing from the game ships with the editor.
- Windows 10 or 11 for the desktop app. It can start the game for you to play-test, so install the PC version first ([Install on Windows](install.md)).
- A mouse with a right button and a wheel. The camera and most tools are built around it.

## Install the editor

1. Open the [latest release](https://github.com/ChristopherVR/Drakan2PC/releases/latest) and download **DrakanEditor-Setup.exe** (or **DrakanEditor-Portable.zip** if you would rather not install anything).
2. Run the installer and start **Drakan Editor** from the Start menu. The Save Game Editor is a separate download on the same page (**DrakanSaveEditor-Setup.exe**); see [Save Game Editor](save-editor.md).
3. The apps have no automatic update check. When a new game release comes out, download the editor again from the same page.

If you build from source instead, the editor also runs in desktop Chrome or Edge: `cd editor`, `npm ci`, `npm run dev`, then open the address it prints. The browser version works the same way, except that it cannot start the game for you and saves projects as downloads.

## First run

1. Click **Open disc image (.iso)** on the start page, or drop the `.iso` onto the window. The desktop app remembers it and reopens it next time (**File > Open Recent**).
2. Under **What do you want to make?** pick a template (a campaign level, a multiplayer arena, a level copy, a puzzle, a shop, a cut scene, a quest and more). The start page explains what it involves; press its button to begin. **+ Add** at the left of the toolbar offers the same list at any time. The editor opens the right panel with sensible defaults and creates a **mod project** to hold your work.
3. A checklist in the bottom-right corner lists the next steps of your task and ticks them off as you go. The current step has a button that takes you to the right place.
4. When you want to try it, press **Play** (F5). The first time, the desktop app asks for your game folder: the folder that contains `drakan.exe` (for an installed game, `dist/DrakanPC` inside the installation folder). Change it later with **File > Game (Play) Folder**.

<!-- media:editor-add-menu -->

## A tour of the window

The layout follows familiar level editors such as Unreal Engine: a viewport in the middle, the scene on the right, assets at the bottom.

| Area | What it is for |
| --- | --- |
| **Top bar** | Menus (File, Edit, Window, Tools, Help), your project (click it for Project Settings, Save, Export and Play), and a tab for each open editor: Level Editor, Story, Menus, Vendors, Project Settings. |
| **Toolbar** | **+ Add**, the level picker, undo and redo, selection and arrange tools, then **Export** and **Play** on the right. |
| **Place** (left) | Ready-to-place things in plain groups (characters, creatures, items, triggers and logic, cut scenes, conversations, quests, vendors), plus **Recently placed**. Drag a row into the viewport, or double-click it to place it in the middle of the view. |
| **Viewport** | The open level in 3D. Right-drag and W A S D fly the camera, F frames the selection, W / E / R switch between move, rotate and scale. |
| **Outliner** (right, top) | Every object in the level, searchable. Click to select, H hides, L locks. |
| **Details** (right, bottom) | The fields of the selected object: health, speed, model, sounds, vendor stock. Changed fields are marked and can be reset. **World Settings**, in the tab beside it, holds the multiplayer arena settings. |
| **Bottom area** | **Content Browser** (every object class the level can use; drag one into the viewport), **Output Log**, **Validation** (problems to fix before you export) and **Logic** (how triggers, doors and quests are wired). |
| **Status bar** | Selection count, grid and snap settings, camera position. |

Story, Menus, Vendors and Project Settings open as tabs of their own from the **Window** menu. **Tools > Disc Files** is an advanced browser for the raw files on the disc; you never need it to make a mod.

## Find anything with Ctrl+P

**Ctrl+P** opens one search box for every command, object, class and level. Start with `>` for commands only, `@` for classes (press Enter to place one), `#` for objects in the open level and `:` for levels.

<!-- media:editor-palette -->

## Get help in the editor

The **Help** button in the top bar opens short task guides; the **?** on each panel opens the guide for that panel. The same guides are on this site as [Editor task guides](../reference/editor-guide.md), and **F1** (or **?**) lists every keyboard shortcut.

<!-- media:editor-help -->

## Projects

Your work lives in a **mod project**: its name, id, author and version, the levels it uses in campaign order, the level Play starts in, and your edits to each level. Open it with **Ctrl+,** or by clicking the project name in the top bar.

- **Desktop app:** a project is a folder (`project.drakan.json` plus a `mod/` folder with the exported pack). It saves itself as you work and with **Ctrl+S**. **File > Open Recent Project** brings it back.
- **Browser:** the project is kept in the browser's storage. **Save project** downloads it as a file and **Open project** reads it back; keep the downloaded file somewhere safe.

<!-- media:editor-project -->

## Common shortcuts

| Keys | Action |
| --- | --- |
| Ctrl+P | Search everything |
| F1 or ? | All keyboard shortcuts |
| W / E / R | Move, rotate, scale |
| Right mouse + W A S D | Fly the camera |
| F | Frame the selection |
| End | Drop the selection onto the ground |
| Ctrl+D, Delete | Duplicate, delete |
| Ctrl+Z, Ctrl+Y | Undo, redo (Ctrl+Shift+H lists the history) |
| H, L | Hide, lock |
| Ctrl+Shift+K | Logic panel |
| Ctrl+Shift+Y | Story editor |
| Ctrl+, | Project Settings |
| Ctrl+E | Export the mod pack |
| F5 | Play test (desktop app) |

## Where to go next

- [Editor task guides](../reference/editor-guide.md): step-by-step guides for placing objects, wiring triggers, shops, campaigns, multiplayer maps, story and cut scenes, packaging and saves.
- [Install and share mods](mods.md): what a mod pack is and how players turn one on.
- [Multiplayer](multiplayer.md): host and join games, and play your own arenas.
- [Editor & mod FAQ](editor-faq.md): answers to common problems.
