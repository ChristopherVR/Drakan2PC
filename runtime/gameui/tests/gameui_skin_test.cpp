// Unit tests for the UI skin (gameui/UiSkin.h): the INI parser, the round trip, the Theme / title / pause hooks, and
// that the shipped reference file configs/ui/default.ini equals the compiled-in defaults. Synthetic data only.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ScreenFixture.h"
#include "gameui/Layout.h"
#include "gameui/PauseScreen.h"
#include "gameui/TitleScreen.h"
#include "gameui/UiSkin.h"

using namespace gameui;
using gameui::test::ScreenWorld;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

const Viewport kVp{0, 0, 1920, 1080};

UiInput keys(std::initializer_list<UiKey> k) {
    UiInput in;
    in.keys = k;
    return in;
}

bool hasText(const DrawList& d, const std::string& s) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Text && c.text == s)
            return true;
    return false;
}

std::vector<std::string> titleLabels(const TitleScreen& t, const TitleState& st) {
    std::vector<std::string> v;
    for (TitleRow r : t.rows(st))
        v.push_back(titleRowLabel(r));
    return v;
}

std::string defaultSkinText() {
    std::string o =
        "; Reference copy of every overridable value with its compiled-in default (docs/reference/modding.md,\n"
        "; \"Menus and HUD\"). A mod ships only the lines it changes, in ui/<name>.ini. Regenerated and checked by\n"
        "; gameui_skin_test (DRAKAN_WRITE_DEFAULT_SKIN=1 rewrites it).\n\n[theme]\n";
    for (const SkinKeyInfo& k : themeColorNames())
        if (k.key != "hud_accent")
            o += k.key + " = " + k.def + "\n";
    for (const SkinKeyInfo& k : themeSizeNames())
        o += k.key + " = " + k.def + "\n";
    o += "\n[hud]\n";
    for (const SkinKeyInfo& k : themeColorNames())
        if (k.key == "hud_accent")
            o += "accent = " + k.def + "\n";
    o += "\n[layout]\n";
    for (const SkinKeyInfo& k : layoutNames())
        o += k.key + " = " + k.def + "\n";
    o += "\n[rows]\n";
    for (const SkinKeyInfo& k : rowPages())
        o += k.key + " = " + k.def + "\n";
    o += "\n[strings]\n";
    for (const SkinKeyInfo& k : stringKeys())
        o += k.key + " = " + k.def + "\n";
    return o;
}

void testParse() {
    UiSkin s;
    std::vector<std::string> w;
    const size_t n = parseUiSkin("; c\n[Theme]\naccent = #11AA33\nsize_body = 30\nbogus = 1\ntext = zz\n"
                                 "[hud]\naccent=#FF000080\n[layout]\ntitle_row_h = 70\ntitle_nope = 1\n"
                                 "[rows]\ntitle.main = new_game , quit,,\nnope.page = a\n"
                                 "[strings]\ntitle.row.new_game = Begin\\nAnew\n[wat]\nx = y\n",
                                 s, &w);
    CHECK(n == 6);
    CHECK(w.size() == 5); // bogus key, bad colour, unknown layout key, unknown row page, unknown section
    CHECK(s.colors.at("accent") == rgba(0x11, 0xAA, 0x33));
    CHECK(s.colors.at("hud_accent") == 0xFF000080u);
    CHECK(s.numbers.at("size_body") == 30 && s.numbers.at("title_row_h") == 70);
    CHECK((s.rows.at("title.main") == std::vector<std::string>{"new_game", "quit"}));
    CHECK(s.strings.at("title.row.new_game") == "Begin\nAnew");
    // later parse wins per key
    parseUiSkin("[strings]\ntitle.row.new_game = X\n", s);
    CHECK(s.strings.at("title.row.new_game") == "X");
    // round trip
    UiSkin r;
    parseUiSkin(writeUiSkin(s), r);
    CHECK(r.strings == s.strings && r.rows == s.rows && r.colors == s.colors && r.numbers == s.numbers);
    Color c = 0;
    CHECK(parseSkinColor("e07b2e", c) && c == rgba(224, 123, 46) && formatSkinColor(c) == "#E07B2E");
    CHECK(!parseSkinColor("#12345", c) && !parseSkinColor("", c));
}

void testNoSkinIsDefault() {
    setUiSkin({});
    const Theme t;
    CHECK(t.ember == rgba(224, 123, 46) && t.hudAccent == t.gilt && t.sizeBody == 24);
    TitleScreen ts;
    ts.open();
    TitleState st;
    st.continueSlot = 0;
    ts.frame(st, {}, kVp);
    CHECK((titleLabels(ts, st) ==
           std::vector<std::string>{"Load Game", "New Game", "Options", "Credits", "PC Settings", "Quit"}));
}

