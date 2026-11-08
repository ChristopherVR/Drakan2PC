# Game card translation

- The card heading, hint, Playing and More above/below labels were already in the menu catalog. The two built-in
  descriptions ("The game on your disc." and the Order of the Flame line from `drakan_game.cpp games()`) were not.
- `menuTextOrSelf` (gameui/MenuText.h) translates catalog text and returns anything else unchanged, so a pack's
  authored `mod.toml` description stays as written. `drawGameCard` uses it for the description.
- Game names stay as proper names (also in the footer game line).
- Tests: `gameui_locale_test` checks every card key in all five languages (translated, not the English key) and renders
  the card in each language, with a pack description staying verbatim.
- Screenshot (German, game = ask): build/harness/loc18/card_de.png, exe copy in build/loc18/play.
