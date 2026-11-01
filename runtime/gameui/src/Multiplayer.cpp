#include "gameui/Multiplayer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include "Paint.h"
#include "gameui/PauseScreen.h"

namespace gameui::mp {

namespace {

Rect hostCard(int i); // defined with the page drawing below

constexpr float kPanelH = 840, kPanelY = 150, kPad = 28;
constexpr float kRowH = 46, kRowStep = 52;
constexpr int kVisibleList = 8; // Join: servers shown at once
constexpr int kVisibleMaps = 8; // Host: maps shown at once
constexpr size_t kNameMax = 24, kServerNameMax = 32, kAddrMax = 64, kPasswordMax = 24, kChatMax = 120;
int wrapIdx(int v, int n) {
    return n <= 0 ? 0 : ((v % n) + n) % n;
}

std::string limitText(int v, const char* unit) {
    return v <= 0 ? std::string("No Limit") : std::to_string(v) + unit;
}

const char* kNameChars = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-_";

} // namespace

const char* modeName(GameMode m) {
    switch (m) {
        case GameMode::Deathmatch:
            return "Deathmatch";
        case GameMode::TeamDeathmatch:
            return "Team Deathmatch";
        case GameMode::DragonsDuel:
            return "Dragon's Duel";
        case GameMode::DragonMaster:
            return "Dragon Master";
        case GameMode::Coop:
            return "Co-op";
        case GameMode::Count:
            break;
    }
    return "?";
}

bool modeAvailable(GameMode m) {
    return m == GameMode::Deathmatch || m == GameMode::TeamDeathmatch || m == GameMode::Coop;
}

const char* mpActionName(MpActionKind k) {
    switch (k) {
        case MpActionKind::HostMatch:
            return "HostMatch";
        case MpActionKind::JoinMatch:
            return "JoinMatch";
        case MpActionKind::Leave:
            return "Leave";
        case MpActionKind::StartMatch:
            return "StartMatch";
        case MpActionKind::SetReady:
            return "SetReady";
        case MpActionKind::RefreshLan:
            return "RefreshLan";
        case MpActionKind::SaveProfile:
            return "SaveProfile";
        case MpActionKind::Close:
            return "Close";
        case MpActionKind::SendChat:
            return "SendChat";
        case MpActionKind::SetCharacter:
            return "SetCharacter";
        case MpActionKind::KickPlayer:
            return "KickPlayer";
    }
    return "?";
}

std::vector<LevelEntry> vanillaLevels() {
    static const char* const kNames[] = {"Level 1A - Surdana",
                                         "Level 1B - Kragmor",
                                         "Level 1C - Catacombs",
                                         "Level 1D - City of the Damned",
                                         "Level 1E - Inquisitor's Lair",
                                         "Level 2 - Shadowmire",
                                         "Level 3A - Andrellian Isles",
                                         "Level 3B - Crystal Mine",
                                         "Level 4A - Ravenshold",
                                         "Level 4B - Northern Tundra",
                                         "Level 5 - Shiverbane",
                                         "Level 6 - Valley of the Fallen",
                                         "Level 6 - Transition to Desert",
                                         "Level 8 - Stratos"};
    std::vector<LevelEntry> v;
    for (const char* n : kNames) {
        LevelEntry e;
        e.name = n;
        const std::string s = n;
        const size_t dash = s.find(" - ");
        e.title = dash == std::string::npos ? s : s.substr(6, dash - 6) + "  " + s.substr(dash + 3);
        v.push_back(std::move(e));
    }
    return v;
}

std::vector<CharacterEntry> placeholderCharacters() {
    return {{0, "Rynn", "Rynn", "The dragon rider. Always the host's character.", true},
            {1, "Wartok", "Placeholder 1", "A heavy fighter (placeholder).", false},
            {2, "Tarn", "Placeholder 2", "A quick scout (placeholder).", false},
            {3, "Sera", "Placeholder 3", "A battle mage (placeholder).", false}};
}

const std::vector<std::string>& modelNames() {
    static const std::vector<std::string> v = {"Rynn"};
    return v;
}
const std::vector<std::string>& dragonSkinNames() {
    static const std::vector<std::string> v = {"Red", "Green serpent", "Tan lizard", "Grey", "Blue", "Black", "White"};
    return v;
}
const std::vector<std::string>& speedNames() {
    static const std::vector<std::string> v = {"Modem 28.8k", "Modem 33.6k", "Modem 56k", "ISDN 112k", "LAN / T1 1M"};
    return v;
}
const std::vector<std::string>& colourNames() {
    static const std::vector<std::string> v = {"Crimson", "Azure", "Emerald", "Gold",
                                               "Violet",  "Amber", "Silver",  "Onyx"};
    return v;
}
Color colourRgba(int i) {
    static const Color c[] = {rgba(196, 40, 36),  rgba(60, 120, 214), rgba(58, 164, 96),   rgba(222, 186, 70),
                              rgba(138, 84, 200), rgba(222, 128, 40), rgba(190, 196, 204), rgba(40, 38, 44)};
    return c[wrapIdx(i, 8)];
}

} // namespace gameui::mp

namespace gameui::design::mp {
Rect panel() {
    return {(kDesignW - 1100) * 0.5f, gameui::mp::kPanelY, 1100, gameui::mp::kPanelH};
}
float rowH() {
    return gameui::mp::kRowH;
}
float rowStep() {
    return gameui::mp::kRowStep;
}
Rect firstRow() {
    const Rect p = panel();
    return {p.x + gameui::mp::kPad, p.y + 96, p.w - 2 * gameui::mp::kPad, gameui::mp::kRowH};
}
Rect titleRect() {
    const Rect p = panel();
    return {p.x + gameui::mp::kPad, p.y + 20, p.w - 2 * gameui::mp::kPad, 56};
}
} // namespace gameui::design::mp

