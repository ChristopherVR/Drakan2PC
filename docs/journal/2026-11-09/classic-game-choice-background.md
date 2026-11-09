# Classic game choice: Drakan 2 background and the title art flash

- `title_cover.py --background-out` writes `previews/background-d2.png` (the 16:9 title art crop, same picture as the
  `game-d2.png` cover); `Build-LocalGame.ps1` places it in `dist/DrakanPC/previews`. `titlePictureOf` skips the disc
  game, so the Drakan 2 title keeps its own native art. The modern chooser stays black.
- Flash after choosing Order of the Flame (classic menu): at 60 fps one frame of the native Ancients' Gates art showed
  between the chooser and the Order of the Flame art. Cause: the overlay thread closes the chooser in the frame of the
  choice, the EE applies the choice later, and the last guest frame had been rendered with the disc art. Fix: the
  choice binds the picture on the overlay thread (`previewTitleBackdrop`, followed by `syncTitleBackdrop` until the game
  index catches up) and `TitleScreen` keeps the chosen game's background up for 0.25 s (`holdGame_`).
- Verified with a 60 fps desktop capture across the choice (evidence in `build/review/d2bg/`): no frame of the disc art
  before the Order of the Flame art; choosing Drakan 2 goes from the chooser background into the native title.
