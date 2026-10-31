#include "drakan_game.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"

namespace drakan_game {
namespace {
constexpr const char* kIntroLevel = "Level 0 - Intro";

std::mutex g_m;
std::vector<Campaign> g_campaigns;
RedirectFn g_redirect;
Game g_game = Game::Drakan;
bool g_ask = true;
std::filesystem::path g_baseCard;

// The campaign redirect of the current game; `clearIfNone`: d2 clears the file layer's redirect (leaving it alone at
// start keeps the DRAKAN_START_LEVEL test aid).
void applyBase(bool clearIfNone) {
    if (!g_redirect)
        return;
    if (g_game == Game::Ootf && !g_campaigns.empty())
        g_redirect(kIntroLevel, g_campaigns.front().start);
    else if (clearIfNone)
        g_redirect("", "");
}

void applyCard() {
    if (g_baseCard.empty())
        return;
    std::filesystem::path root = g_baseCard;
    if (g_game == Game::Ootf) {
        std::string leaf = g_baseCard.filename().string();
        if (leaf.empty())
            leaf = "mc0";
        root = g_baseCard.parent_path() / (leaf + "_ootf");
        // The options (audio, controller) are the player's, not the game's: a new campaign card starts from the same
        // settings folder instead of the defaults.
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string settings = std::string(drakan_region::build().saveId) + "Settings";
        if (!fs::exists(root / settings, ec) && fs::is_directory(g_baseCard / settings, ec)) {
            fs::create_directories(root, ec);
            fs::copy(g_baseCard / settings, root / settings, fs::copy_options::recursive, ec);
        }
    }
    PS2Runtime::IoPaths p = PS2Runtime::getIoPaths();
    p.mcRoot = root;
    PS2Runtime::setIoPaths(p);
}
} // namespace

void configure(std::vector<Campaign> campaigns, RedirectFn redirect) {
    std::lock_guard<std::mutex> l(g_m);
    g_campaigns = std::move(campaigns);
    g_redirect = std::move(redirect);
    g_baseCard = PS2Runtime::getIoPaths().mcRoot;
    const std::string want = drakan_cfg::getString("general", "game", "DRAKAN_GAME", "ask");
    g_ask = true;
    g_game = Game::Drakan;
    if (drakan_cfg::compareNoCase(want.c_str(), "d1") == 0 || drakan_cfg::compareNoCase(want.c_str(), "ootf") == 0) {
        g_ask = false;
        if (!g_campaigns.empty())
            g_game = Game::Ootf;
        else
            std::fprintf(stderr, "[game] game = d1 but no pack with campaign = \"ootf\" is active: starting Drakan\n");
    } else if (drakan_cfg::compareNoCase(want.c_str(), "d2") == 0) {
        g_ask = false;
    }
    for (const Campaign& c : g_campaigns)
        std::fprintf(stderr, "[game] campaign '%s' (%s) from pack '%s', New Game opens '%s'\n", c.id.c_str(),
                     c.name.c_str(), c.modId.c_str(), c.start.c_str());
    applyCard();
    applyBase(false);
    std::fprintf(stderr, "[game] playing %s (setting '%s')\n", g_game == Game::Ootf ? "d1" : "d2", want.c_str());
}

bool ootfAvailable() {
    std::lock_guard<std::mutex> l(g_m);
    return !g_campaigns.empty();
}

Game current() {
    std::lock_guard<std::mutex> l(g_m);
    return g_game;
}

std::string name(Game g) {
    std::lock_guard<std::mutex> l(g_m);
    if (g == Game::Ootf)
        return !g_campaigns.empty() && !g_campaigns.front().name.empty() ? g_campaigns.front().name
                                                                         : "Drakan: Order of the Flame";
    return "Drakan";
}

bool offerChoice() {
    std::lock_guard<std::mutex> l(g_m);
    return g_ask && !g_campaigns.empty();
}

bool select(Game g) {
    std::lock_guard<std::mutex> l(g_m);
    if (g == Game::Ootf && g_campaigns.empty())
        return false;
    if (g != g_game) {
        g_game = g;
        applyCard();
        applyBase(true);
        std::fprintf(stderr, "[game] switched to %s\n", g == Game::Ootf ? "d1" : "d2");
    }
    return true;
}

void redirect(const std::string& from, const std::string& to) {
    std::lock_guard<std::mutex> l(g_m);
    if (!g_redirect)
        return;
    if (from.empty() || to.empty())
        applyBase(true);
    else
        g_redirect(from, to);
}

std::string firstLevel() {
    std::lock_guard<std::mutex> l(g_m);
    return g_game == Game::Ootf && !g_campaigns.empty() ? g_campaigns.front().start : "Level 1A - Surdana";
}

std::string mapLevelName(const std::string& requested) {
    std::lock_guard<std::mutex> l(g_m);
    if (g_game == Game::Ootf && !g_campaigns.empty() && requested == kIntroLevel)
        return g_campaigns.front().start;
    return requested;
}

bool isDiscCutsceneLevel(const std::string& level) {
    return level.rfind("Level 0", 0) == 0;
}

} // namespace drakan_game