namespace gameui::mp {

MultiplayerScreen::MultiplayerScreen(MpConfig cfg) : cfg_(std::move(cfg)) {}

// Which game modes the screen's scope offers: the Multiplayer menu has no Co-op, the campaign's Co-op flow only has it.
static bool scopeAllows(MpScope sc, GameMode m) {
    return sc == MpScope::Any || (sc == MpScope::Coop) == (m == GameMode::Coop);
}

void MultiplayerScreen::open(const MpState& st, MpScope scope) {
    open_ = true;
    scope_ = scope;
    if (scope == MpScope::Coop) {
        host_.coop = true;
        host_.mode = GameMode::Coop;
        host_.modeId.clear();
        host_.modeTeam = false;
    } else if (scope == MpScope::Versus && host_.coop) {
        host_.coop = false;
        host_.mode = host_.teams >= 2 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
    }
    page_ = MpPage::Menu;
    lobbyActive_ = false;
    lobbyFocused_ = false;
    rows_.clear();
    focus_ = 0;
    hover_ = -1;
    editing_ = false;
    if (!profileSeeded_ && !st.defaultName.empty()) {
        setup_.name = st.defaultName.substr(0, kNameMax);
        setup_.colour = st.defaultColour;
        setup_.model = st.defaultModel;
        profileSeeded_ = true;
    }
    host_.level = 0;
    levelScroll_ = 0;
    chatInput_.clear();
    buildRows(st);
}

void MultiplayerScreen::openLobby(const MpState& st) {
    open(st, st.mode == GameMode::Coop ? MpScope::Coop : MpScope::Versus);
    setPage(MpPage::Lobby, st);
}

void MultiplayerScreen::close() {
    open_ = false;
    editing_ = false;
}

void MultiplayerScreen::setPage(MpPage p, const MpState& st) {
    page_ = p;
    lobbyActive_ = false;
    lobbyFocused_ = false;
    focus_ = 0;
    rows_.clear(); // the old page's rows must not steer the new focus
    editing_ = false;
    mapListMode_ = false;
    autoChar_ = -1;
    listScroll_ = 0;
    buildRows(st);
    ensureFocus();
}

bool MultiplayerScreen::navigable(int i) const {
    if (i < 0 || i >= rowCount())
        return false;
    const Row& r = rows_[static_cast<size_t>(i)];
    return r.kind != RowKind::Info && r.enabled;
}

void MultiplayerScreen::ensureFocus() {
    if (navigable(focus_))
        return;
    for (int i = 0; i < rowCount(); ++i)
        if (navigable(i)) {
            focus_ = i;
            return;
        }
    focus_ = 0;
}

void MultiplayerScreen::moveFocus(int dir) {
    for (int i = focus_ + dir; i >= 0 && i < rowCount(); i += dir)
        if (navigable(i)) {
            focus_ = i;
            return;
        }
}

static const std::vector<LevelEntry>& levelsOf(const MpState& st) {
    static const std::vector<LevelEntry> vanilla = vanillaLevels();
    return st.levels.empty() ? vanilla : st.levels;
}

// The Host page's Level Group: All Levels, the disc's levels, or the custom (mod pack) levels.
// Deathmatch maps are multiplayer arenas (`st.arenas`) with the campaign levels as the fallback until the arena
// definitions exist; Co-op always plays campaign levels.
std::vector<LevelEntry> MultiplayerScreen::groupLevels(const MpState& st) const {
    std::vector<LevelEntry> out;
    const bool arenas = !host_.coop && !st.arenas.empty();
    for (const LevelEntry& e : arenas ? st.arenas : levelsOf(st))
        if (host_.levelGroup == 0 || (host_.levelGroup == 1 && !e.custom) || (host_.levelGroup == 2 && e.custom))
            out.push_back(e);
    return out;
}

GameMode MultiplayerScreen::hostMode() const {
    if (host_.coop)
        return GameMode::Coop;
    if (!host_.modeId.empty()) // a custom mode brings its own base rules
        return host_.modeTeam ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
    return host_.teams >= 2 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
}

// The custom modes the chosen map can play (Game Mode row); none for co-op and for campaign levels.
std::vector<const ModeEntry*> MultiplayerScreen::modesForMap(const MpState& st) const {
    std::vector<const ModeEntry*> out;
    if (host_.coop)
        return out;
    const LevelEntry map = hostMap(st);
    for (const ModeEntry& m : st.modes)
        if (std::find(map.modeIds.begin(), map.modeIds.end(), m.id) != map.modeIds.end())
            out.push_back(&m);
    return out;
}

// The characters the local player may pick: the host is Rynn, a joiner picks one of the others.
std::vector<CharacterEntry> MultiplayerScreen::allowedCharacters(const MpState& st) const {
    std::vector<CharacterEntry> all = st.characters.empty() ? placeholderCharacters() : st.characters;
    std::vector<CharacterEntry> out;
    for (const CharacterEntry& c : all)
        if (c.hostOnly == st.hosting)
            out.push_back(c);
    if (out.empty())
        out = all;
    return out;
}

CharacterEntry MultiplayerScreen::selectedCharacter(const MpState& st) const {
    const std::vector<CharacterEntry> ok = allowedCharacters(st);
    const int want = st.selfCharacter >= 0 ? st.selfCharacter : setup_.character;
    for (const CharacterEntry& c : ok)
        if (c.classId == want)
            return c;
    return ok.empty() ? CharacterEntry{} : ok.front();
}

LevelEntry MultiplayerScreen::hostMap(const MpState& st) const {
    const std::vector<LevelEntry> lv = groupLevels(st);
    if (lv.empty())
        return {};
    return lv[static_cast<size_t>(wrapIdx(host_.level, static_cast<int>(lv.size())))];
}

// The maps whose pictures the current page wants (the glue loads them): the Host list's visible maps, the selected
// server's map is a level name only (no id), the lobby's map likewise.
std::vector<uint32_t> MultiplayerScreen::previewRequests(const MpState& st) const {
    std::vector<uint32_t> ids;
    if (page_ != MpPage::Host)
        return ids;
    const std::vector<LevelEntry> lv = groupLevels(st);
    const int n = static_cast<int>(lv.size());
    for (int i = std::max(0, levelScroll_); i < std::min(n, levelScroll_ + kVisibleMaps); ++i)
        if (lv[static_cast<size_t>(i)].previewId)
            ids.push_back(lv[static_cast<size_t>(i)].previewId);
    const LevelEntry cur = hostMap(st);
    if (cur.previewId && std::find(ids.begin(), ids.end(), cur.previewId) == ids.end())
        ids.push_back(cur.previewId);
    return ids;
}

bool MultiplayerScreen::selectMode(GameMode m, const MpState& st) {
    if (!modeAvailable(m) || !scopeAllows(scope_, m))
        return false;
    host_.coop = m == GameMode::Coop;
    host_.modeId.clear();
    host_.modeTeam = false;
    if (m == GameMode::Deathmatch)
        host_.teams = 0;
    else if (m == GameMode::TeamDeathmatch && host_.teams < 2)
        host_.teams = 2;
    host_.mode = hostMode();
    host_.level = 0;
    levelScroll_ = 0;
    buildRows(st);
    return true;
}

// Join list: LAN answers sorted by ping (lowest first), then the Add Server entries ("+" prefix, white).
void MultiplayerScreen::buildEntries(const MpState& st) {
    entries_.clear();
    std::vector<LanServer> lan = st.lan;
    std::stable_sort(lan.begin(), lan.end(),
                     [](const LanServer& a, const LanServer& b) { return a.pingMs < b.pingMs; });
    for (const LanServer& s : lan) {
        JoinEntry e;
        e.name = s.name.empty() ? s.host : s.name;
        e.level = s.level;
        e.address = s.host + ":" + std::to_string(s.port);
        e.players = s.players;
        e.maxPlayers = s.maxPlayers;
        e.pingMs = s.pingMs;
        e.locked = s.passwordProtected;
        e.mods = s.mods;
        e.mode = s.mode;
        e.known = true;
        entries_.push_back(std::move(e));
    }
    for (const std::string& a : custom_) {
        JoinEntry e;
        e.name = "+" + a;
        e.address = a;
        e.custom = true;
        entries_.push_back(std::move(e));
    }
}

void MultiplayerScreen::buildRows(const MpState& st) {
    // Rows come and go (players join, buttons enable): the focus follows the row, not its index.
    const bool keep = focus_ >= 0 && focus_ < rowCount();
    const RowId keepId = keep ? rows_[static_cast<size_t>(focus_)].id : RowId::MenuBack;
    const int keepIndex = keep ? rows_[static_cast<size_t>(focus_)].index : 0;
    rows_.clear();
    const Rect first = design::mp::firstRow();
    float y = first.y;
    const auto add = [&](RowId id, RowKind kind, std::string label, std::string value = {}, bool enabled = true,
                         int index = 0) {
        Row r;
        r.id = id;
        r.kind = kind;
        r.index = index;
        r.label = std::move(label);
        r.value = std::move(value);
        r.enabled = enabled;
        r.rect = {first.x, y, first.w, kRowH}; // placed by layoutRows
        y += kRowStep;
        rows_.push_back(std::move(r));
    };
    const bool net = st.available;
    const auto pick = [](const std::vector<std::string>& v, int i) {
        return v[static_cast<size_t>(wrapIdx(i, static_cast<int>(v.size())))];
    };
    switch (page_) {
        case MpPage::Menu: // the OotF "Multiplayer" screen: Player Name, Player Color, skins, Connection Speed, buttons
            add(RowId::MenuName, RowKind::Text, "Player Name", setup_.name);
            add(RowId::MenuColour, RowKind::Choice, "Player Color", pick(colourNames(), setup_.colour));
            add(RowId::MenuHumanSkin, RowKind::Choice, "Human Skin", pick(modelNames(), setup_.model));
            add(RowId::MenuDragonSkin, RowKind::Choice, "Dragon Skin", pick(dragonSkinNames(), setup_.dragonSkin));
            add(RowId::MenuSpeed, RowKind::Choice, "Connection Speed", pick(speedNames(), setup_.speed));
            add(RowId::MenuHost, RowKind::Action, "Host Game", {}, net);
            add(RowId::MenuJoin, RowKind::Action, "Join Game", {}, net);
            add(RowId::MenuMaster, RowKind::Action, "mplayer.com", {}, false);
            add(RowId::MenuBack, RowKind::Action, "Back");
            break;
        case MpPage::Host: {
            const std::vector<LevelEntry> levels = groupLevels(st);
            host_.level = wrapIdx(host_.level, static_cast<int>(levels.size()));
            if (host_.level < levelScroll_)
                levelScroll_ = host_.level;
            if (host_.level >= levelScroll_ + kVisibleMaps)
                levelScroll_ = host_.level - kVisibleMaps + 1;
            levelScroll_ = std::clamp(levelScroll_, 0, std::max(0, static_cast<int>(levels.size()) - kVisibleMaps));
            // A custom mode that does not fit this map (or this game type) drops back to the standard rules.
            const std::vector<const ModeEntry*> fits = modesForMap(st);
            const ModeEntry* mode = nullptr;
            for (const ModeEntry* m : fits)
                if (m->id == host_.modeId)
                    mode = m;
            if (!mode && !host_.modeId.empty()) {
                host_.modeId.clear();
                host_.modeTeam = false;
            }
            if (mode) { // its rules fix the type, the limits and the teams
                host_.modeTeam = mode->teamBase;
                host_.teams = mode->teamBase ? std::max(2, mode->teams) : 0;
                host_.fragLimit = mode->fragLimit;
                host_.timeLimitMin = (mode->timeLimitSec + 59) / 60;
            }
            const bool team = host_.teams >= 2;
            const bool arenas = !host_.coop && !st.arenas.empty();
            const char* const kGroups[] = {arenas ? "All Maps" : "All Levels", arenas ? "Arenas" : "Disc levels",
                                           arenas ? "Custom maps" : "Custom levels"};
            const std::string lvl =
                levels.empty() ? std::string("none") : levels[static_cast<size_t>(host_.level)].title;
            add(RowId::HostName, RowKind::Text, "Server Name", host_.serverName);
            add(RowId::HostPassword, RowKind::Text, "Password",
                host_.password.empty() ? std::string("open") : std::string(host_.password.size(), '*'));
            add(RowId::HostGroup, RowKind::Choice, "Level Group", kGroups[wrapIdx(host_.levelGroup, 3)]);
            add(RowId::HostLevel, RowKind::Choice, arenas ? "Map" : "Starting Level",
                lvl + (!levels.empty() && levels[static_cast<size_t>(host_.level)].custom ? "  (custom)" : ""));
            add(RowId::HostType, RowKind::Choice, "Game Type",
                host_.coop ? std::string("Co-op") : std::string(team ? "Team Deathmatch" : "Deathmatch"),
                scope_ == MpScope::Any);
            add(RowId::HostMode, RowKind::Choice, "Game Mode",
                host_.coop     ? std::string("-")
                : mode         ? mode->name
                : fits.empty() ? std::string("Standard  (no modes for this map)")
                               : std::string("Standard"),
                !host_.coop && !fits.empty());
            add(RowId::HostMax, RowKind::Number, "Maximum Number of Players", std::to_string(host_.maxPlayers));
            // Co-op has no kills, time or teams: the rows stay (the form keeps its shape) but are greyed. A custom mode
            // fixes them as well (shown with the mode's values; the limit counts the mode's points).
            const bool coop = host_.coop;
            const bool fixed = coop || mode;
            add(RowId::HostFrag, RowKind::Number, mode ? "Score Limit" : "Kills Limit",
                coop ? std::string("-") : limitText(host_.fragLimit, ""), !fixed);
            add(RowId::HostTime, RowKind::Number, "Time Limit",
                coop ? std::string("-") : limitText(host_.timeLimitMin, " minutes"), !fixed);
            add(RowId::HostTeams, RowKind::Number, "Number of Teams",
                coop                       ? std::string("-")
                : host_.teams == 0         ? std::string("No Teams")
                : host_.teams > 2 && !mode ? std::to_string(host_.teams) + "  (coming)"
                                           : std::to_string(host_.teams),
                !fixed);
            add(RowId::HostFriendly, RowKind::Toggle, "Friendly Fire?",
                coop   ? "No"
                : team ? "No  (coming)"
                       : "No",
                false);
            add(RowId::HostRegister, RowKind::Toggle, "Register With Master Servers?", "No  (servers are gone)", false);
            add(RowId::HostStart, RowKind::Action, "Begin Game", {},
                net && (coop || host_.teams <= 2 || mode) && !levels.empty() &&
                    levels[static_cast<size_t>(host_.level)].supports(hostMode()));
            add(RowId::HostBack, RowKind::Action, "Back");
            break;
        }
        case MpPage::Join: {
            buildEntries(st);
            const int n = static_cast<int>(entries_.size());
            selectedServer_ = n ? std::clamp(selectedServer_, 0, n - 1) : 0;
            const bool hasSel = n > 0;
            add(RowId::JoinHeader, RowKind::Info, "");
            if (n == 0)
                add(RowId::LobbyInfo, RowKind::Info,
                    st.lanScanning ? "Searching the local network ..." : "No servers found. Refresh, or Add Server.");
            for (int i = listScroll_; i < std::min(n, listScroll_ + kVisibleList); ++i)
                add(RowId::JoinServer, RowKind::List, entries_[static_cast<size_t>(i)].name, {}, true, i);
            add(RowId::JoinRefresh, RowKind::Action, st.lanScanning ? "Refresh (searching ...)" : "Refresh");
            if (addingServer_) {
                add(RowId::JoinNewAddress, RowKind::Text, "Server Address (IP:port)", newAddress_);
            } else {
                add(RowId::JoinAdd, RowKind::Action, "Add Server");
            }
            add(RowId::JoinInfo, RowKind::Action, showInfo_ ? "Hide Server Info" : "Server Info", {}, hasSel);
            add(RowId::JoinPassword, RowKind::Text, "Password",
                joinPassword_.empty() ? std::string("none") : std::string(joinPassword_.size(), '*'));
            add(RowId::JoinConnect, RowKind::Action, "Connect", {}, net && hasSel);
            add(RowId::JoinBack, RowKind::Action, "Back");
            break;
        }
        case MpPage::Lobby: {
            const std::string mode = st.modeName.empty() ? std::string(modeName(st.mode)) : st.modeName;
            add(RowId::LobbyInfo, RowKind::Info,
                (st.serverName.empty() ? std::string("Match") : st.serverName) + "   " + mode + "   " +
                    (st.level.empty() ? std::string("-") : st.level));
            std::string limits =
                st.mode == GameMode::Coop
                    ? "Players " + std::to_string(st.roster.size()) + "/" + std::to_string(st.maxPlayers)
                    : "Kills limit " + limitText(st.fragLimit, "") + "   Time limit " +
                          limitText(st.timeLimitMin, " min") + "   Players " + std::to_string(st.roster.size()) + "/" +
                          std::to_string(st.maxPlayers);
            add(RowId::LobbyInfo, RowKind::Info, limits);
            if (!st.message.empty())
                add(RowId::LobbyInfo, RowKind::Info, st.message);
            int i = 0;
            for (const RosterRow& p : st.roster)
                add(RowId::LobbyPlayer, RowKind::Info, p.name, {}, true, i++);
            const bool lobby = st.conn == Conn::Connected && st.phase == MatchPhaseView::Lobby;
            if (st.mode == GameMode::Coop)
                add(RowId::LobbyCharacter, RowKind::Choice, "Character",
                    selectedCharacter(st).name + (st.hosting ? "  (host)" : ""),
                    lobby && !st.hosting && allowedCharacters(st).size() > 1);
            add(RowId::LobbyReady, RowKind::Toggle, "Ready", st.selfReady ? "Ready" : "Not ready", lobby);
            if (st.hosting)
                add(RowId::LobbyStart, RowKind::Action, "Start Match", {}, lobby && !st.roster.empty());
            if (st.hosting) {
                const std::vector<const RosterRow*> others = [&] {
                    std::vector<const RosterRow*> o;
                    for (const RosterRow& pl : st.roster)
                        if (!pl.self)
                            o.push_back(&pl);
                    return o;
                }();
                kickIdx_ = others.empty() ? 0 : wrapIdx(kickIdx_, static_cast<int>(others.size()));
                add(RowId::LobbyKick, RowKind::Choice, "Kick player",
                    others.empty() ? std::string("nobody else") : others[static_cast<size_t>(kickIdx_)]->name,
                    st.conn == Conn::Connected && !others.empty());
            }
            add(RowId::LobbyLeave, RowKind::Action, "Leave");
            add(RowId::LobbyChat, RowKind::Text, "Chat", chatInput_, lobby);
            break;
        }
    }
    layoutRows(st);
    if (keep)
        for (int i = 0; i < rowCount(); ++i)
            if (rows_[static_cast<size_t>(i)].id == keepId && rows_[static_cast<size_t>(i)].index == keepIndex) {
                focus_ = i;
                break;
            }
    ensureFocus();
    if (mapListMode_ &&
        !(focus_ >= 0 && focus_ < rowCount() && rows_[static_cast<size_t>(focus_)].id == RowId::HostLevel))
        mapListMode_ = false; // the focus left the Map row (pointer, Left / Right ...)
}

int MultiplayerScreen::rowOf(RowId id) const {
    for (int i = 0; i < rowCount(); ++i)
        if (rows_[static_cast<size_t>(i)].id == id)
            return i;
    return -1;
}

// Places every row on the page's design canvas (1920 x 1080): the Menu form left with the character preview right, the
// Host fields in two columns with the map list, the Join table with the details pane, the Lobby roster with the chat.
void MultiplayerScreen::layoutRows(const MpState& st) {
    int infoOrd = 0;
    int colA = 0, colB = 0;
    const bool teamLobby = st.mode == GameMode::TeamDeathmatch;
    for (Row& r : rows_) {
        switch (r.id) {
            case RowId::MenuName:
            case RowId::MenuColour:
            case RowId::MenuHumanSkin:
            case RowId::MenuDragonSkin:
            case RowId::MenuSpeed:
                r.rect = {80, 150 + static_cast<float>(static_cast<int>(r.id) - static_cast<int>(RowId::MenuName)) * 76,
                          900, 64};
                break;
            case RowId::MenuHost:
                r.rect = {80, 548, 900, 80};
                break;
            case RowId::MenuJoin:
                r.rect = {80, 640, 900, 80};
                break;
            case RowId::MenuMaster:
                r.rect = {80, 732, 900, 64};
                break;
            case RowId::MenuBack:
                r.rect = {80, 910, 900, 64};
                break;
            case RowId::HostName:
                r.rect = {80, 288, 540, 52};
                break;
            case RowId::HostPassword:
                r.rect = {80, 348, 540, 52};
                break;
            case RowId::HostGroup:
                r.rect = {80, 408, 540, 52};
                break;
            case RowId::HostLevel:
                r.rect = {80, 468, 540, 522};
                break;
            case RowId::HostType:
            case RowId::HostMode:
            case RowId::HostMax:
            case RowId::HostFrag:
            case RowId::HostTime:
            case RowId::HostTeams:
            case RowId::HostFriendly:
            case RowId::HostRegister: {
                const int k = static_cast<int>(r.id) - static_cast<int>(RowId::HostType);
                // the ids run Type, Mode, Max, Frag, Time, Teams, Friendly, Register
                r.rect = {640, 288 + static_cast<float>(k) * 60, 560, 52};
                break;
            }
            case RowId::HostStart:
                r.rect = {640, 850, 560, 70};
                break;
            case RowId::HostBack:
                r.rect = {640, 930, 560, 60};
                break;
            case RowId::JoinHeader:
                r.rect = {80, 150, 1160, 40};
                break;
            case RowId::JoinServer:
                r.rect = {80, 196 + static_cast<float>(r.index - listScroll_) * 62, 1160, 56};
                break;
            case RowId::JoinRefresh:
                r.rect = {80, 716, 572, 60};
                break;
            case RowId::JoinAdd:
            case RowId::JoinNewAddress:
                r.rect = {668, 716, 572, 60};
                break;
            case RowId::JoinInfo:
                r.rect = {80, 784, 572, 60};
                break;
            case RowId::JoinPassword:
                r.rect = {668, 784, 572, 60};
                break;
            case RowId::JoinConnect:
                r.rect = {80, 856, 572, 76};
                break;
            case RowId::JoinBack:
                r.rect = {668, 856, 572, 76};
                break;
            case RowId::LobbyInfo:
                if (page_ == MpPage::Join)
                    r.rect = {80, 196, 1160, 56};
                else {
                    r.rect = {80, 128 + static_cast<float>(infoOrd) * 30, 1000, 30};
                    ++infoOrd;
                }
                break;
            case RowId::LobbyPlayer: {
                const bool second = teamLobby && r.index < static_cast<int>(st.roster.size()) &&
                                    (st.roster[static_cast<size_t>(r.index)].team & 1) == 1;
                int& ord = second ? colB : colA;
                if (teamLobby)
                    r.rect = {second ? 600.0f : 80.0f, 276 + static_cast<float>(ord) * 56, 500, 50};
                else
                    r.rect = st.mode == GameMode::Coop ? Rect{80, 262 + static_cast<float>(ord) * 50, 1020, 44}
                                                       : Rect{80, 270 + static_cast<float>(ord) * 56, 1020, 50};
                ++ord;
                break;
            }
            case RowId::LobbyChat:
                r.rect = {1156, 770, 668, 48};
                break;
            case RowId::LobbyCharacter:
                r.rect = {430, 912, 650, 60};
                break;
            case RowId::LobbyReady:
                r.rect = {1140, 850, st.hosting ? 340.0f : 700.0f, 64};
                break;
            case RowId::LobbyKick:
                r.rect = {1140, 706, 700, 56};
                break;
            case RowId::LobbyStart:
                r.rect = {1500, 850, 340, 64};
                break;
            case RowId::LobbyLeave:
                r.rect = {1140, 926, 700, 56};
                break;
        }
    }
}

MpAction MultiplayerScreen::baseAction(MpActionKind k, const MpState& st) const {
    MpAction a;
    a.kind = k;
    a.setup = setup_;
    a.host = host_;
    a.host.mode = hostMode();
    const std::vector<LevelEntry> levels = groupLevels(st);
    if (!levels.empty())
        a.level = levels[static_cast<size_t>(wrapIdx(host_.level, static_cast<int>(levels.size())))].name;
    return a;
}

std::string* MultiplayerScreen::textTarget(RowId id) {
    switch (id) {
        case RowId::HostName:
            return &host_.serverName;
        case RowId::HostPassword:
            return &host_.password;
        case RowId::JoinNewAddress:
            return &newAddress_;
        case RowId::JoinPassword:
            return &joinPassword_;
        case RowId::MenuName:
            return &setup_.name;
        case RowId::LobbyChat:
            return &chatInput_;
        default:
            return nullptr;
    }
}

static size_t textLimit(RowId id) {
    switch (id) {
        case RowId::HostName:
            return kServerNameMax;
        case RowId::JoinNewAddress:
            return kAddrMax;
        case RowId::HostPassword:
        case RowId::JoinPassword:
            return kPasswordMax;
        case RowId::LobbyChat:
            return kChatMax;
        default:
            return kNameMax;
    }
}

void MultiplayerScreen::back(const MpState& st, MpFrameOutput& out) {
    out.sound = UiSound::Back;
    if (mapListMode_) {
        mapListMode_ = false;
        return;
    }
    if (editing_) {
        editing_ = false;
        if (addingServer_ && rowCount() > 0 && rows_[static_cast<size_t>(focus_)].id == RowId::JoinNewAddress)
            addingServer_ = false;
        return;
    }
    switch (page_) {
        case MpPage::Menu:
            out.actions.push_back(baseAction(MpActionKind::SaveProfile, st));
            out.wantsClose = true;
            out.actions.push_back(baseAction(MpActionKind::Close, st));
            break;
        case MpPage::Lobby:
            out.actions.push_back(baseAction(MpActionKind::Leave, st));
            setPage(MpPage::Menu, st);
            break;
        case MpPage::Host:
        case MpPage::Join:
            showInfo_ = false;
            addingServer_ = false;
            setPage(MpPage::Menu, st);
            break;
    }
}

void MultiplayerScreen::adjust(int i, int dir, const MpState& st, MpFrameOutput& out) {
    if (i < 0 || i >= rowCount() || dir == 0)
        return;
    const Row& r = rows_[static_cast<size_t>(i)];
    if (!r.enabled)
        return;
    static const int kFragSteps[] = {0, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200};
    static const int kTimeSteps[] = {0, 5, 10, 15, 20, 30, 45, 60};
    static const int kTeamSteps[] = {0, 2, 3, 4};
    const auto step = [&](const int* a, int n, int v) {
        int at = 0;
        for (int k = 0; k < n; ++k)
            if (a[k] == v)
                at = k;
        return a[std::clamp(at + dir, 0, n - 1)];
    };
    bool changed = true;
    switch (r.id) {
        case RowId::HostGroup:
            host_.levelGroup = wrapIdx(host_.levelGroup + dir, 3);
            host_.level = 0;
            levelScroll_ = 0;
            break;
        case RowId::HostLevel:
            host_.level = wrapIdx(host_.level + dir, static_cast<int>(groupLevels(st).size()));
            break;
        case RowId::HostType:
            if (scope_ != MpScope::Any) // the scope fixes the type (Multiplayer: versus; campaign: Co-op)
                break;
            host_.coop = !host_.coop;
            host_.level = 0; // the map list changes (arenas vs campaign levels)
            levelScroll_ = 0;
            host_.mode = host_.coop         ? GameMode::Coop
                         : host_.teams >= 2 ? GameMode::TeamDeathmatch
                                            : GameMode::Deathmatch;
            break;
        case RowId::HostMode: {
            // Standard, then every custom mode the map can play (Left / Right cycle, wrapping around).
            const std::vector<const ModeEntry*> fits = modesForMap(st);
            int at = 0; // 0 = Standard
            for (size_t k = 0; k < fits.size(); ++k)
                if (fits[k]->id == host_.modeId)
                    at = static_cast<int>(k) + 1;
            at = wrapIdx(at + dir, static_cast<int>(fits.size()) + 1);
            if (at == 0) {
                host_.modeId.clear();
                host_.modeTeam = false;
            } else {
                host_.modeId = fits[static_cast<size_t>(at - 1)]->id;
                host_.modeTeam = fits[static_cast<size_t>(at - 1)]->teamBase;
            }
            break;
        }
        case RowId::HostMax:
            host_.maxPlayers = std::clamp(host_.maxPlayers + dir, kMinPlayers, kMaxPlayers);
            break;
        case RowId::HostFrag:
            host_.fragLimit = step(kFragSteps, 16, host_.fragLimit);
            break;
        case RowId::HostTime:
            host_.timeLimitMin = step(kTimeSteps, 8, host_.timeLimitMin);
            break;
        case RowId::HostTeams:
            host_.teams = step(kTeamSteps, 4, host_.teams);
            host_.mode = host_.coop         ? GameMode::Coop
                         : host_.teams >= 2 ? GameMode::TeamDeathmatch
                                            : GameMode::Deathmatch;
            break;
        case RowId::MenuColour:
            setup_.colour = wrapIdx(setup_.colour + dir, static_cast<int>(colourNames().size()));
            break;
        case RowId::MenuHumanSkin:
            setup_.model = wrapIdx(setup_.model + dir, static_cast<int>(modelNames().size()));
            break;
        case RowId::MenuDragonSkin:
            setup_.dragonSkin = wrapIdx(setup_.dragonSkin + dir, static_cast<int>(dragonSkinNames().size()));
            break;
        case RowId::MenuSpeed:
            setup_.speed = wrapIdx(setup_.speed + dir, static_cast<int>(speedNames().size()));
            break;
        case RowId::LobbyCharacter: {
            const std::vector<CharacterEntry> ok = allowedCharacters(st);
            const CharacterEntry cur = selectedCharacter(st);
            size_t at = 0;
            for (size_t k = 0; k < ok.size(); ++k)
                if (ok[k].classId == cur.classId)
                    at = k;
            const CharacterEntry& next =
                ok[static_cast<size_t>(wrapIdx(static_cast<int>(at) + dir, static_cast<int>(ok.size())))];
            setup_.character = next.classId;
            MpAction a = baseAction(MpActionKind::SetCharacter, st);
            a.character = next.classId;
            out.actions.push_back(a);
            break;
        }
        case RowId::LobbyReady: {
            MpAction a = baseAction(MpActionKind::SetReady, st);
            a.flag = !st.selfReady;
            out.actions.push_back(a);
            break;
        }
        case RowId::LobbyKick:
            kickIdx_ += dir;
            break;
        default:
            changed = false;
    }
    if (changed) {
        out.sound = UiSound::Slider;
        const RowId id = r.id;
        buildRows(st);
        for (int k = 0; k < rowCount(); ++k)
            if (rows_[static_cast<size_t>(k)].id == id) {
                focus_ = k;
                break;
            }
    }
}

void MultiplayerScreen::activate(int i, const MpState& st, MpFrameOutput& out) {
    if (!navigable(i))
        return;
    const Row r = rows_[static_cast<size_t>(i)];
    out.sound = UiSound::Confirm;
    const auto joinEntry = [&](const JoinEntry& e) {
        MpAction a = baseAction(MpActionKind::JoinMatch, st);
        a.address = e.address;
        a.password = joinPassword_;
        out.actions.push_back(a);
        showInfo_ = false;
        setPage(MpPage::Lobby, st);
    };
    switch (r.id) {
        case RowId::MenuHost:
            out.actions.push_back(baseAction(MpActionKind::SaveProfile, st));
            setPage(MpPage::Host, st);
            return;
        case RowId::MenuJoin:
            out.actions.push_back(baseAction(MpActionKind::SaveProfile, st));
            setPage(MpPage::Join, st);
            out.actions.push_back(baseAction(MpActionKind::RefreshLan, st));
            return;
        case RowId::MenuBack:
        case RowId::HostBack:
        case RowId::JoinBack:
        case RowId::LobbyLeave:
            back(st, out);
            return;
        case RowId::HostStart:
            out.actions.push_back(baseAction(MpActionKind::HostMatch, st));
            setPage(MpPage::Lobby, st);
            return;
        case RowId::JoinRefresh:
            out.actions.push_back(baseAction(MpActionKind::RefreshLan, st));
            return;
        case RowId::JoinAdd:
            addingServer_ = true;
            buildRows(st);
            for (int k = 0; k < rowCount(); ++k)
                if (rows_[static_cast<size_t>(k)].id == RowId::JoinNewAddress)
                    focus_ = k;
            editing_ = true;
            return;
        case RowId::JoinInfo:
            showInfo_ = !showInfo_;
            return;
        case RowId::JoinServer:
            selectedServer_ = r.index;
            joinEntry(entries_[static_cast<size_t>(r.index)]); // the list row joins (double-click in OotF)
            return;
        case RowId::JoinConnect:
            if (!entries_.empty())
                joinEntry(entries_[static_cast<size_t>(
                    std::clamp(selectedServer_, 0, static_cast<int>(entries_.size()) - 1))]);
            return;
        case RowId::LobbyStart:
            out.actions.push_back(baseAction(MpActionKind::StartMatch, st));
            return;
        case RowId::LobbyKick: {
            int n = 0;
            for (const RosterRow& pl : st.roster) {
                if (pl.self)
                    continue;
                if (n++ == kickIdx_) {
                    MpAction a = baseAction(MpActionKind::KickPlayer, st);
                    a.player = pl.id;
                    out.actions.push_back(a);
                    break;
                }
            }
            return;
        }
        case RowId::HostLevel:
            mapListMode_ = !mapListMode_; // Select enters / leaves the list: Up / Down then step through the maps
            return;
        case RowId::LobbyReady:
            adjust(i, 1, st, out);
            out.sound = UiSound::Toggle;
            return;
        case RowId::HostName:
        case RowId::HostPassword:
        case RowId::JoinNewAddress:
        case RowId::JoinPassword:
        case RowId::MenuName:
        case RowId::LobbyChat:
            editing_ = true;
            return;
        default:
            adjust(i, 1, st, out);
            return;
    }
}

// Up / Down inside the server list moves through all servers, scrolling the visible window; false when the focus is not
// in the list or leaves it (then the ordinary row movement continues).
bool MultiplayerScreen::stepList(int dir, const MpState& st) {
    if (page_ == MpPage::Host && mapListMode_ && focus_ >= 0 && focus_ < rowCount() &&
        rows_[static_cast<size_t>(focus_)].id == RowId::HostLevel) {
        const int n = static_cast<int>(groupLevels(st).size());
        host_.level = std::clamp(host_.level + dir, 0, std::max(0, n - 1));
        buildRows(st);
        return true; // the list holds Up / Down until Select / Back leaves it
    }
    if (page_ != MpPage::Join || focus_ < 0 || focus_ >= rowCount())
        return false;
    const Row& f = rows_[static_cast<size_t>(focus_)];
    if (f.id != RowId::JoinServer)
        return false;
    const int target = f.index + dir, n = static_cast<int>(entries_.size());
    if (target < 0 || target >= n)
        return false;
    if (target < listScroll_)
        listScroll_ = target;
    if (target >= listScroll_ + kVisibleList)
        listScroll_ = target - kVisibleList + 1;
    selectedServer_ = target;
    buildRows(st);
    for (int i = 0; i < rowCount(); ++i)
        if (rows_[static_cast<size_t>(i)].id == RowId::JoinServer && rows_[static_cast<size_t>(i)].index == target)
            focus_ = i;
    return true;
}

void MultiplayerScreen::padEdit(UiKey k) {
    // Gamepad text entry: Up / Down change the last character, Right appends one, Left deletes one.
    std::string* t = textTarget(rows_[static_cast<size_t>(focus_)].id);
    if (!t)
        return;
    const std::string cs = kNameChars;
    const size_t lim = textLimit(rows_[static_cast<size_t>(focus_)].id);
    if (k == UiKey::Right) {
        if (t->size() < lim)
            t->push_back('A');
    } else if (k == UiKey::Left) {
        if (!t->empty())
            t->pop_back();
    } else if (!t->empty()) {
        const size_t at = cs.find(t->back());
        const int n = static_cast<int>(cs.size());
        const int cur = at == std::string::npos ? 0 : static_cast<int>(at);
        t->back() = cs[static_cast<size_t>(wrapIdx(cur + (k == UiKey::Up ? 1 : -1), n))];
    }
}

void MultiplayerScreen::typeChars(const UiInput& in, MpFrameOutput& out) {
    std::string* t = textTarget(rows_[static_cast<size_t>(focus_)].id);
    if (!t)
        return;
    const size_t lim = textLimit(rows_[static_cast<size_t>(focus_)].id);
    for (int i = 0; i < in.backspaces && !t->empty(); ++i)
        t->pop_back();
    for (char c : in.text)
        if (c >= 0x20 && c < 0x7f && t->size() < lim)
            t->push_back(c);
    (void)out;
}

MpFrameOutput MultiplayerScreen::frame(const MpState& st, const UiInput& in, const Viewport& vp) {
    MpFrameOutput out;
    if (!open_)
        return out;
    lf_ = computeFrame(vp);
    // State driven transitions: a rejected / dropped join returns from the lobby to the join page with the reason.
    if (page_ == MpPage::Lobby && st.conn != Conn::Offline && st.conn != Conn::Rejected)
        lobbyActive_ = true;
    if (page_ == MpPage::Lobby && st.conn == Conn::Connected && !lobbyFocused_) {
        lobbyFocused_ = true; // the lobby is up: start on Ready (clients and host alike)
        buildRows(st);
        for (int i = 0; i < rowCount(); ++i)
            if (rows_[static_cast<size_t>(i)].id == RowId::LobbyReady && rows_[static_cast<size_t>(i)].enabled)
                focus_ = i;
    }
    // A joiner whose session character is not one of the characters she may play (the default is Rynn, the host's)
    // takes the first one that is: tell the session once.
    if (page_ == MpPage::Lobby && st.mode == GameMode::Coop && st.conn == Conn::Connected &&
        st.phase == MatchPhaseView::Lobby && !st.hosting && st.selfCharacter >= 0) {
        const CharacterEntry c = selectedCharacter(st);
        if (c.classId != st.selfCharacter && c.classId != autoChar_) {
            autoChar_ = c.classId;
            setup_.character = c.classId;
            MpAction a = baseAction(MpActionKind::SetCharacter, st);
            a.character = c.classId;
            out.actions.push_back(a);
        }
    }
    if (page_ == MpPage::Lobby && (st.conn == Conn::Rejected || (st.conn == Conn::Offline && lobbyActive_))) {
        if (st.conn == Conn::Rejected) // rejected / kicked / failed: the session thread stays until it is told to go
            out.actions.push_back(baseAction(MpActionKind::Leave, st));
        setPage(st.hosting ? MpPage::Host : MpPage::Join, st);
    }
    buildRows(st);

    // Pointer.
    hover_ = -1;
    ptrValid_ = in.pointerValid;
    ptrX_ = in.pointerX;
    ptrY_ = in.pointerY;
    bool consumed = false;
    if (in.pointerValid && in.leftClick && page_ == MpPage::Host && !editing_) {
        for (int m = 0; m < static_cast<int>(GameMode::Count) && !consumed; ++m)
            if (lf_.map(hostCard(m)).contains(in.pointerX, in.pointerY)) {
                consumed = true;
                if (selectMode(static_cast<GameMode>(m), st))
                    out.sound = UiSound::Slider;
            }
        const int hl = rowOf(RowId::HostLevel);
        if (!consumed && hl >= 0 && lf_.map(rows_[static_cast<size_t>(hl)].rect).contains(in.pointerX, in.pointerY)) {
            const Rect lr = rows_[static_cast<size_t>(hl)].rect;
            const float local = (in.pointerY - lf_.map({0, lr.y + 8, 0, 0}).y) / (lf_.scale * (62.0f - 4.0f));
            const int idx = levelScroll_ + static_cast<int>(std::floor(local));
            const int n = static_cast<int>(groupLevels(st).size());
            if (local >= 0 && idx >= 0 && idx < n) {
                host_.level = idx;
                focus_ = hl;
                out.sound = UiSound::Slider;
                buildRows(st);
            }
            consumed = true;
        }
    }
    if (in.pointerValid && !consumed)
        for (int i = rowCount() - 1; i >= 0; --i) {
            const Row& r = rows_[static_cast<size_t>(i)];
            if (r.kind != RowKind::Info && lf_.map(r.rect).contains(in.pointerX, in.pointerY)) {
                hover_ = i;
                if (in.pointerMoved && navigable(i) && !editing_)
                    focus_ = i;
                break;
            }
        }
    if (in.rightClick)
        back(st, out);
    else if (in.leftClick && !consumed) {
        if (hover_ >= 0 && navigable(hover_)) {
            if (editing_ && hover_ != focus_)
                editing_ = false;
            focus_ = hover_;
            activate(hover_, st, out);
        } else if (editing_)
            editing_ = false;
    }
    std::vector<UiKey> keys = in.keys;
    if (in.wheel != 0 && !editing_)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);

