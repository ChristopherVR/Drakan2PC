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
