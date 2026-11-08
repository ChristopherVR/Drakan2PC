# Original-style Choose your game

With `title_menu = native` and `game = ask` plus an installed campaign pack, the game's own title menu has no game
selector, so the choice was never offered (commit 18a57fd3 made an explicit native setting win over the modern title).

Now the glue (`drakan_menu_screens.inl`, `titleTick`) binds the hosted title in a classic mode when the title style is
Original and `drakan_game::offerChoice()` is true. The native title page stays hidden (art only) while
`gameui::TitleScreen` draws the game list with `TitleState::classicGameChoice`: the heading, one plain centred text row
per game (focused one in the heading colour with `> <` markers), the footer hints, over the native art dimmed a little.
No panel, cover, description or "Playing" mark. Input: Up/Down/wheel, Enter/click, Esc/right click (keeps the current
game; emits SelectGame for it so the host hands over), same UiSound feedback as the modern card.

The pick goes through the same `SelectGame` title action as the modern card (`drakan_game::select`, Load Game enable
refresh); in classic mode the glue then finishes the action and calls `titleFallBack`, which restores the native title
page. A new visit to the title asks again, like the modern card. Strings are the modern card's ("Choose your game",
"More above", "More below"), so no new translations. Test: `testTitleClassicGameChoice` (gameui_saveload_test).

## Background (follow-up)

The native art is the Ancients' Gates picture, so it is hidden while the list is up (`HiddenDraw` without the art widget
kept, drakan_menu_screens.inl `menuScreenDraw`); the host draws black, or the focused game's
`previews/background-<id>.png` (`TitleState::Game::background`, same lookup as the covers) dimmed. The native title shows
again once a game is picked. Tests: `testTitleClassicGameChoice` (background follows the focus).

Order of the Flame background: not generated. The Drakan 1 install's `Dragon.rrc` was decoded (type 0x40 records: 4-byte
w, h, 54-byte header, zlib, 16-bit pixels, 83 pictures of 256 x 256 plus smaller ones): they are the parchment level maps,
objective text sheets, HUD / pack frames, the Surreal and a publisher logo. No title, menu or loading background exists
in the install (the title art is video), so the converter emits nothing. Until a source exists the player's own
`previews/background-ootf.png` (or the supplied cover as that file) serves; a pack may ship `background-ootf.png`.