    if (editing_) {
        typeChars(in, out);
        for (UiKey k : keys) {
            if (k == UiKey::Confirm || k == UiKey::Back || k == UiKey::Close) {
                const bool commit = k == UiKey::Confirm;
                editing_ = false;
                if (commit && rows_[static_cast<size_t>(focus_)].id == RowId::LobbyChat) {
                    if (!chatInput_.empty()) {
                        MpAction a = baseAction(MpActionKind::SendChat, st);
                        a.text = chatInput_;
                        out.actions.push_back(a);
                        chatInput_.clear();
                    }
                }
                if (addingServer_ && rows_[static_cast<size_t>(focus_)].id == RowId::JoinNewAddress) {
                    // Add Server: OK keeps the address (once), Cancel drops it.
                    if (commit && !newAddress_.empty() &&
                        std::find(custom_.begin(), custom_.end(), newAddress_) == custom_.end())
                        custom_.push_back(newAddress_);
                    addingServer_ = false;
                }
                out.sound = UiSound::Confirm;
                break;
            }
            if (in.device == InputDevice::Gamepad)
                padEdit(k);
        }
    } else {
        for (UiKey k : keys) {
            switch (k) {
                case UiKey::Up:
                    if (!stepList(-1, st))
                        moveFocus(-1);
                    out.sound = UiSound::Move;
                    break;
                case UiKey::Down:
                    if (!stepList(1, st))
                        moveFocus(1);
                    out.sound = UiSound::Move;
                    break;
                case UiKey::Left:
                    adjust(focus_, -1, st, out);
                    break;
                case UiKey::Right:
                    adjust(focus_, 1, st, out);
                    break;
                case UiKey::Confirm:
                    activate(focus_, st, out);
                    break;
                case UiKey::Back:
                case UiKey::Close:
                    back(st, out);
                    break;
                default:
                    break;
            }
            if (!open_ || out.wantsClose)
                break;
        }
    }
    buildRows(st);
    ensureFocus();
    out.wantsText = editing_;

