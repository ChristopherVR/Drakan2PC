#include "drakan_game.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "drakan_config.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "ps2_runtime.h"

namespace drakan_game {
namespace {
constexpr const char* kIntroLevel = "Level 0 - Intro";

std::mutex g_m;
std::vector<Campaign> g_campaigns;
RedirectFn g_redirect;
SkinFn g_skin;
int g_index = 0; // 0 = the disc game, i = g_campaigns[i - 1]
bool g_ask = true;
std::string g_setting = "ask"; // what [general] game / DRAKAN_GAME said
bool g_settingFromEnv = false;
bool g_rewrite = false; // the campaign's New Game path rewrite is armed (not while DRAKAN_START_LEVEL drives the start)
bool g_mpRedirect = false; // a multiplayer launch holds its own file redirect: no campaign rewrite meanwhile
std::filesystem::path g_baseCard;

const Campaign* cur() {
    return g_index > 0 && g_index <= static_cast<int>(g_campaigns.size()) ? &g_campaigns[g_index - 1] : nullptr;
}

Game kindOf(const Campaign* c) {
    return !c ? Game::Drakan : c->id == "ootf" ? Game::Ootf : Game::Custom;
}

std::string nameOf(const Campaign* c) {
    if (!c)
        return "Drakan";
    if (!c->name.empty())
        return c->name;
    return c->id == "ootf" ? "Drakan: Order of the Flame" : c->id;
}

bool equalNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// New Game opens the disc's opening cut scene level ("Level 0 - Intro"); in a campaign that load is pointed at the
// campaign's start level before the engine sees it (drakan_lifecycle::setLevelPathRewriter), so the engine knows the
// level by its real name: its exits, the arrival markers of the next level and the save file names are right (a file
// redirect kept the name "Level 0 - Intro", and the arrival after the first exit fell back to the level's default
// point). Saves are loaded by their own path (fromSave: untouched).
std::string rewriteLevelPath(const std::string& path, bool fromSave) {
    if (fromSave)
        return {};
    std::lock_guard<std::mutex> l(g_m);
    const Campaign* c = cur();
    if (!c || !g_rewrite || g_mpRedirect || c->start.empty() || equalNoCase(c->start, kIntroLevel))
        return {};
    const size_t slash = path.find_last_of("/\\");
    const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = file.rfind('.');
    if (!equalNoCase(dot == std::string::npos ? file : file.substr(0, dot), kIntroLevel))
        return {}; // another level, or another file of the intro folder (Out.lvl, the outro)
    const std::string intro = kIntroLevel;
    std::string out;
    for (size_t i = 0; i < path.size();) {
        if (equalNoCase(path.substr(i, intro.size()), intro)) {
            out += c->start;
            i += intro.size();
        } else
            out += path[i++];
    }
    std::fprintf(stderr, "[game] New Game of '%s' opens '%s'\n", c->id.c_str(), c->start.c_str());
    return out;
}

// The base file redirect of the current game: none (a campaign starts through the path rewrite above). `clearIfNone`:
// clear the file layer's redirect (leaving it alone at start keeps the DRAKAN_START_LEVEL test aid).
void applyBase(bool clearIfNone) {
    if (g_redirect && clearIfNone)
        g_redirect("", "");
}

void applyCard() {
    if (g_baseCard.empty())
        return;
    std::filesystem::path root = g_baseCard;
    if (const Campaign* c = cur()) {
        std::string leaf = g_baseCard.filename().string();
        if (leaf.empty())
            leaf = "mc0";
        root = g_baseCard.parent_path() / (leaf + "_" + c->saves);
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
    std::fprintf(stderr, "[game] memory card folder %s\n", root.string().c_str());
}

void applySkin() {
    if (g_skin)
        g_skin(cur());
}
} // namespace

bool validCampaignId(const std::string& id) {
    if (id.empty() || id.size() > 32 || !std::isalnum(static_cast<unsigned char>(id[0])))
        return false;
    for (char ch : id)
        if (!(std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) ||
              ch == '_' || ch == '-'))
            return false;
    return id != "d1" && id != "d2" && id != "drakan" && id != "ask";
}

bool validSaveFolder(const std::string& s) {
    if (s.empty() || s.size() > 32)
        return false;
    for (char ch : s)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'))
            return false;
    return true;
}

void configure(std::vector<Campaign> campaigns, RedirectFn redirect, SkinFn skin) {
    std::lock_guard<std::mutex> l(g_m);
    g_campaigns = std::move(campaigns);
    for (Campaign& c : g_campaigns)
        if (c.saves.empty())
            c.saves = c.id;
    g_redirect = std::move(redirect);
    g_skin = std::move(skin);
    g_baseCard = PS2Runtime::getIoPaths().mcRoot;
    const std::string want = drakan_cfg::getString("general", "game", "DRAKAN_GAME", "ask");
    g_setting = want.empty() ? std::string("ask") : want;
    {
        const char* env = std::getenv("DRAKAN_GAME");
        g_settingFromEnv = env && *env;
    }
    g_ask = true;
    g_index = 0;
    if (drakan_cfg::compareNoCase(want.c_str(), "d2") == 0 || drakan_cfg::compareNoCase(want.c_str(), "drakan") == 0) {
        g_ask = false;
    } else if (drakan_cfg::compareNoCase(want.c_str(), "ask") != 0 && !want.empty()) {
        g_ask = false;
        const bool d1 = drakan_cfg::compareNoCase(want.c_str(), "d1") == 0;
        for (size_t i = 0; i < g_campaigns.size(); ++i)
            if (drakan_cfg::compareNoCase(g_campaigns[i].id.c_str(), d1 ? "ootf" : want.c_str()) == 0) {
                g_index = static_cast<int>(i) + 1;
                break;
            }
        if (g_index == 0)
            std::fprintf(stderr, "[game] game = %s but no active pack has that campaign: starting Drakan\n",
                         want.c_str());
    }
    for (const Campaign& c : g_campaigns)
        std::fprintf(stderr, "[game] campaign '%s' (%s) from pack '%s', New Game opens '%s', saves in <card>_%s\n",
                     c.id.c_str(), nameOf(&c).c_str(), c.modId.c_str(), c.start.c_str(), c.saves.c_str());
    // The DRAKAN_START_LEVEL test aid (drakan_overrides / drakan_playtest) installed its own start before the campaign
    // was known: keep it, or a multiplayer arena started next to a campaign pack loads the campaign's first level
    // instead (the "dark Relic"). Choosing a game on the title arms the rewrite again.
    const char* sl = std::getenv("DRAKAN_START_LEVEL");
    g_rewrite = !(sl && *sl);
    if (g_rewrite && !g_campaigns.empty())
        drakan_lifecycle::setLevelPathRewriter(rewriteLevelPath);
    applyCard();
    applyBase(false);
    applySkin();
    std::fprintf(stderr, "[game] playing %s (setting '%s')\n", cur() ? cur()->id.c_str() : "d2", want.c_str());
}

bool ootfAvailable() {
    std::lock_guard<std::mutex> l(g_m);
    return std::any_of(g_campaigns.begin(), g_campaigns.end(), [](const Campaign& c) { return c.id == "ootf"; });
}

Game current() {
    std::lock_guard<std::mutex> l(g_m);
    return kindOf(cur());
}

std::vector<GameInfo> games() {
    std::lock_guard<std::mutex> l(g_m);
    std::vector<GameInfo> v;
    v.push_back({"d2", "Drakan", "The Ancients' Gates, the game on your disc.", Game::Drakan});
    for (const Campaign& c : g_campaigns) {
        std::string d = c.description;
        if (d.empty() && c.id == "ootf")
            d = "The first Drakan, rebuilt on this engine: its own levels and saves.";
        v.push_back({c.id, nameOf(&c), d, kindOf(&c)});
    }
    return v;
}

int currentIndex() {
    std::lock_guard<std::mutex> l(g_m);
    return g_index;
}

std::string currentModId() {
    std::lock_guard<std::mutex> l(g_m);
    const Campaign* c = cur();
    return c ? c->modId : std::string();
}

std::string currentName() {
    std::lock_guard<std::mutex> l(g_m);
    return nameOf(cur());
}

bool offerChoice() {
    std::lock_guard<std::mutex> l(g_m);
    return g_ask && !g_campaigns.empty();
}

std::string choiceSetting(bool* fromEnv) {
    std::lock_guard<std::mutex> l(g_m);
    if (fromEnv)
        *fromEnv = g_settingFromEnv;
    return g_setting;
}

bool select(int index) {
    std::lock_guard<std::mutex> l(g_m);
    if (index < 0 || index > static_cast<int>(g_campaigns.size()))
        return false;
    if (index != g_index) {
        g_index = index;
        if (!g_rewrite && index > 0) { // the title's choice: the campaign start replaces a test aid's start
            g_rewrite = true;
            drakan_lifecycle::setLevelPathRewriter(rewriteLevelPath);
        }
        applyCard();
        applyBase(true);
        applySkin();
        std::fprintf(stderr, "[game] switched to %s\n", cur() ? cur()->id.c_str() : "d2");
    }
    return true;
}

void redirect(const std::string& from, const std::string& to) {
    std::lock_guard<std::mutex> l(g_m);
    if (!g_redirect)
        return;
    g_mpRedirect = !from.empty() && !to.empty();
    if (!g_mpRedirect)
        applyBase(true);
    else
        g_redirect(from, to);
}

std::string firstLevel() {
    std::lock_guard<std::mutex> l(g_m);
    const Campaign* c = cur();
    return c ? c->start : "Level 1A - Surdana";
}

std::vector<std::string> campaignLevels() {
    std::lock_guard<std::mutex> l(g_m);
    const Campaign* c = cur();
    return c ? c->levels : std::vector<std::string>{};
}

bool isCampaignPackLevel(const std::string& level) {
    std::lock_guard<std::mutex> l(g_m);
    for (const Campaign& c : g_campaigns)
        for (const std::string& name : c.levels)
            if (drakan_cfg::compareNoCase(name.c_str(), level.c_str()) == 0)
                return true;
    return false;
}

std::string mapLevelName(const std::string& requested) {
    std::lock_guard<std::mutex> l(g_m);
    if (const Campaign* c = cur(); c && requested == kIntroLevel)
        return c->start;
    return requested;
}

bool isDiscCutsceneLevel(const std::string& level) {
    return level.rfind("Level 0", 0) == 0;
}

} // namespace drakan_game
