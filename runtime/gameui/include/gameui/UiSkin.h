// Data-driven menus and HUD: a "UI skin" is a small INI file (docs/reference/modding.md, "Menus and HUD") that
// overrides the strings, the entry order / visibility of the menu pages, the theme colours and sizes, a few layout
// numbers and the HUD accent. Nothing in it is code: another engine (Unreal UMG, ...) can read the same file and the
// same stable ids. The defaults compiled into the screens equal the pre-skin look; an empty skin changes nothing.
//
// Sources, applied in this order (later keys win): `ui.ini` next to the exe, then `ui/*.ini` of every enabled mod pack
// in load order. The host installs the merged result once at startup with setUiSkin(); screens read it when they are
// constructed (Theme) or drawn (strings, rows, layout). Std only, platform free.
//
//   [theme]    name = #RRGGBB or #RRGGBBAA (colours: see themeColorNames()), size_<name> = design units, accent = #...
//              (accent sets ember and a matching soft glow; the individual keys still win)
//   [hud]      accent = #RRGGBB          (Theme::hudAccent: respawn count, chat names, dragon bar, results highlight)
//   [layout]   title_column_w, title_row_h, title_row_step, title_centre_y, pause_menu_x, pause_menu_w,
//              pause_panel_y, pause_row_h, pause_row_step (design units on the 1920 x 1080 canvas)
//   [rows]     <page> = id, id, ...   pages: title.main, pause.root, pause.options, pause.audio, pause.controller.
//              Listed ids are shown in that order, ids left out are hidden, unknown ids and rows the game has no
//              use for right now (Continue without a save) are skipped; an empty result falls back to the default.
//   [strings]  <key> = text           keys: title.row.<id>, title.desc.<id>, title.quit.title, title.quit.body,
//              pause.row.<id>, ... (rowIds() lists the ids; stringKeys() every key with its default)
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gameui/Draw.h"

namespace gameui {

struct Theme;

struct UiSkin {
    std::map<std::string, std::string> strings;
    std::map<std::string, std::vector<std::string>> rows;
    std::map<std::string, Color> colors;  // theme colour name -> colour ("hud_accent" for [hud] accent)
    std::map<std::string, float> numbers; // "size_body", "title_row_h", ...
    bool empty() const { return strings.empty() && rows.empty() && colors.empty() && numbers.empty(); }
};

// Merges `text` into `into` (keys of later parses replace earlier ones). Returns the number of keys accepted; problems
// (unknown section / key, bad colour or number) are appended to `warnings` and skipped.
size_t parseUiSkin(std::string_view text, UiSkin& into, std::vector<std::string>* warnings = nullptr);
// The skin as INI text (sorted, round-trips through parseUiSkin).
std::string writeUiSkin(const UiSkin& skin);
// "#rrggbb" / "#rrggbbaa" (also without '#'), the reverse.
bool parseSkinColor(std::string_view s, Color& out);
std::string formatSkinColor(Color c);

// The active skin. setUiSkin() is meant to be called once at startup, before the screens are built; a replaced skin is
// kept alive (never freed) so references returned by activeUiSkin() stay valid.
void setUiSkin(UiSkin skin);
const UiSkin& activeUiSkin();

// Skin string for `key`, else `def`. The pointer lives as long as the process.
const char* skinText(const std::string& key, const char* def);
float skinNumber(const char* key, float def);
// Reorders / filters `defaults` by the skin's [rows] list for `page` (see the header comment); `idOf(row)` gives the
// row's stable id.
template <class Row, class IdFn>
std::vector<Row> applyRowOrder(const char* page, std::vector<Row> defaults, IdFn idOf);
const std::vector<std::string>* skinRows(const char* page);

// Applies `skin` to `t` (the Theme constructor calls it with the active skin).
void applyUiSkin(Theme& t, const UiSkin& skin);

// Names for tools: theme colours (with their defaults), the numbers, row pages with their default ids.
struct SkinKeyInfo {
    std::string key;
    std::string def; // default as written in an INI
};
std::vector<SkinKeyInfo> themeColorNames();
std::vector<SkinKeyInfo> themeSizeNames();
std::vector<SkinKeyInfo> layoutNames();
// Every overridable string with its default text and every row page with its default ids ("title.main" ->
// "continue, load_game, ..."). Rows that are only shown in some situations are included.
std::vector<SkinKeyInfo> stringKeys();
std::vector<SkinKeyInfo> rowPages();

template <class Row, class IdFn>
std::vector<Row> applyRowOrder(const char* page, std::vector<Row> defaults, IdFn idOf) {
    const std::vector<std::string>* want = skinRows(page);
    if (!want)
        return defaults;
    std::vector<Row> out;
    std::vector<bool> used(defaults.size(), false);
    for (const std::string& id : *want)
        for (size_t i = 0; i < defaults.size(); ++i)
            if (!used[i] && id == idOf(defaults[i])) {
                used[i] = true;
                out.push_back(defaults[i]);
                break;
            }
    return out.empty() ? defaults : out;
}

} // namespace gameui