    std::vector<Hint> hints;
    const bool onMapRow = page_ == MpPage::Host && focus_ >= 0 && focus_ < rowCount() &&
                          rows_[static_cast<size_t>(focus_)].id == RowId::HostLevel;
    hints.push_back({Prompt::Confirm, editing_ || mapListMode_ ? "Done" : (onMapRow ? "Browse maps" : "Select"),
                     navigable(focus_)});
    hints.push_back({Prompt::Back, editing_ || mapListMode_ ? "Done" : "Back"});
    draw(st, out.draw, hints);
    return out;
}

// ---- big two-pane pages (design canvas 1920 x 1080) ----

namespace {

constexpr float kM = 80;                      // outer margin
constexpr float kTop = 150;                   // first content y (under the title rule)
constexpr float kBottom = 990;                // last content y (above the footer hints)
constexpr float kCardTop = 140, kCardH = 124; // Host: game mode cards
constexpr float kHostY0 = 288;                // Host: first field row
constexpr float kJoinRowY = 196, kJoinRowStep = 62, kJoinRowH = 56;
constexpr float kListPad = 8, kListItemH = 62;

Rect hostCard(int i) {
    const float gap = 14, w = (kDesignW - 2 * kM - 4 * gap) / 5.0f;
    return {kM + static_cast<float>(i) * (w + gap), kCardTop, w, kCardH};
}
Rect hostList() {
    return {kM, kHostY0 + 180, 540, kBottom - (kHostY0 + 180)};
}
Rect menuPreview() {
    return {1020, kTop, 820, kBottom - kTop};
}
Rect hostPreview() {
    return {1220, kHostY0, 620, kBottom - kHostY0};
}
Rect joinPreview() {
    return {1280, kTop, 560, kBottom - kTop};
}
Rect lobbyPreview() {
    return {1140, kTop, 700, 330};
}

Color dragonSkinRgba(int i) {
    static const Color c[] = {rgba(190, 48, 40),  rgba(70, 150, 84), rgba(196, 166, 106), rgba(150, 156, 164),
                              rgba(64, 116, 200), rgba(36, 34, 40),  rgba(228, 226, 220)};
    return c[wrapIdx(i, 7)];
}

Color teamRgba(int team) {
    static const Color c[] = {rgba(206, 70, 40), rgba(70, 130, 220), rgba(76, 170, 90), rgba(210, 190, 230)};
    return c[team & 3];
}
const char* teamName(int team) {
    static const char* const n[] = {"Fire", "Water", "Earth", "Air"};
    return n[team & 3];
}

void pxLine(DrawList& d, const detail::Paint& p, float x0, float y0, float x1, float y1, Color c, float thick) {
    const Rect a = p.map({x0, y0, 0, 0}), b = p.map({x1, y1, 0, 0});
    d.line({a.x, a.y}, {b.x, b.y}, c, p.px(thick));
}

uint32_t fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

Color mix(Color a, Color b, float t) {
    const auto ch = [&](int sh) {
        const float x = static_cast<float>((a >> sh) & 255), y = static_cast<float>((b >> sh) & 255);
        return static_cast<uint32_t>(std::clamp(x + (y - x) * t, 0.0f, 255.0f));
    };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// A generated overhead sketch of a map: layered ridgelines in a biome tint chosen from the map's name, with the spawn
// points marked. It is the stand-in for the map's real picture (ImageKind::LevelPreview), drawn underneath it; nothing
// from the disc is stored.
void drawMapArt(DrawList& d, const detail::Paint& p, const Rect& r, const std::string& name, int spawns, double now) {
    const Theme& th = p.th();
    uint32_t h = fnv(name);
    const auto rnd = [&h]() {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        return static_cast<float>(h & 0xffffff) / 16777216.0f;
    };
    static const Color kFar[] = {rgba(122, 142, 108), rgba(176, 150, 106), rgba(134, 158, 188), rgba(170, 98, 62),
                                 rgba(112, 100, 138)};
    static const Color kNear[] = {rgba(20, 34, 22), rgba(46, 30, 16), rgba(18, 28, 44), rgba(40, 14, 10),
                                  rgba(22, 16, 34)};
    const unsigned biome = fnv(name) % 5;
    const Rect px = p.map(r);
    d.gradient(px, mix(kFar[biome], rgba(255, 244, 220), 0.35f), mix(kFar[biome], kNear[biome], 0.5f));
    d.pushClip(px);
    const int layers = 8;
    const float step = 5;
    for (int l = 0; l < layers; ++l) {
        const float t = static_cast<float>(l) / static_cast<float>(layers - 1);
        const float a1 = 14 + rnd() * 30, f1 = 0.006f + rnd() * 0.01f, p1 = rnd() * 6.28f;
        const float a2 = 5 + rnd() * 12, f2 = 0.02f + rnd() * 0.03f, p2 = rnd() * 6.28f;
        const float baseY = r.y + r.h * (0.22f + 0.1f * static_cast<float>(l));
        const Color fill = mix(kFar[biome], kNear[biome], 0.15f + 0.8f * t);
        const Color edge = mix(fill, rgba(255, 240, 200), 0.28f);
        float py = 0;
        for (float x = 0; x < r.w; x += step) {
            const float y = baseY + a1 * std::sin(f1 * x * 2.0f + p1) + a2 * std::sin(f2 * x + p2);
            d.rect(p.map({r.x + x, y, step + 0.6f, r.bottom() - y}), fill);
            if (x > 0)
                pxLine(d, p, r.x + x - step, py, r.x + x, y, edge, 1.5f);
            py = y;
        }
    }
    for (float gx = 0; gx <= r.w; gx += 62)
        pxLine(d, p, r.x + gx, r.y, r.x + gx, r.bottom(), rgba(255, 255, 255, 14), 1);
    for (float gy = 0; gy <= r.h; gy += 62)
        pxLine(d, p, r.x, r.y + gy, r.right(), r.y + gy, rgba(255, 255, 255, 14), 1);
    const int n = std::clamp(spawns > 0 ? spawns : 6, 2, 16);
    for (int k = 0; k < n; ++k) {
        const float sx = r.x + 40 + rnd() * (r.w - 80), sy = r.y + r.h * 0.35f + rnd() * (r.h * 0.55f - 20);
        const float pulse = 1.0f + 0.15f * static_cast<float>(std::sin(now * 2.0 + k));
        const float rad = 7 * pulse;
        d.rect(p.map({sx - 3, sy - 3, 6, 6}), th.gilt, 3 * p.lf().scale);
        d.outline(p.map({sx - rad, sy - rad, rad * 2, rad * 2}), withAlpha(th.gilt, 200), 1.5f, rad * p.lf().scale);
    }
    d.popClip();
    d.rect({px.x, px.bottom() - p.px(30), px.w, p.px(30)}, rgba(0, 0, 0, 120));
}

// Frame, picture and caption of a map preview.
void drawMapCard(DrawList& d, const detail::Paint& p, const Rect& r, const LevelEntry& e, double now) {
    const Theme& th = p.th();
    drawMapArt(d, p, r, e.name.empty() ? std::string("none") : e.name, e.spawns, now);
    if (e.previewId)
        d.image(p.map(r), ImageRef{ImageKind::LevelPreview, e.previewId});
    d.outline(p.map(r), th.gilt, 2.0f);
    d.outline(p.map(r.inset(-4)), withAlpha(th.brassDim, 220), 1.5f);
    p.label(d, {r.x + 14, r.bottom() - 30, r.w - 28, 30}, e.previewId ? "MAP PREVIEW" : "OVERHEAD SKETCH",
            withAlpha(th.ivory, 220), TextAlign::Left, 14);
}

// A hooded fighter in the player's colour (a stylised stand-in until models can be drawn in the menu), idle sway.
void drawFigure(DrawList& d, const detail::Paint& p, const Rect& r, Color cloak, int skin, double now) {
    const Theme& th = p.th();
    const float s = p.lf().scale;
    const float sway = 4.0f * static_cast<float>(std::sin(now * 1.4));
    const float cx = r.cx() + sway * 0.4f, base = r.bottom() - 30;
    const float u = r.h / 10.0f; // the figure is about ten units high
    const Color dark = mix(cloak, rgba(0, 0, 0), 0.55f), light = mix(cloak, rgba(255, 240, 210), 0.3f);
    const Color leather = skin % 2 ? rgba(88, 62, 40) : rgba(64, 52, 46);
    d.rect(p.map({cx - u * 2.4f, base - 10, u * 4.8f, 22}), rgba(0, 0, 0, 120), 11 * s);
    d.rect(p.map({cx - u * 0.95f, base - u * 3.1f, u * 0.8f, u * 3.1f}), leather, 6 * s);
    d.rect(p.map({cx + u * 0.15f, base - u * 3.1f, u * 0.8f, u * 3.1f}), leather, 6 * s);
    d.rect(p.map({cx - u * 1.7f + sway * 0.3f, base - u * 7.2f, u * 3.4f, u * 4.6f}), dark, 14 * s);
    d.rect(p.map({cx - u * 1.15f, base - u * 7.0f, u * 2.3f, u * 3.1f}), light, 12 * s);
    d.rect(p.map({cx - u * 1.15f, base - u * 4.5f, u * 2.3f, u * 0.4f}), rgba(40, 30, 20), 3 * s);
    d.outline(p.map({cx - u * 1.15f, base - u * 7.0f, u * 2.3f, u * 3.1f}), withAlpha(th.gilt, 160), 1.5f, 12 * s);
    d.rect(p.map({cx - u * 1.7f, base - u * 6.8f, u * 0.6f, u * 2.4f}), cloak, 8 * s);
    d.rect(p.map({cx + u * 1.1f, base - u * 6.8f, u * 0.6f, u * 2.4f}), cloak, 8 * s);
    d.rect(p.map({cx - u * 0.75f, base - u * 9.2f, u * 1.5f, u * 1.7f}), dark, 20 * s);
    d.rect(p.map({cx - u * 0.52f, base - u * 8.9f, u * 1.04f, u * 1.15f}), rgba(214, 170, 132), 14 * s);
    pxLine(d, p, cx + u * 1.5f, base - u * 4.3f, cx + u * 1.85f + sway * 0.2f, base - u * 8.4f, rgba(212, 218, 226), 4);
    pxLine(d, p, cx + u * 1.3f, base - u * 4.9f, cx + u * 1.75f, base - u * 4.9f, th.gilt, 4);
}

} // namespace

void MultiplayerScreen::draw(const MpState& st, DrawList& d, const std::vector<Hint>& hints) const {
    const ScreenConfig& sc = cfg_.screen;
    const detail::Paint p(lf_, sc);
    const Theme& th = sc.theme;
    const Rect view{lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h};
    d.rect(view, withAlpha(th.backdropEdge, 215));
    d.gradient(view, withAlpha(th.panelRaised, 70), withAlpha(rgba(60, 14, 8), 90));

    // Header: page title, the status / error line, the gilded rule with the crest.
    const char* title = page_ == MpPage::Menu   ? "Multiplayer"
                        : page_ == MpPage::Host ? "Host Game"
                        : page_ == MpPage::Join ? "Join Game"
                                                : "Lobby";
    p.text(d, {kM, 30, 900, 84}, title, FontRole::Title, th.heading, TextAlign::Left, 64);
    {
        const std::string msg =
            !st.available ? (st.unavailableWhy.empty() ? std::string("Networking is not available in this build.")
                                                       : st.unavailableWhy)
                          : (page_ != MpPage::Lobby ? st.message : std::string());
        if (!msg.empty())
            p.text(d, {980, 50, kDesignW - kM - 980, 44}, msg, FontRole::Body, th.bad, TextAlign::Right, 26);
    }
    p.ornateRule(d, p.map({kM, 0, 0, 0}).x, p.map({kDesignW - kM, 0, 0, 0}).x, p.map({0, 122, 0, 0}).y, true);

    // ---- shared row painters ----
    const auto field = [&](int i, float labelW, float size) {
        const Row& r = rows_[static_cast<size_t>(i)];
        const bool foc = i == focus_, hov = i == hover_ && r.enabled;
        d.panel(p.map(r.rect), PanelStyle::Card,
                !r.enabled ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
        if (foc)
            p.focus(d, p.map(r.rect));
        const Color lc = !r.enabled ? th.textDisabled : (foc ? th.heading : th.text);
        p.text(d, {r.rect.x + 22, r.rect.y, labelW, r.rect.h}, r.label, FontRole::Body, lc, TextAlign::Left, size);
        std::string v = r.value;
        if (r.kind == RowKind::Text && foc && editing_)
            v += (static_cast<long long>(st.now * 2) & 1) ? "_" : " ";
        const bool arrows = foc && !editing_ && r.enabled && r.kind != RowKind::Text;
        if (arrows)
            v = "<  " + v + "  >";
        Rect vr{r.rect.x + 22 + labelW, r.rect.y, r.rect.w - 44 - labelW, r.rect.h};
        if (r.id == RowId::MenuColour) {
            const Rect sw{vr.right() - 56, r.rect.y + 10, 56, r.rect.h - 20};
            d.rect(p.map(sw), colourRgba(setup_.colour), 6);
            d.outline(p.map(sw), th.gilt, 2.0f, 6);
            vr.w -= 74;
        }
        const bool placeholder =
            r.value.find("coming") != std::string::npos || r.value.find("gone") != std::string::npos;
        p.text(d, vr, v, FontRole::Body,
               !r.enabled        ? th.textDisabled
               : editing_ && foc ? th.ember
               : placeholder     ? th.textMuted
                                 : (foc ? th.heading : th.brass),
               TextAlign::Right, size);
    };
    const auto button = [&](int i, float size) {
        const Row& r = rows_[static_cast<size_t>(i)];
        const bool foc = i == focus_, hov = i == hover_ && r.enabled;
        d.panel(p.map(r.rect), PanelStyle::Card,
                !r.enabled ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
        if (foc)
            p.focus(d, p.map(r.rect));
        p.text(d, r.rect, r.label, FontRole::Heading, !r.enabled ? th.textDisabled : (foc ? th.heading : th.text),
               TextAlign::Center, size);
    };
    const auto lineWrapped = [&](const Rect& box, const std::string& text, FontRole f, float size, Color c,
                                 int maxLines, float lineH) {
        const std::vector<std::string> ls = p.wrap(text, box.w, f, size);
        float y = box.y;
        for (size_t k = 0; k < ls.size() && static_cast<int>(k) < maxLines; ++k, y += lineH)
            p.text(d, {box.x, y, box.w, lineH}, ls[k], f, c, TextAlign::Left, size);
    };
    const auto findMap = [&](const std::string& name) {
        LevelEntry e;
        for (const std::vector<LevelEntry>* src : {&st.arenas, &st.levels})
            for (const LevelEntry& x : *src)
                if (x.name == name)
                    return x;
        if (st.levels.empty())
            for (const LevelEntry& x : vanillaLevels())
                if (x.name == name)
                    return x;
        e.name = name;
        e.title = name;
        return e;
    };
    const auto modeList = [&](const LevelEntry& e) {
        std::string s;
        for (int m = 0; m < static_cast<int>(GameMode::Count); ++m) {
            const GameMode gm = static_cast<GameMode>(m);
            if (!modeAvailable(gm) || !e.supports(gm))
                continue;
            s += (s.empty() ? "" : ", ") + std::string(modeName(gm));
        }
        return s.empty() ? std::string("-") : s;
    };
    const auto statLine = [&](float x, float y, float w, const char* k, const std::string& v, Color vc) {
        p.label(d, {x, y, 180, 32}, k, th.brass, TextAlign::Left, 15);
        p.text(d, {x + 190, y, w - 190, 32}, v, FontRole::Body, vc, TextAlign::Left, 22);
    };

    switch (page_) {
        case MpPage::Menu: {
            // Left: the form and the buttons; right: the character preview.
            for (int i = 0; i < rowCount(); ++i) {
                const Row& r = rows_[static_cast<size_t>(i)];
                if (r.id == RowId::MenuHost || r.id == RowId::MenuJoin || r.id == RowId::MenuMaster ||
                    r.id == RowId::MenuBack)
                    button(i, 34);
                else
                    field(i, 330, 26);
            }
            const Rect pv = menuPreview();
            d.panel(p.map(pv), PanelStyle::Main, withAlpha(th.panel, 226));
            const Rect stage{pv.x + 40, pv.y + 70, pv.w - 80, pv.h - 250};
            d.gradient(p.map(stage), mix(colourRgba(setup_.colour), rgba(0, 0, 0), 0.72f), rgba(10, 6, 4));
            d.outline(p.map(stage), th.brassDim, 2.0f);
            d.image(p.map({stage.cx() - 260, stage.cy() - 260, 520, 520}), ImageRef{ImageKind::SkinOrb, 0},
                    withAlpha(colourRgba(setup_.colour), 170));
            drawFigure(d, p, stage.inset(30), colourRgba(setup_.colour), setup_.model, st.now);
            p.label(d, {pv.x + 40, pv.y + 22, pv.w - 80, 34}, "YOUR CHARACTER", th.brass, TextAlign::Left, 18);
            const std::string nm = setup_.name.empty() ? std::string("Rynn") : setup_.name;
            p.text(d, {pv.x + 40, pv.bottom() - 170, pv.w - 280, 64}, nm, FontRole::Title, th.heading, TextAlign::Left,
                   54);
            p.text(d, {pv.x + 40, pv.bottom() - 104, pv.w - 280, 40},
                   modelNames()[static_cast<size_t>(wrapIdx(setup_.model, static_cast<int>(modelNames().size())))] +
                       "   " + colourNames()[static_cast<size_t>(wrapIdx(setup_.colour, 8))],
                   FontRole::Body, th.textMuted, TextAlign::Left, 26);
            // Dragon skin chip.
            const Rect dc{pv.right() - 220, pv.bottom() - 170, 180, 110};
            d.panel(p.map(dc), PanelStyle::Inset, withAlpha(th.inset, 220));
            const Color dg = dragonSkinRgba(setup_.dragonSkin);
            d.rect(p.map({dc.x + 36, dc.y + 36, 108, 34}), dg, 17 * lf_.scale);
            d.rect(p.map({dc.x + 118, dc.y + 24, 38, 24}), mix(dg, rgba(0, 0, 0), 0.2f), 12 * lf_.scale);
            pxLine(d, p, dc.x + 60, dc.y + 38, dc.x + 30, dc.y + 8, mix(dg, rgba(255, 255, 255), 0.2f), 5);
            pxLine(d, p, dc.x + 90, dc.y + 38, dc.x + 100, dc.y + 6, mix(dg, rgba(255, 255, 255), 0.2f), 5);
            p.text(d, {dc.x + 6, dc.y + 76, dc.w - 12, 30},
                   dragonSkinNames()[static_cast<size_t>(wrapIdx(setup_.dragonSkin, 7))], FontRole::Small, th.text,
                   TextAlign::Center, 20);
            break;
        }
        case MpPage::Host: {
            // Mode cards across the top.
            static const char* const kDesc[] = {"Free for all. Most kills wins.", "Two teams. The team total wins.",
                                                "Dragon combat in the air.", "Hold the dragon soul crystal.",
                                                "Fight through the campaign together."};
            for (int m = 0; m < static_cast<int>(GameMode::Count); ++m) {
                const GameMode gm = static_cast<GameMode>(m);
                const Rect c = hostCard(m);
                const bool sel = gm == hostMode(), ok = modeAvailable(gm) && scopeAllows(scope_, gm);
                const bool hov = ptrValid_ && p.map(c).contains(ptrX_, ptrY_);
                d.panel(p.map(c), PanelStyle::Card,
                        !ok ? withAlpha(th.panel, 120) : (sel || hov ? th.panelRaised : th.panel));
                if (sel)
                    d.outline(p.map(c), th.gilt, 3.0f, 4);
                const float ix = c.x + 50, iy = c.y + c.h * 0.5f;
                const Color ic = !ok ? th.textDisabled : (sel ? th.heading : th.brass);
                switch (gm) {
                    case GameMode::Deathmatch:
                        pxLine(d, p, ix - 24, iy + 24, ix + 24, iy - 24, ic, 5);
                        pxLine(d, p, ix + 24, iy + 24, ix - 24, iy - 24, ic, 5);
                        break;
                    case GameMode::TeamDeathmatch:
                    case GameMode::Coop:
                        d.outline(p.map({ix - 30, iy - 18, 36, 36}), ic, 4.0f, 18 * lf_.scale);
                        d.outline(p.map({ix - 6, iy - 18, 36, 36}), ic, 4.0f, 18 * lf_.scale);
                        break;
                    case GameMode::DragonsDuel:
                        for (int k = -2; k <= 2; ++k)
                            pxLine(d, p, ix, iy + 18, ix + static_cast<float>(k) * 14, iy - 26, ic, 4);
                        break;
                    default:
                        pxLine(d, p, ix, iy - 28, ix + 22, iy, ic, 4);
                        pxLine(d, p, ix + 22, iy, ix, iy + 28, ic, 4);
                        pxLine(d, p, ix, iy + 28, ix - 22, iy, ic, 4);
                        pxLine(d, p, ix - 22, iy, ix, iy - 28, ic, 4);
                        break;
                }
                p.text(d, {c.x + 96, c.y + 14, c.w - 108, 44}, modeName(gm), FontRole::Heading,
                       !ok ? th.textDisabled : (sel ? th.heading : th.text), TextAlign::Left, 24);
                if (ok)
                    lineWrapped({c.x + 96, c.y + 58, c.w - 108, 24}, kDesc[m], FontRole::Small, 18, th.textMuted, 2,
                                24);
                else
                    p.text(d, {c.x + 96, c.y + 62, c.w - 108, 26}, "Coming", FontRole::Small, th.textDisabled,
                           TextAlign::Left, 20);
            }
            // Fields.
            for (int i = 0; i < rowCount(); ++i) {
                const Row& r = rows_[static_cast<size_t>(i)];
                if (r.id == RowId::HostLevel)
                    continue;
                if (r.id == RowId::HostStart || r.id == RowId::HostBack)
                    button(i, 30);
                else
                    field(i, r.rect.x < 600 ? 190.0f : 330.0f, 22);
            }
            // Map list.
            const std::vector<LevelEntry> lv = groupLevels(st);
            const int n = static_cast<int>(lv.size());
            const Rect lr = hostList();
            const int hi = rowOf(RowId::HostLevel);
            const bool lfoc = hi >= 0 && hi == focus_;
            d.panel(p.map(lr), PanelStyle::Inset, withAlpha(th.inset, 215));
            if (lfoc)
                p.focus(d, p.map(lr));
            if (lfoc && mapListMode_)
                d.outline(p.map(lr), th.select, 4.0f, 6);
            const int cur = n ? wrapIdx(host_.level, n) : 0;
            for (int i = levelScroll_; i < std::min(n, levelScroll_ + kVisibleMaps); ++i) {
                const Rect ir{lr.x + kListPad,
                              lr.y + kListPad + static_cast<float>(i - levelScroll_) * (kListItemH - 4),
                              lr.w - 2 * kListPad - 14, kListItemH - 8};
                const LevelEntry& e = lv[static_cast<size_t>(i)];
                const bool sel = i == cur;
                d.panel(p.map(ir), sel ? PanelStyle::Card : PanelStyle::Inset,
                        sel ? th.panelRaised : withAlpha(th.inset, 150));
                if (sel)
                    d.outline(p.map(ir), lfoc ? th.select : th.gilt, 2.0f, 4);
                const bool supp = e.supports(hostMode());
                p.text(d, {ir.x + 16, ir.y, ir.w - (e.game.empty() ? 140 : 270), ir.h}, e.title, FontRole::Body,
                       !supp ? th.textDisabled : (sel ? th.heading : th.text), TextAlign::Left, 24);
                if (!e.game.empty()) // which game the map comes from
                    p.label(d, {ir.right() - 250, ir.y, 110, ir.h}, e.game, th.brass, TextAlign::Right, 14);
                if (e.custom)
                    p.label(d, {ir.right() - 120, ir.y, 108, ir.h}, "CUSTOM", th.ember, TextAlign::Right, 14);
                else if (e.maxPlayers > 0)
                    p.text(d, {ir.right() - 100, ir.y, 88, ir.h}, std::to_string(e.maxPlayers) + " max",
                           FontRole::Small, th.textMuted, TextAlign::Right, 18);
            }
            if (n > kVisibleMaps) { // scroll bar
                const float bh = (lr.h - 16) * static_cast<float>(kVisibleMaps) / static_cast<float>(n);
                const float by = lr.y + 8 +
                                 (lr.h - 16 - bh) * static_cast<float>(levelScroll_) /
                                     static_cast<float>(std::max(1, n - kVisibleMaps));
                d.rect(p.map({lr.right() - 14, by, 6, bh}), withAlpha(th.gilt, 200), 3);
            }
            if (n == 0)
                p.text(d, lr, "No maps", FontRole::Body, th.textMuted, TextAlign::Center, 26);
            // Rules summary under the settings column.
            {
                std::string rules;
                if (host_.coop)
                    rules = "Co-op campaign for up to " + std::to_string(host_.maxPlayers) + " players.";
                else
                    rules = "First to " +
                            (host_.fragLimit > 0
                                 ? std::to_string(host_.fragLimit) + (host_.modeId.empty() ? " kills" : " points")
                                 : std::string("no limit")) +
                            (host_.timeLimitMin > 0 ? " or " + std::to_string(host_.timeLimitMin) + " minutes"
                                                    : std::string()) +
                            (host_.teams >= 2 ? ", " + std::to_string(host_.teams) + " teams." : ".");
                lineWrapped({640, 774, 560, 28}, rules, FontRole::Body, 22, th.textMuted, 2,
                            30); // below the last row (ends 760)
            }
            // Preview pane.
            const LevelEntry me = hostMap(st);
            const Rect pv = hostPreview();
            drawMapCard(d, p, {pv.x, pv.y, pv.w, 349}, me, st.now);
            if (!me.name.empty()) {
                p.text(d, {pv.x, pv.y + 362, pv.w, 50}, me.title, FontRole::Title, th.heading, TextAlign::Left, 38);
                const std::string desc = !me.description.empty() ? me.description
                                         : host_.coop            ? std::string("A campaign level, played together.")
                                         : me.arena
                                             ? std::string("A multiplayer arena.")
                                             : std::string("A campaign level used as a deathmatch map until dedicated "
                                                           "arenas are available.");
                lineWrapped({pv.x, pv.y + 414, pv.w, 28}, desc, FontRole::Small, 21, th.textMuted, 3, 28);
                statLine(pv.x, pv.y + 506, pv.w, "PLAYERS",
                         me.maxPlayers > 0 ? "up to " + std::to_string(std::min(me.maxPlayers, host_.maxPlayers)) +
                                                 " (map supports " + std::to_string(me.maxPlayers) + ")"
                                           : "up to " + std::to_string(host_.maxPlayers),
                         th.text);
                statLine(pv.x, pv.y + 540, pv.w, "MODES", modeList(me), th.text);
                statLine(pv.x, pv.y + 574, pv.w, "SPAWNS", me.spawns > 0 ? std::to_string(me.spawns) : std::string("-"),
                         th.text);
                if (!me.game.empty() && !me.custom)
                    statLine(pv.x, pv.y + 608, pv.w, "GAME", me.game, th.brass);
                if (me.custom)
                    statLine(pv.x, pv.y + 608, pv.w, "FROM",
                             (me.author.empty() ? std::string("mod pack") : me.author) +
                                 (me.game.empty() ? std::string() : "  (" + me.game + ")"),
                             th.ember);
                if (!me.supports(hostMode()))
                    p.text(d, {pv.x, pv.y + 646, pv.w, 34},
                           std::string("This map does not support ") + modeName(hostMode()) + ".", FontRole::Body,
                           th.bad, TextAlign::Left, 22);
            }
            break;
        }
        case MpPage::Join: {
            d.panel(p.map({kM - 10, kTop - 6, 1180, 560}), PanelStyle::Main, withAlpha(th.panel, 226));
            for (int i = 0; i < rowCount(); ++i) {
                const Row& r = rows_[static_cast<size_t>(i)];
                const bool foc = i == focus_, hov = i == hover_ && r.enabled;
                if (r.id == RowId::JoinHeader) {
                    const float x = r.rect.x;
                    p.label(d, {x + 22, r.rect.y, 300, r.rect.h}, "SERVER NAME", th.brass, TextAlign::Left, 16);
                    p.label(d, {x + 330, r.rect.y, 230, r.rect.h}, "MAP", th.brass, TextAlign::Left, 16);
                    p.label(d, {x + 570, r.rect.y, 190, r.rect.h}, "MODE", th.brass, TextAlign::Left, 16);
                    p.label(d, {x + 770, r.rect.y, 110, r.rect.h}, "PLAYERS", th.brass, TextAlign::Center, 16);
                    p.label(d, {x + 890, r.rect.y, 80, r.rect.h}, "PING", th.brass, TextAlign::Right, 16);
                    p.label(d, {x + 990, r.rect.y, 150, r.rect.h}, "MODS", th.brass, TextAlign::Left, 16);
                    p.rule(d, r.rect.x, r.rect.bottom(), r.rect.w);
                } else if (r.id == RowId::JoinServer) {
                    const JoinEntry& s = entries_[static_cast<size_t>(r.index)];
                    const bool selected = r.index == selectedServer_;
                    d.panel(p.map(r.rect), PanelStyle::Card, hov || selected ? th.panelRaised : th.panel);
                    if (foc)
                        p.focus(d, p.map(r.rect));
                    else if (selected)
                        d.outline(p.map(r.rect), th.gilt, 2.0f, 4);
                    const Color c = foc ? th.good : (s.custom ? th.text : rgba(122, 172, 232));
                    const float x = r.rect.x;
                    p.text(d, {x + 22, r.rect.y, 300, r.rect.h}, s.name + (s.locked ? "  [lock]" : ""), FontRole::Body,
                           c, TextAlign::Left, 24);
                    p.text(d, {x + 330, r.rect.y, 230, r.rect.h}, s.known ? s.level : "-", FontRole::Small,
                           th.textMuted, TextAlign::Left, 20);
                    p.text(d, {x + 570, r.rect.y, 190, r.rect.h}, s.known ? modeName(s.mode) : "-", FontRole::Small,
                           th.text, TextAlign::Left, 20);
                    p.text(d, {x + 770, r.rect.y, 110, r.rect.h},
                           s.known ? std::to_string(s.players) + "/" + std::to_string(s.maxPlayers) : "-",
                           FontRole::Body, th.text, TextAlign::Center, 22);
                    p.text(d, {x + 890, r.rect.y, 80, r.rect.h}, s.known ? std::to_string(s.pingMs) : "-",
                           FontRole::Small, th.textMuted, TextAlign::Right, 20);
                    p.text(d, {x + 990, r.rect.y, 150, r.rect.h}, s.mods.empty() ? "-" : s.mods, FontRole::Small,
                           th.textMuted, TextAlign::Left, 18);
                } else if (r.id == RowId::LobbyInfo) {
                    p.text(d, {r.rect.x + 22, r.rect.y, r.rect.w - 44, r.rect.h}, r.label, FontRole::Body, th.textMuted,
                           TextAlign::Left, 26);
                } else if (r.kind == RowKind::Action) {
                    button(i, r.id == RowId::JoinConnect ? 32.0f : 26.0f);
                } else {
                    field(i, 150, 22);
                }
            }
            p.text(d, {kM, 944, 800, 32}, "Available Servers: " + std::to_string(entries_.size()), FontRole::Small,
                   th.textMuted, TextAlign::Left, 20);
            const Rect pv = joinPreview();
            if (!entries_.empty()) {
                const JoinEntry& e = entries_[static_cast<size_t>(
                    std::clamp(selectedServer_, 0, static_cast<int>(entries_.size()) - 1))];
                const LevelEntry me = findMap(e.known ? e.level : std::string());
                drawMapCard(d, p, {pv.x, pv.y, pv.w, 315}, me, st.now);
                p.text(d, {pv.x, pv.y + 326, pv.w, 46}, e.name, FontRole::Title, th.heading, TextAlign::Left, 34);
                const float y = pv.y + 380;
                statLine(pv.x, y, pv.w, "ADDRESS", e.address, th.text);
                statLine(pv.x, y + 36, pv.w, "MAP", e.known ? (me.title.empty() ? e.level : me.title) : "-", th.text);
                statLine(pv.x, y + 72, pv.w, "MODE", e.known ? modeName(e.mode) : "-", th.text);
                statLine(pv.x, y + 108, pv.w, "PLAYERS",
                         e.known ? std::to_string(e.players) + " / " + std::to_string(e.maxPlayers) : "-", th.text);
                statLine(pv.x, y + 144, pv.w, "PING", e.known ? std::to_string(e.pingMs) + " ms" : "-", th.text);
                statLine(pv.x, y + 180, pv.w, "PASSWORD", e.locked ? "required" : "none",
                         e.locked ? th.ember : th.text);
                statLine(pv.x, y + 216, pv.w, "MODS", e.mods.empty() ? std::string("none") : e.mods, th.text);
                if (showInfo_ && !me.description.empty())
                    lineWrapped({pv.x, y + 258, pv.w, 26}, me.description, FontRole::Small, 19, th.textMuted, 3, 26);
            } else {
                d.panel(p.map(pv), PanelStyle::Inset, withAlpha(th.inset, 200));
                p.text(d, pv, st.lanScanning ? "Searching ..." : "No server selected", FontRole::Heading, th.textMuted,
                       TextAlign::Center, 30);
            }
            break;
        }
        case MpPage::Lobby: {
            const bool team = st.mode == GameMode::TeamDeathmatch;
            int infoOrd = 0;
            for (int i = 0; i < rowCount(); ++i) {
                const Row& r = rows_[static_cast<size_t>(i)];
                switch (r.id) {
                    case RowId::LobbyInfo:
                        p.text(d, r.rect, r.label, FontRole::Body,
                               infoOrd == 2 ? th.ember : (infoOrd == 0 ? th.text : th.textMuted), TextAlign::Left,
                               infoOrd == 0 ? 26.0f : 22.0f);
                        ++infoOrd;
                        break;
                    case RowId::LobbyPlayer: {
                        const RosterRow& pl = st.roster[static_cast<size_t>(r.index)];
                        d.panel(p.map(r.rect), pl.self ? PanelStyle::Card : PanelStyle::Inset,
                                pl.self ? th.panelRaised : withAlpha(th.inset, 200));
                        if (pl.self)
                            d.outline(p.map(r.rect), th.gilt, 2.0f, 4);
                        const Color sw = pl.colour >= 0 ? colourRgba(pl.colour)
                                         : pl.team >= 0 ? teamRgba(pl.team)
                                                        : colourRgba(pl.id);
                        d.rect(p.map({r.rect.x + 12, r.rect.y + 9, 14, r.rect.h - 18}), sw, 3);
                        p.text(d, {r.rect.x + 40, r.rect.y, r.rect.w - 220, r.rect.h},
                               pl.name + (pl.self ? "  (you)" : "") + (pl.host ? "  host" : "") +
                                   (pl.characterName.empty() ? std::string() : "   as " + pl.characterName),
                               FontRole::Body, pl.self ? th.heading : th.text, TextAlign::Left, 24);
                        if (pl.pingMs >= 0 && r.rect.w > 600)
                            p.text(d, {r.rect.right() - 340, r.rect.y, 100, r.rect.h},
                                   std::to_string(pl.pingMs) + " ms", FontRole::Small, th.textMuted, TextAlign::Right,
                                   18);
                        const Rect pill{r.rect.right() - 128, r.rect.y + 8, 116, r.rect.h - 16};
                        d.rect(p.map(pill), pl.ready ? withAlpha(th.good, 70) : withAlpha(rgba(0, 0, 0), 110),
                               6 * lf_.scale);
                        d.outline(p.map(pill), pl.ready ? th.good : th.brassDim, 1.5f, 6 * lf_.scale);
                        p.text(d, pill, pl.ready ? "READY" : "WAITING", FontRole::Small,
                               pl.ready ? th.good : th.textMuted, TextAlign::Center, 16);
                        break;
                    }
                    case RowId::LobbyCharacter:
                    case RowId::LobbyKick:
                        field(i, 170, 26);
                        break;
                    case RowId::LobbyChat: {
                        d.panel(p.map(r.rect), PanelStyle::Inset, withAlpha(th.inset, r.enabled ? 230 : 120));
                        if (i == focus_)
                            p.focus(d, p.map(r.rect));
                        std::string v = chatInput_;
                        const bool ed = i == focus_ && editing_;
                        if (ed)
                            v += (static_cast<long long>(st.now * 2) & 1) ? "_" : " ";
                        if (v.empty())
                            p.text(d, {r.rect.x + 16, r.rect.y, r.rect.w - 32, r.rect.h}, "Press Select to chat",
                                   FontRole::Small, th.textDisabled, TextAlign::Left, 20);
                        else
                            p.text(d, {r.rect.x + 16, r.rect.y, r.rect.w - 32, r.rect.h}, "Say: " + v, FontRole::Body,
                                   ed ? th.ember : th.text, TextAlign::Left, 22);
                        break;
                    }
                    case RowId::LobbyReady: {
                        const bool foc = i == focus_, hov = i == hover_ && r.enabled;
                        d.panel(p.map(r.rect), PanelStyle::Card,
                                !r.enabled     ? withAlpha(th.panel, 120)
                                : st.selfReady ? withAlpha(th.good, 60)
                                : hov          ? th.panelRaised
                                               : th.panel);
                        if (foc)
                            p.focus(d, p.map(r.rect));
                        p.text(d, r.rect, st.selfReady ? "Ready (cancel)" : "Ready up", FontRole::Heading,
                               !r.enabled ? th.textDisabled : (st.selfReady ? th.good : (foc ? th.heading : th.text)),
                               TextAlign::Center, 28);
                        break;
                    }
                    default:
                        button(i, r.id == RowId::LobbyStart ? 30.0f : 26.0f);
                        break;
                }
            }
            if (st.mode == GameMode::Coop) { // character select: big preview, name, description, the selector row
                const CharacterEntry ch = selectedCharacter(st);
                const Rect cpn{80, 676, 1020, 314};
                d.panel(p.map(cpn), PanelStyle::Main, withAlpha(th.panel, 226));
                const Rect stage{cpn.x + 20, cpn.y + 20, 280, cpn.h - 40};
                const Color cc = colourRgba(setup_.colour);
                d.gradient(p.map(stage), mix(cc, rgba(0, 0, 0), 0.75f), rgba(10, 6, 4));
                d.outline(p.map(stage), th.brassDim, 2.0f);
                d.image(p.map({stage.cx() - 130, stage.cy() - 130, 260, 260}), ImageRef{ImageKind::SkinOrb, 0},
                        withAlpha(cc, 150));
                if (ch.turnFrames > 0) { // the real model, turning (frames made at run time from the disc)
                    const int fr = static_cast<int>(st.now * 6.0) % ch.turnFrames;
                    const float ih = stage.h - 12, iw = ih / 1.5f;
                    d.image(p.map({stage.cx() - iw / 2, stage.y + 6, iw, ih}),
                            ImageRef{ImageKind::CharTurntable,
                                     static_cast<uint32_t>(ch.classId & 0xffff) | (static_cast<uint32_t>(fr) << 16)});
                } else {
                    drawFigure(d, p, stage.inset(14), cc, ch.classId, st.now);
                }
                p.label(d, {430, cpn.y + 20, 600, 28}, st.hosting ? "YOUR CHARACTER (HOST)" : "CHOOSE YOUR CHARACTER",
                        th.brass, TextAlign::Left, 16);
                p.text(d, {430, cpn.y + 48, 640, 64}, ch.name.empty() ? std::string("-") : ch.name, FontRole::Title,
                       th.heading, TextAlign::Left, 52);
                p.text(d, {430, cpn.y + 112, 640, 34}, ch.model, FontRole::Body, th.textMuted, TextAlign::Left, 24);
                lineWrapped({430, cpn.y + 156, 640, 28}, ch.description, FontRole::Small, 21, th.text, 2, 28);
                p.text(d, {430, cpn.y + 184, 640, 30},
                       st.hosting ? "Rynn is locked to the host. Everyone gets their own dragon."
                                  : "Rynn belongs to the host. Everyone gets their own dragon.",
                       FontRole::Small, th.textDisabled, TextAlign::Left, 19);
                if (const int ci = rowOf(RowId::LobbyCharacter); ci >= 0)
                    field(ci, 170, 26); // the selector sits on the panel (drawn again over it)
            }
            int ready = 0;
            for (const RosterRow& pl : st.roster)
                ready += pl.ready ? 1 : 0;
            if (team) {
                int teams = 2; // custom modes play with up to four teams: one column per team on the roster
                for (const RosterRow& pl : st.roster)
                    teams = std::max(teams, std::min(pl.team + 1, 4));
                const float colW = 1020.0f / static_cast<float>(teams);
                for (int t = 0; t < teams; ++t) {
                    const float x = kM + static_cast<float>(t) * colW;
                    d.rect(p.map({x, 258, colW - 20, 6}), teamRgba(t));
                    int cnt = 0;
                    for (const RosterRow& pl : st.roster)
                        cnt += pl.team == t ? 1 : 0;
                    p.text(d, {x, 218, colW * 0.55f, 40}, teamName(t), FontRole::Heading, th.heading, TextAlign::Left,
                           30);
                    p.text(d, {x + colW * 0.5f, 218, colW * 0.5f - 20, 40}, std::to_string(cnt) + " players",
                           FontRole::Small, th.textMuted, TextAlign::Right, 20);
                }
            } else {
                p.label(d, {kM, 232, 400, 28},
                        "PLAYERS  " + std::to_string(st.roster.size()) + " / " + std::to_string(st.maxPlayers),
                        th.brass, TextAlign::Left, 16);
                p.label(d, {kM + 500, 232, 520, 28}, std::to_string(ready) + " READY", th.brass, TextAlign::Right, 16);
            }
            const LevelEntry me = findMap(st.level);
            const Rect pv = lobbyPreview();
            drawMapCard(d, p, pv, me, st.now);
            d.rect(p.map({pv.x, pv.y, pv.w, 70}), rgba(0, 0, 0, 130));
            p.text(d, {pv.x + 14, pv.y + 10, pv.w - 28, 50}, me.title.empty() ? std::string("-") : me.title,
                   FontRole::Title, th.heading, TextAlign::Left, 36);
            const Rect cp{pv.x, pv.bottom() + 16, pv.w, 770 - (pv.bottom() + 16) - 10};
            d.panel(p.map(cp), PanelStyle::Inset, withAlpha(th.inset, 215));
            p.label(d, {cp.x + 16, cp.y + 6, 300, 26}, "CHAT", th.brass, TextAlign::Left, 15);
            {
                const size_t maxLines = 5;
                const size_t from = st.chat.size() > maxLines ? st.chat.size() - maxLines : 0;
                float y = cp.y + 34;
                for (size_t k = from; k < st.chat.size(); ++k, y += 31) {
                    const HudChatLine& c = st.chat[k];
                    p.text(d, {cp.x + 16, y, cp.w - 32, 30}, c.who.empty() ? c.text : c.who + ": " + c.text,
                           FontRole::Small, c.who.empty() ? th.textMuted : th.text, TextAlign::Left, 20);
                }
                if (st.chat.empty())
                    p.text(d, {cp.x + 16, y, cp.w - 32, 30}, "No messages yet.", FontRole::Small, th.textDisabled,
                           TextAlign::Left, 20);
            }
            if (st.countdownSec >= 0) {
                d.rect(view, withAlpha(rgba(0, 0, 0), 150));
                d.panel(p.map({660, 380, 600, 260}), PanelStyle::Main, withAlpha(th.panelRaised, 240));
                p.text(d, {680, 396, 560, 56}, "Match starts in", FontRole::Heading, th.textMuted, TextAlign::Center,
                       34);
                p.text(d, {680, 450, 560, 160},
                       std::to_string(std::max(0, static_cast<int>(std::ceil(st.countdownSec)))), FontRole::Title,
                       th.heading, TextAlign::Center, 130);
            }
            break;
        }
    }

    const bool loadingCard = page_ == MpPage::Lobby && st.phase != MatchPhaseView::Lobby && st.conn == Conn::Connected;
    if (loadingCard)
        p.confirmCard(d, design::pause::cardPanel(2), "Loading...",
                      st.level.empty() ? std::string("The match is starting.") : "Loading " + st.level + ".");

    std::vector<Hint> hs = hints;
    const std::vector<Rect> rs =
        p.layoutHints(hs, kDesignW - design::mp::kMargin, false, design::mp::kFooterY, design::mp::kHintH,
                      design::mp::kHintGlyph, design::mp::kHintGap, FontRole::Body);
    for (size_t i = 0; i < hs.size(); ++i)
        p.drawHint(d, hs[i], p.map(rs[i]), false, design::mp::kHintGlyph, FontRole::Body);
}

// ---- in-game HUD ----

namespace {
// Design rects (the 1920 x 1080 canvas) of the results screen's buttons: a row at the bottom right of the panel (the
// panel is {110, 60, 1700, 960}).
std::vector<Rect> resultsButtonDesign(size_t count) {
    std::vector<Rect> out;
    const float w = count > 3 ? 236.0f : 262.0f, h = 66.0f, gap = 14.0f;
    const float right = 110.0f + 1700.0f - 44.0f, y = 60.0f + 960.0f - 100.0f;
    float x = right - static_cast<float>(count) * w - static_cast<float>(count > 0 ? count - 1 : 0) * gap;
    for (size_t i = 0; i < count; ++i, x += w + gap)
        out.push_back({x, y, w, h});
    return out;
}
} // namespace

namespace {
// Design rects of the vote cards: a row of up to three above the results buttons.
constexpr float kVoteY = 640, kVoteCardH = 170;
std::vector<Rect> mapVoteDesign(size_t count) {
    std::vector<Rect> out;
    const float gap = 16, left = 110 + 44, w = (1700 - 88 - 2 * gap) / 3.0f;
    for (size_t i = 0; i < count && i < 3; ++i)
        out.push_back({left + static_cast<float>(i) * (w + gap), kVoteY + 38, w, kVoteCardH});
    return out;
}
} // namespace

std::vector<Rect> mapVoteRects(size_t count, const Viewport& vp, const ScreenConfig& cfg) {
    const LayoutFrame lf = computeFrame(vp);
    const detail::Paint p(lf, cfg);
    std::vector<Rect> out;
    for (const Rect& r : mapVoteDesign(count))
        out.push_back(p.map(r));
    return out;
}

int mapVoteFrame(size_t count, int mine, const UiInput& in, const Viewport& vp, const ScreenConfig& cfg) {
    if (count == 0)
        return -1;
    const int n = static_cast<int>(std::min<size_t>(count, 3));
    if (in.pointerValid && in.leftClick) {
        const std::vector<Rect> rects = mapVoteRects(count, vp, cfg);
        for (int i = 0; i < n; ++i)
            if (rects[static_cast<size_t>(i)].contains(in.pointerX, in.pointerY))
                return i;
    }
    if (in.has(UiKey::PageNext))
        return mine < 0 ? 0 : (mine + 1) % n;
    if (in.has(UiKey::PagePrev))
        return mine < 0 ? n - 1 : (mine + n - 1) % n;
    return -1;
}

std::vector<Rect> resultsButtonRects(size_t count, const Viewport& vp, const ScreenConfig& cfg) {
    const LayoutFrame lf = computeFrame(vp);
    const detail::Paint p(lf, cfg);
    std::vector<Rect> out;
    for (const Rect& r : resultsButtonDesign(count))
        out.push_back(p.map(r));
    return out;
}

int ResultsMenu::frame(const std::vector<ResultsButton>& buttons, const UiInput& in, const Viewport& vp,
                       const ScreenConfig& cfg) {
    const int n = static_cast<int>(buttons.size());
    if (n == 0) {
        focus_ = -1;
        return -1;
    }
    if (focus_ < 0 || focus_ >= n || !buttons[static_cast<size_t>(focus_)].enabled) {
        focus_ = -1;
        for (int i = 0; i < n && focus_ < 0; ++i)
            if (buttons[static_cast<size_t>(i)].enabled)
                focus_ = i;
    }
    const auto move = [&](int dir) {
        for (int i = focus_ + dir; i >= 0 && i < n; i += dir)
            if (buttons[static_cast<size_t>(i)].enabled) {
                focus_ = i;
                return;
            }
    };
    for (UiKey k : in.keys) {
        if (k == UiKey::Left || k == UiKey::Up)
            move(-1);
        else if (k == UiKey::Right || k == UiKey::Down)
            move(1);
    }
    int pressed = -1;
    if (in.pointerValid) {
        const std::vector<Rect> rects = resultsButtonRects(buttons.size(), vp, cfg);
        for (int i = 0; i < n; ++i)
            if (buttons[static_cast<size_t>(i)].enabled &&
                rects[static_cast<size_t>(i)].contains(in.pointerX, in.pointerY)) {
                if (in.pointerMoved || in.leftClick)
                    focus_ = i;
                if (in.leftClick)
                    pressed = i;
            }
    }
    if (pressed < 0 && in.has(UiKey::Confirm) && focus_ >= 0)
        pressed = focus_;
    return pressed;
}

std::vector<RosterRow> sortedRoster(std::vector<RosterRow> r) {
    std::stable_sort(r.begin(), r.end(), [](const RosterRow& a, const RosterRow& b) {
        if (a.score != b.score)
            return a.score > b.score; // points first (a standard match: score = kills)
        if (a.kills != b.kills)
            return a.kills > b.kills;
        if (a.deaths != b.deaths)
            return a.deaths < b.deaths;
        return a.id < b.id;
    });
    return r;
}

namespace {

std::string mmss(int sec) {
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", std::max(0, sec) / 60, std::max(0, sec) % 60);
    return b;
}

// One standings table (header + rows) in a design rect: rank, colour swatch, name, kills, deaths, ping, time.
void drawStandings(DrawList& d, const detail::Paint& p, const Rect& box, const std::vector<RosterRow>& rows,
                   float colStep, float rowStep, int maxRows, const char* killsLabel = "KILLS", bool ranks = true,
                   bool scoreMode = false) {
    const Theme& th = p.th();
    const float W = box.w;
    const float tEnd = W - 20, pEnd = tEnd - colStep, dEnd = pEnd - colStep, kEnd = dEnd - colStep;
    const float hy = box.y;
    p.label(d, {box.x + 70, hy, 300, 28}, "PLAYER", th.brass, TextAlign::Left, 17);
    p.label(d, {box.x + kEnd - 110, hy, 110, 28}, killsLabel, th.brass, TextAlign::Right, 17);
    p.label(d, {box.x + dEnd - 110, hy, 110, 28}, "DEATHS", th.brass, TextAlign::Right, 17);
    p.label(d, {box.x + pEnd - 110, hy, 110, 28}, "PING", th.brass, TextAlign::Right, 17);
    p.label(d, {box.x + tEnd - 110, hy, 110, 28}, "TIME", th.brass, TextAlign::Right, 17);
    float y = hy + 36;
    int rank = 1;
    for (const RosterRow& r : rows) {
        if (rank > maxRows)
            break;
        const Rect rr{box.x, y, W, rowStep - 6};
        d.panel(p.map(rr), r.self ? PanelStyle::Card : PanelStyle::Inset,
                r.self ? th.panelRaised : withAlpha(th.inset, 190));
        if (r.self)
            d.outline(p.map(rr), th.gilt, 2.5f, 4);
        if (ranks)
            p.text(d, {rr.x + 8, rr.y, 44, rr.h}, std::to_string(rank), FontRole::Numeric,
                   rank == 1 ? th.gilt : th.textMuted, TextAlign::Center, 26);
        const Color sw = r.colour >= 0 ? colourRgba(r.colour) : r.team >= 0 ? teamRgba(r.team) : colourRgba(r.id);
        d.rect(p.map({rr.x + 60, rr.y + 8, 10, rr.h - 16}), sw, 3);
        p.text(d, {rr.x + 84, rr.y, kEnd - 84 - 130, rr.h},
               r.name + (r.self ? "  (you)" : "") +
                   (r.characterName.empty() ? std::string() : "   as " + r.characterName),
               FontRole::Body, r.self ? th.heading : th.text, TextAlign::Left, 28);
        p.text(d, {rr.x + kEnd - 110, rr.y, 110, rr.h}, std::to_string(scoreMode ? r.score : r.kills),
               FontRole::Numeric, th.heading, TextAlign::Right, 30);
        p.text(d, {rr.x + dEnd - 110, rr.y, 110, rr.h}, std::to_string(r.deaths), FontRole::Numeric, th.textMuted,
               TextAlign::Right, 28);
        p.text(d, {rr.x + pEnd - 110, rr.y, 110, rr.h}, r.pingMs < 0 ? std::string("-") : std::to_string(r.pingMs),
               FontRole::Numeric, th.textMuted, TextAlign::Right, 28);
        p.text(d, {rr.x + tEnd - 110, rr.y, 110, rr.h}, mmss(r.timeSec), FontRole::Numeric, th.textMuted,
               TextAlign::Right, 28);
        y += rowStep;
        ++rank;
    }
}

} // namespace

DrawList drawHud(const HudState& hs, const Viewport& vp, const ScreenConfig& cfg, float chatLifeSec, int maxLines,
                 float killLifeSec) {
    DrawList d;
    const LayoutFrame lf = computeFrame(vp);
    const detail::Paint p(lf, cfg);
    const Theme& th = cfg.theme;
    const Rect view{lf.view.x, lf.view.y, lf.view.w, lf.view.h};
    if (hs.scoreboard || hs.results) {
        const std::vector<RosterRow> rows = sortedRoster(hs.roster);
        const bool teamGame = hs.mode == GameMode::TeamDeathmatch;
        const int ts[4] = {hs.scoreMode ? hs.teamScore[0] : hs.teamKills[0],
                           hs.scoreMode ? hs.teamScore[1] : hs.teamKills[1], hs.teamScore[2], hs.teamScore[3]};
        const Rect box{110, 60, 1700, 960};
        d.rect(view, withAlpha(th.backdropEdge, hs.results ? 190 : 150));
        d.panel(p.map(box), PanelStyle::Main, withAlpha(th.panel, 238));
        const bool coop = hs.mode == GameMode::Coop;
        const std::string head = hs.results
                                     ? (coop ? std::string(hs.levelComplete ? "Level Complete" : "Party Defeated")
                                             : std::string("Match Over"))
                                     : (hs.serverName.empty() ? std::string("Scoreboard") : hs.serverName);
        p.text(d, {box.x + 44, box.y + 20, 900, 76}, head, FontRole::Title, th.heading, TextAlign::Left, 60);
        std::string sub = (hs.modeName.empty() ? std::string(modeName(hs.mode)) : hs.modeName) + "   " + hs.level;
        if (hs.results && !hs.serverName.empty())
            sub = hs.serverName + "   " + sub;
        p.text(d, {box.x + 46, box.y + 96, 1000, 36}, sub, FontRole::Body, th.textMuted, TextAlign::Left, 26);
        // Limit chips, top right.
        float cx = box.right() - 44;
        const auto chip = [&](const char* k, const std::string& v) {
            const float w = 230;
            cx -= w;
            d.panel(p.map({cx, box.y + 26, w, 100}), PanelStyle::Inset, withAlpha(th.inset, 220));
            p.label(d, {cx + 16, box.y + 34, w - 32, 26}, k, th.brass, TextAlign::Left, 15);
            p.text(d, {cx + 16, box.y + 62, w - 32, 56}, v, FontRole::Numeric, th.heading, TextAlign::Left, 44);
            cx -= 16;
        };
        if (coop) {
            int kills = 0, deaths = 0;
            for (const RosterRow& r : rows) {
                kills += r.kills;
                deaths += r.deaths;
            }
            chip("PARTY DEATHS", std::to_string(deaths));
            chip("CREATURES SLAIN", std::to_string(kills));
        }
        if (hs.timeLeftSec >= 0)
            chip(hs.results ? "TIME" : "TIME LEFT", mmss(hs.timeLeftSec));
        if (hs.fragLimit > 0 && !coop)
            chip(hs.scoreMode ? "SCORE LIMIT" : "FRAG LIMIT", std::to_string(hs.fragLimit));
        p.ornateRule(d, p.map({box.x + 44, 0, 0, 0}).x, p.map({box.right() - 44, 0, 0, 0}).x,
                     p.map({0, box.y + 150, 0, 0}).y, false);
        float top = box.y + 168;
        const bool voteOn = hs.results && !hs.vote.empty();
        // Winner banner on the results screen.
        std::string winner;
        if (hs.results && coop) {
            winner = hs.levelComplete ? "Level complete" : "The party was defeated";
        } else if (hs.results && !rows.empty()) {
            if (teamGame) {
                int best = 0;
                bool tie = false;
                for (int t = 1; t < std::clamp(hs.teamCount, 2, 4); ++t) {
                    if (ts[t] > ts[best]) {
                        best = t;
                        tie = false;
                    } else if (ts[t] == ts[best]) {
                        tie = true;
                    }
                }
                winner = tie ? std::string("Draw") : std::string("Team ") + teamName(best) + " wins";
            } else if (rows.size() > 1 && rows[0].score == rows[1].score && rows[0].kills == rows[1].kills &&
                       rows[0].deaths == rows[1].deaths) {
                winner = "Draw";
            } else {
                winner = rows[0].name + (rows[0].self ? " (you) win" : " wins");
                if (rows[0].self)
                    winner = "You win";
            }
        }
        if (!winner.empty()) {
            d.panel(p.map({box.x + 44, top, box.w - 88, 72}), PanelStyle::Card, th.panelRaised);
            p.text(d, {box.x + 60, top, box.w - 120, 72}, winner, FontRole::Heading,
                   coop ? (hs.levelComplete ? th.good : th.bad) : th.hudAccent, TextAlign::Center, 46);
            top += 90;
        }
        if (coop) {
            drawStandings(d, p, {box.x + 44, top, box.w - 88, 0}, rows, 150, hs.results ? 52.0f : 56.0f,
                          hs.results ? 8 : 8, "CREATURES", false);
        } else if (teamGame && hs.teamCount > 2) {
            // 3 or 4 teams: one score card per team, then every player in one table (the swatch is the team's colour).
            const int n = std::clamp(hs.teamCount, 2, 4);
            const float cw = (box.w - 88 - static_cast<float>(n - 1) * 16) / static_cast<float>(n);
            for (int t = 0; t < n; ++t) {
                const float x = box.x + 44 + static_cast<float>(t) * (cw + 16);
                d.panel(p.map({x, top, cw, 76}), PanelStyle::Inset, withAlpha(th.inset, 220));
                d.rect(p.map({x, top, cw, 6}), teamRgba(t));
                p.text(d, {x + 14, top + 8, cw - 140, 64}, std::string("Team ") + teamName(t), FontRole::Heading,
                       th.heading, TextAlign::Left, 32);
                p.text(d, {x + cw - 130, top + 6, 116, 68}, std::to_string(ts[t]), FontRole::Numeric, teamRgba(t),
                       TextAlign::Right, 52);
            }
            drawStandings(d, p, {box.x + 44, top + 92, box.w - 88, 0}, rows, 150, hs.results ? 46.0f : 50.0f,
                          hs.results ? (voteOn ? 3 : 6) : 8, hs.scoreMode ? "SCORE" : "KILLS", true, hs.scoreMode);
        } else if (teamGame) {
            for (int t = 0; t < 2; ++t) {
                const float x = box.x + 44 + static_cast<float>(t) * 818;
                const Rect pane{x, top, 790, 0};
                d.rect(p.map({x, top, 790, 6}), teamRgba(t));
                p.text(d, {x, top + 10, 400, 56}, std::string("Team ") + teamName(t), FontRole::Heading, th.heading,
                       TextAlign::Left, 38);
                p.text(d, {x + 440, top + 6, 350, 64}, std::to_string(ts[t]), FontRole::Numeric, teamRgba(t),
                       TextAlign::Right, 56);
                std::vector<RosterRow> mine;
                for (const RosterRow& r : rows)
                    if ((r.team & 1) == t)
                        mine.push_back(r);
                drawStandings(d, p, {pane.x, top + 76, 790, 0}, mine, 100, 52, hs.results ? (voteOn ? 3 : 6) : 9,
                              hs.scoreMode ? "SCORE" : "KILLS", true, hs.scoreMode);
            }
        } else {
            drawStandings(d, p, {box.x + 44, top, box.w - 88, 0}, rows, 150, hs.results ? 52.0f : 56.0f,
                          hs.results ? (voteOn                      ? 4
                                        : hs.resultsButtons.empty() ? 10
                                                                    : 7)
                                     : 12,
                          hs.scoreMode ? "SCORE" : "KILLS", true, hs.scoreMode);
        }
        if (voteOn) { // the map vote: one picture card per candidate
            p.label(d, {box.x + 46, kVoteY, 900, 30}, "VOTE FOR THE NEXT MAP", th.brass, TextAlign::Left, 17);
            p.text(d, {box.x + 600, kVoteY - 6, box.w - 600 - 44, 38}, "Click a map or press Q / E", FontRole::Small,
                   th.textMuted, TextAlign::Right, 20);
            const std::vector<Rect> cards = mapVoteDesign(hs.vote.size());
            for (size_t i = 0; i < cards.size(); ++i) {
                const VoteCandidate& v = hs.vote[i];
                const Rect& c = cards[i];
                const bool mine = static_cast<int>(i) == hs.voteMine, won = static_cast<int>(i) == hs.voteWinner;
                d.panel(p.map(c), mine || won ? PanelStyle::Card : PanelStyle::Inset,
                        mine || won ? th.panelRaised : withAlpha(th.inset, 225));
                if (mine || won)
                    d.outline(p.map(c), won ? th.good : th.hudAccent, 3.0f, 4);
                const Rect pic{c.x + 10, c.y + 10, 300, 111};
                if (v.previewId)
                    d.image(p.map(pic), ImageRef{ImageKind::LevelPreview, v.previewId});
                else
                    drawMapArt(d, p, pic, v.name, 0, hs.now);
                d.outline(p.map(pic), withAlpha(th.brass, 150), 1.5f, 3);
                p.text(d, {c.x + 328, c.y + 8, c.w - 340, 80}, std::to_string(v.votes), FontRole::Numeric,
                       v.votes > 0 ? th.heading : th.textDisabled, TextAlign::Center, 64);
                p.label(d, {c.x + 328, c.y + 82, c.w - 340, 28}, v.votes == 1 ? "VOTE" : "VOTES", th.brass,
                        TextAlign::Center, 15);
                if (mine)
                    p.label(d, {c.x + 328, c.y + 106, c.w - 340, 28}, "YOUR PICK", th.hudAccent, TextAlign::Center, 15);
                p.text(d, {c.x + 12, c.y + 124, c.w - 24, 40}, v.name, FontRole::Body,
                       mine || won ? th.heading : th.text, TextAlign::Left, 25);
            }
        }
        if (hs.results) {
            std::string next = hs.resultsText;
            if (hs.nextMatchSec >= 0)
                next = "Next match in " + std::to_string(static_cast<int>(std::ceil(hs.nextMatchSec))) + " ...";
            if (next.empty())
                next = coop ? "Continuing to the next level ..." : "Returning to the lobby ...";
            if (hs.resultsButtons.empty()) {
                p.rule(d, box.x + 44, box.bottom() - 70, box.w - 88);
                p.text(d, {box.x + 44, box.bottom() - 62, box.w - 88, 48}, next, FontRole::Body, th.brass,
                       TextAlign::Center, 28);
            } else {
                // Buttons, right aligned at the bottom of the panel; the countdown (and a note) to their left.
                p.rule(d, box.x + 44, box.bottom() - 112, box.w - 88);
                const std::vector<Rect> rects = resultsButtonDesign(hs.resultsButtons.size());
                float textRight = box.right() - 44;
                for (size_t i = 0; i < hs.resultsButtons.size() && i < rects.size(); ++i) {
                    const ResultsButton& b = hs.resultsButtons[i];
                    const bool focus = static_cast<int>(i) == hs.resultsFocus && b.enabled;
                    d.panel(p.map(rects[i]), focus ? PanelStyle::Card : PanelStyle::Inset,
                            focus ? th.panelRaised : withAlpha(th.inset, b.enabled ? 230 : 130));
                    if (focus)
                        d.outline(p.map(rects[i]), th.hudAccent, 3.0f, 4);
                    p.text(d, {rects[i].x + 8, rects[i].y, rects[i].w - 16, rects[i].h}, b.label, FontRole::Heading,
                           !b.enabled ? th.textDisabled
                           : focus    ? th.heading
                                      : th.text,
                           TextAlign::Center, 28);
                    textRight = std::min(textRight, rects[i].x);
                }
                const float textW = textRight - 24 - (box.x + 44);
                if (textW > 100) {
                    p.text(d, {box.x + 44, box.bottom() - 100, textW, 40}, next, FontRole::Body, th.brass,
                           TextAlign::Left, 26);
                    if (!hs.resultsNote.empty())
                        p.text(d, {box.x + 44, box.bottom() - 62, textW, 34}, hs.resultsNote, FontRole::Small,
                               th.textMuted, TextAlign::Left, 21);
                }
            }
        } else {
            p.text(d, {box.x + 44, box.bottom() - 50, box.w - 88, 36}, "Hold F4 for the scoreboard", FontRole::Small,
                   th.textDisabled, TextAlign::Center, 19);
        }
    }
    if (!hs.banner.empty() && !hs.results) { // limit reached: banner before the next level
        d.panel(p.map({400, 20, 1120, 110}), PanelStyle::Main, withAlpha(th.panelRaised, 238));
        p.text(d, {420, 26, 1080, 60}, hs.banner, FontRole::Heading, th.heading, TextAlign::Center, 44);
        if (!hs.resultsText.empty())
            p.text(d, {420, 86, 1080, 36}, hs.resultsText, FontRole::Body, th.textMuted, TextAlign::Center, 24);
    }
    if (!hs.status.empty() && !hs.results && !hs.scoreboard) { // limits chip, top centre
        d.panel(p.map({640, 16, 640, 62}), PanelStyle::Inset, withAlpha(th.inset, 215));
        p.text(d, {652, 16, 616, 62}, hs.status, FontRole::Heading, th.text, TextAlign::Center, 30);
    }
    { // Kill feed: top right, newest at the bottom, fading.
        std::vector<const HudKill*> ks;
        for (const HudKill& k : hs.kills)
            if (hs.now - k.at < killLifeSec)
                ks.push_back(&k);
        if (ks.size() > 6)
            ks.erase(ks.begin(), ks.end() - 6);
        float ky = hs.scoreboard || hs.results ? 1030.0f : 24.0f;
        if (!(hs.scoreboard || hs.results))
            for (const HudKill* k : ks) {
                const double age = hs.now - k->at;
                const uint8_t a = age < killLifeSec - 1.5
                                      ? 255
                                      : static_cast<uint8_t>(255 * std::max(0.0, (killLifeSec - age) / 1.5));
                const std::string line = k->killer.empty()
                                             ? k->victim + " died"
                                             : k->killer + "  killed  " + k->victim +
                                                   (k->weapon.empty() ? std::string() : "  (" + k->weapon + ")");
                const bool mine = k->selfKiller || k->selfVictim;
                const Rect kr{1080, ky, 800, 46};
                d.rect(p.map(kr), withAlpha(rgba(0, 0, 0), static_cast<uint8_t>(a / 2 + 20)), 6);
                if (mine)
                    d.outline(p.map(kr), withAlpha(k->selfKiller ? th.select : th.brass, a), 2.0f, 6);
                p.text(d, {kr.x + 14, ky, kr.w - 28, 46}, line, FontRole::Body,
                       withAlpha(k->selfKiller   ? th.heading
                                 : k->selfVictim ? th.brass
                                                 : th.text,
                                 a),
                       TextAlign::Right, 26);
                ky += 52;
            }
    }
    constexpr float kPartyRowStride = 98;
    if (hs.mode == GameMode::Coop && !hs.scoreboard && !hs.results) { // Co-op: party panel and partner markers
        const size_t n = std::min<size_t>(hs.partners.size(), 7);
        float y = 24;
        for (size_t i = 0; i < n; ++i, y += kPartyRowStride) {
            const HudPartner& q = hs.partners[i];
            const Color sw = q.colour >= 0 ? colourRgba(q.colour) : colourRgba(q.id);
            const Rect row{24, y, 560, 88};
            d.panel(p.map(row), PanelStyle::Inset, withAlpha(th.inset, q.down ? 160 : 225));
            d.rect(p.map({row.x + 10, row.y + 8, 10, row.h - 16}), q.down ? th.textDisabled : sw, 3);
            p.text(d, {row.x + 34, row.y + 2, 400, 34},
                   q.name + (q.character.empty() ? std::string() : "  (" + q.character + ")"), FontRole::Body,
                   q.down ? th.textMuted : th.text, TextAlign::Left, 30);
            const Rect bar{row.x + 34, row.y + 40, 360, 26};
            const float f = std::clamp(q.down ? 0.0f : q.hpFrac, 0.0f, 1.0f);
            if (q.hpKnown || q.down) { // the session does not always know a partner's health: no bar then
                d.rect(p.map(bar), rgba(0, 0, 0, 190), 4);
                if (f > 0)
                    d.rect(p.map({bar.x, bar.y, bar.w * f, bar.h}), f > 0.35f ? th.hp : th.bad, 4);
                d.outline(p.map(bar), withAlpha(th.brassDim, 230), 1.5f, 4);
            }
            if (q.dragonKnown && !q.down) { // mounted: the dragon's own bar under the health
                const Rect db{bar.x, bar.y + 32, 360, 10};
                const float df = std::clamp(q.dragonFrac, 0.0f, 1.0f);
                d.rect(p.map(db), rgba(0, 0, 0, 190), 3);
                if (df > 0)
                    d.rect(p.map({db.x, db.y, db.w * df, db.h}), th.brass, 3);
                d.outline(p.map(db), withAlpha(th.brassDim, 230), 1.0f, 3);
                p.text(d, {db.x + db.w + 8, db.y - 9, 120, 28}, "DRAGON", FontRole::Small, th.brass, TextAlign::Left,
                       18);
            }
            if (q.down) {
                char b[48];
                if (q.respawnInSec > 0)
                    std::snprintf(b, sizeof b, "DOWN  %ds", static_cast<int>(std::ceil(q.respawnInSec)));
                else
                    std::snprintf(b, sizeof b, "DOWN");
                p.text(d, {row.x + 400, row.y + 36, 150, 34}, b, FontRole::Body, th.bad, TextAlign::Left, 30);
            } else if (q.hpKnown) {
                const std::string t = q.hpMax > 0 ? std::to_string(q.hp) + " / " + std::to_string(q.hpMax)
                                                  : std::to_string(static_cast<int>(f * 100 + 0.5f)) + "%";
                p.text(d, {row.x + 404, row.y + 36, 150, 34}, t, FontRole::Body, th.text, TextAlign::Left, 28);
            }
        }
        for (size_t i = 0; i < hs.partners.size(); ++i) {
            const HudPartner& q = hs.partners[i];
            if (!q.markerValid)
                continue;
            const Color sw = q.down ? th.textDisabled : (q.colour >= 0 ? colourRgba(q.colour) : colourRgba(q.id));
            float mx = q.markerX * kDesignW, my = q.markerY * kDesignH;
            const std::string label =
                q.name +
                (q.distance > 0 ? "  " + std::to_string(static_cast<int>(q.distance + 0.5f)) + " m" : std::string());
            if (q.onScreen) { // a diamond above the partner and the name over it
                pxLine(d, p, mx, my - 14, mx + 12, my, sw, 3);
                pxLine(d, p, mx + 12, my, mx, my + 14, sw, 3);
                pxLine(d, p, mx, my + 14, mx - 12, my, sw, 3);
                pxLine(d, p, mx - 12, my, mx, my - 14, sw, 3);
                p.text(d, {mx - 150, my - 52, 300, 30}, label, FontRole::Small, q.down ? th.textMuted : th.text,
                       TextAlign::Center, 21);
            } else { // an arrow on the screen edge pointing to them
                const float a = q.dirDeg * 3.14159265f / 180.0f, dx = std::cos(a), dy = std::sin(a);
                const float tx = std::abs(dx) > 1e-4f ? (kDesignW * 0.5f - 90) / std::abs(dx) : 1e9f;
                const float ty = std::abs(dy) > 1e-4f ? (kDesignH * 0.5f - 90) / std::abs(dy) : 1e9f;
                const float t = std::min(tx, ty);
                mx = kDesignW * 0.5f + dx * t;
                my = kDesignH * 0.5f + dy * t;
                // Not over the party panel (top left): slide along the edge the arrow sits on.
                const float panelR = 24 + 560 + 50, panelB = 24 + kPartyRowStride * static_cast<float>(n) + 40;
                if (n > 0 && mx < panelR && my < panelB) {
                    if (ty <= tx)
                        mx = panelR;
                    else
                        my = panelB;
                }
                const float px_ = -dy, py_ = dx;
                pxLine(d, p, mx + dx * 22, my + dy * 22, mx - dx * 14 + px_ * 16, my - dy * 14 + py_ * 16, sw, 4);
                pxLine(d, p, mx + dx * 22, my + dy * 22, mx - dx * 14 - px_ * 16, my - dy * 14 - py_ * 16, sw, 4);
                pxLine(d, p, mx - dx * 14 + px_ * 16, my - dy * 14 + py_ * 16, mx - dx * 14 - px_ * 16,
                       my - dy * 14 - py_ * 16, sw, 4);
                const bool left = dx > 0.3f; // label towards the screen centre
                const float lx = left ? mx - 330 : (dx < -0.3f ? mx + 40 : mx - 150);
                const float ly = dy > 0.6f ? my - 66 : (dy < -0.6f ? my + 40 : my - 15);
                p.text(d, {lx, ly, 290, 30}, label, FontRole::Small, q.down ? th.textMuted : th.text,
                       left ? TextAlign::Right : (dx < -0.3f ? TextAlign::Left : TextAlign::Center), 21);
            }
        }
    }
    if (!hs.alive && !hs.results) { // respawn overlay
        d.rect(view, withAlpha(rgba(60, 0, 0), 100));
        d.panel(p.map({460, 330, 1000, 340}), PanelStyle::Main, withAlpha(th.panelRaised, 240));
        p.text(d, {480, 350, 960, 90}, "You were killed", FontRole::Title, th.heading, TextAlign::Center, 66);
        if (!hs.deathText.empty())
            p.text(d, {480, 440, 960, 44}, hs.deathText, FontRole::Body, th.textMuted, TextAlign::Center, 28);
        const int secs = std::max(1, static_cast<int>(std::ceil(hs.respawnInSec)));
        if (hs.respawnInSec > 0) {
            p.label(d, {480, 500, 960, 30}, "RESPAWNING IN", th.brass, TextAlign::Center, 18);
            p.text(d, {480, 526, 960, 120}, std::to_string(secs), FontRole::Numeric, th.hudAccent, TextAlign::Center,
                   96);
        } else {
            p.text(d, {480, 540, 960, 60}, "Respawning ...", FontRole::Heading, th.brass, TextAlign::Center, 40);
        }
    }
    // Chat log: bottom left, fading, with a backdrop panel while the input is open.
    std::vector<const HudChatLine*> lines;
    for (const HudChatLine& c : hs.chat)
        if (hs.chatOpen || hs.now - c.at < chatLifeSec)
            lines.push_back(&c);
    if (static_cast<int>(lines.size()) > maxLines)
        lines.erase(lines.begin(), lines.end() - maxLines);
    const float lineH = 42;
    const float chatBottom = hs.chatOpen ? 930.0f : (hs.scoreboard || hs.results) ? 900.0f : 990.0f;
    float y = chatBottom - static_cast<float>(lines.size()) * lineH;
    if (hs.chatOpen && !lines.empty())
        d.panel(p.map({50, y - 10, 1000, static_cast<float>(lines.size()) * lineH + 20}), PanelStyle::Inset,
                withAlpha(th.inset, 150));
    for (const HudChatLine* c : lines) {
        const double age = hs.now - c->at;
        const uint8_t a = hs.chatOpen || age < chatLifeSec - 1.5
                              ? 255
                              : static_cast<uint8_t>(255 * std::max(0.0, (chatLifeSec - age) / 1.5));
        d.rect(p.map({60, y, 980, lineH - 4}), withAlpha(rgba(0, 0, 0), static_cast<uint8_t>(a / 3)), 4);
        if (c->who.empty()) {
            p.text(d, {76, y, 950, lineH - 4}, c->text, FontRole::Body, withAlpha(th.textMuted, a), TextAlign::Left,
                   26);
        } else {
            const float nw = p.textWidth(c->who + ": ", FontRole::Body, 26);
            p.text(d, {76, y, nw + 4, lineH - 4}, c->who + ": ", FontRole::Body, withAlpha(th.hudAccent, a),
                   TextAlign::Left, 26);
            p.text(d, {76 + nw, y, 950 - nw, lineH - 4}, c->text, FontRole::Body, withAlpha(th.text, a),
                   TextAlign::Left, 26);
        }
        y += lineH;
    }
    if (hs.chatOpen) {
        d.panel(p.map({50, 940, 1000, 60}), PanelStyle::Inset, withAlpha(th.inset, 240));
        d.outline(p.map({50, 940, 1000, 60}), th.select, 2.0f);
        p.text(d, {70, 940, 960, 60}, "Say: " + hs.chatInput + ((static_cast<long long>(hs.now * 2) & 1) ? "_" : " "),
               FontRole::Body, th.text, TextAlign::Left, 28);
    }
    return d;
}

} // namespace gameui::mp