void testTheme() {
    UiSkin s;
    parseUiSkin("[theme]\naccent = #20C0A0\nselect = #00FF00\nsize_body = 28\n[hud]\naccent = #FF00FF\n", s);
    setUiSkin(s);
    const Theme t;
    CHECK(t.ember == rgba(0x20, 0xC0, 0xA0) && (t.emberGlow >> 8) == (rgba(0x20, 0xC0, 0xA0) >> 8));
    CHECK((t.emberGlow & 0xff) == 58); // the glow keeps its default alpha
    CHECK(t.select == rgba(0, 255, 0) && t.sizeBody == 28 && t.hudAccent == rgba(255, 0, 255));
    CHECK(t.gilt == rgba(226, 182, 92)); // untouched
    ScreenConfig cfg;                    // the screens' config picks it up
    CHECK(cfg.theme.hudAccent == rgba(255, 0, 255));
    setUiSkin({});
}

void testTitle() {
    UiSkin s;
    parseUiSkin("[rows]\ntitle.main = new_game, continue, quit, options\n"
                "[strings]\ntitle.row.new_game = Begin Anew\ntitle.desc.new_game = A fresh start.\n"
                "title.quit.title = Leave?\n[layout]\ntitle_column_w = 640\n",
                s);
    setUiSkin(s);
    TitleScreen ts;
    ts.open();
    TitleState st;
    st.continueSlot = 3; // Continue is shown
    TitleFrameOutput o = ts.frame(st, {}, kVp);
    CHECK((titleLabels(ts, st) == std::vector<std::string>{"Begin Anew", "Continue", "Quit", "Options"}));
    CHECK(hasText(o.draw, "Begin Anew") && !hasText(o.draw, "New Game") && !hasText(o.draw, "Credits"));
    CHECK(hasText(o.draw, "A fresh start.")); // the focused row's description (first row after the rename)
    CHECK(design::title::menuPanel(4).w == 640);
    // Continue is hidden without a save even though the skin lists it
    st.continueSlot = 0;
    ts.frame(st, {}, kVp);
    CHECK((titleLabels(ts, st) == std::vector<std::string>{"Begin Anew", "Quit", "Options"}));
    // Down from the first row lands on the second listed row: navigation follows the skin order
    o = ts.frame(st, keys({UiKey::Down}), kVp);
    CHECK((ts.focus() == TargetId{TargetKind::TitleRow, static_cast<int>(TitleRow::Quit)}));
    // an unusable list (no known id) falls back to the default
    UiSkin bad;
    parseUiSkin("[rows]\ntitle.main = nothing, here\n", bad);
    setUiSkin(bad);
    CHECK(ts.rows(st).size() == 6);
    setUiSkin({});
}

void testPause() {
    UiSkin s;
    parseUiSkin(
        "[rows]\npause.root = resume, options, save, load, quit\npause.audio = music_volume, master_volume, back\n"
        "[strings]\npause.row.resume = Return to Game\n[layout]\npause_row_step = 120\n",
        s);
    setUiSkin(s);
    ScreenWorld w;
    const Snapshot snap = w.read();
    PauseScreen scr{PauseConfig{}};
    PauseState st;
    scr.open();
    PauseFrameOutput o = scr.frame(snap, st, {}, kVp);
    CHECK((scr.rows() ==
           std::vector<PauseRow>{PauseRow::Resume, PauseRow::Options, PauseRow::Save, PauseRow::Load, PauseRow::Quit}));
    CHECK(hasText(o.draw, "Return to Game")); // (the Back hint still reads "Resume")
    CHECK(design::pause::row(1).y - design::pause::row(0).y == 120);
    setUiSkin({});
    CHECK(design::pause::row(1).y - design::pause::row(0).y == 104);
}

void testDefaultFile() {
    const std::string text = defaultSkinText();
    UiSkin s;
    std::vector<std::string> w;
    parseUiSkin(text, s, &w);
    CHECK(w.empty());
    // the full reference skin changes nothing
    setUiSkin(s);
    const Theme with;
    setUiSkin({});
    const Theme without;
    CHECK(with.ember == without.ember && with.panel == without.panel && with.hudAccent == without.hudAccent &&
          with.sizeTitle == without.sizeTitle);
    TitleScreen a;
    a.open();
    TitleState st;
    st.continueSlot = 2;
    st.canLoad = true;
    a.frame(st, {}, kVp);
    const std::vector<TitleRow> plain = a.rows(st);
    TitleConfig cfg;
    cfg.showMultiplayer = true;
    TitleScreen b(cfg);
    b.open();
    const std::vector<TitleRow> mp = b.rows(st);
    setUiSkin(s);
    CHECK(a.rows(st) == plain && b.rows(st) == mp);
    CHECK(design::title::menuPanel(6).h > 0);
    setUiSkin({});

#ifdef GAMEUI_REPO_ROOT
    const std::string path = std::string(GAMEUI_REPO_ROOT) + "/configs/ui/default.ini";
    if (std::getenv("DRAKAN_WRITE_DEFAULT_SKIN")) {
        std::ofstream(path, std::ios::binary) << text;
    }
    std::ifstream f(path, std::ios::binary);
    CHECK(f.good());
    const std::string onDisk((std::istreambuf_iterator<char>(f)), {});
    CHECK(onDisk == text); // regenerate with DRAKAN_WRITE_DEFAULT_SKIN=1 after changing a default
#endif
}

} // namespace

int main() {
    testParse();
    testNoSkinIsDefault();
    testTheme();
    testTitle();
    testPause();
    testDefaultFile();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
