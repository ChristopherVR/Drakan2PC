#include "gameui/Multiplayer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include "Paint.h"
#include "gameui/PauseScreen.h"

namespace gameui::mp {

namespace {

constexpr float kPanelH = 840, kPanelY = 150, kPad = 28;
constexpr float kRowH = 46, kRowStep = 52, kInfoH = 36;
constexpr int kVisibleList = 6;
constexpr size_t kNameMax = 24, kServerNameMax = 32, kAddrMax = 64, kPasswordMax = 24;
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
    return m == GameMode::Deathmatch || m == GameMode::TeamDeathmatch;
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

void MultiplayerScreen::open(const MpState& st) {
    open_ = true;
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
    buildRows(st);
}

void MultiplayerScreen::openLobby(const MpState& st) {
    open(st);
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
std::vector<LevelEntry> MultiplayerScreen::groupLevels(const MpState& st) const {
    std::vector<LevelEntry> out;
    for (const LevelEntry& e : levelsOf(st))
        if (host_.levelGroup == 0 || (host_.levelGroup == 1 && !e.custom) || (host_.levelGroup == 2 && e.custom))
            out.push_back(e);
    return out;
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
        const float h = kind == RowKind::Info ? kInfoH : kRowH;
        r.rect = {first.x, y, first.w, h};
        y += kind == RowKind::Info ? kInfoH : kRowStep;
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
            const bool team = host_.teams >= 2;
            static const char* const kGroups[] = {"All Levels", "Disc levels", "Custom levels"};
            const std::string lvl =
                levels.empty() ? std::string("none") : levels[static_cast<size_t>(host_.level)].title;
            add(RowId::HostName, RowKind::Text, "Server Name", host_.serverName);
            add(RowId::HostPassword, RowKind::Text, "Password",
                host_.password.empty() ? std::string("open") : std::string(host_.password.size(), '*'));
            add(RowId::HostGroup, RowKind::Choice, "Level Group", kGroups[wrapIdx(host_.levelGroup, 3)]);
            add(RowId::HostLevel, RowKind::Choice, "Starting Level",
                lvl + (!levels.empty() && levels[static_cast<size_t>(host_.level)].custom ? "  (custom)" : ""));
            add(RowId::HostMax, RowKind::Number, "Maximum Number of Players", std::to_string(host_.maxPlayers));
            add(RowId::HostFrag, RowKind::Number, "Kills Limit", limitText(host_.fragLimit, ""));
            add(RowId::HostTime, RowKind::Number, "Time Limit", limitText(host_.timeLimitMin, " minutes"));
            add(RowId::HostTeams, RowKind::Number, "Number of Teams",
                host_.teams == 0  ? std::string("No Teams")
                : host_.teams > 2 ? std::to_string(host_.teams) + "  (coming)"
                                  : std::to_string(host_.teams));
            add(RowId::HostFriendly, RowKind::Toggle, "Friendly Fire?", team ? "No  (coming)" : "No", false);
            add(RowId::HostRegister, RowKind::Toggle, "Register With Master Servers?", "No  (servers are gone)", false);
            add(RowId::HostStart, RowKind::Action, "Begin Game", {}, net && host_.teams <= 2 && !levels.empty());
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
            const std::string mode = std::string(modeName(st.mode));
            add(RowId::LobbyInfo, RowKind::Info,
                (st.serverName.empty() ? std::string("Match") : st.serverName) + "   " + mode + "   " +
                    (st.level.empty() ? std::string("-") : st.level));
            std::string limits = "Kills limit " + limitText(st.fragLimit, "") + "   Time limit " +
                                 limitText(st.timeLimitMin, " min") + "   Players " + std::to_string(st.roster.size()) +
                                 "/" + std::to_string(st.maxPlayers);
            add(RowId::LobbyInfo, RowKind::Info, limits);
            if (!st.message.empty())
                add(RowId::LobbyInfo, RowKind::Info, st.message);
            int i = 0;
            for (const RosterRow& p : st.roster)
                add(RowId::LobbyPlayer, RowKind::Info, p.name, {}, true, i++);
            const bool lobby = st.conn == Conn::Connected && st.phase == MatchPhaseView::Lobby;
            add(RowId::LobbyReady, RowKind::Toggle, "Ready", st.selfReady ? "Ready" : "Not ready", lobby);
            if (st.hosting)
                add(RowId::LobbyStart, RowKind::Action, "Start Match", {}, lobby && !st.roster.empty());
            add(RowId::LobbyLeave, RowKind::Action, "Leave");
            break;
        }
    }
    if (keep)
        for (int i = 0; i < rowCount(); ++i)
            if (rows_[static_cast<size_t>(i)].id == keepId && rows_[static_cast<size_t>(i)].index == keepIndex) {
                focus_ = i;
                break;
            }
    ensureFocus();
}

MpAction MultiplayerScreen::baseAction(MpActionKind k, const MpState& st) const {
    MpAction a;
    a.kind = k;
    a.setup = setup_;
    a.host = host_;
    a.host.mode = host_.teams >= 2 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
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
        default:
            return kNameMax;
    }
}

void MultiplayerScreen::back(const MpState& st, MpFrameOutput& out) {
    out.sound = UiSound::Back;
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
            break;
        case RowId::HostLevel:
            host_.level = wrapIdx(host_.level + dir, static_cast<int>(groupLevels(st).size()));
            break;
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
            host_.mode = host_.teams >= 2 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
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
        case RowId::LobbyReady: {
            MpAction a = baseAction(MpActionKind::SetReady, st);
            a.flag = !st.selfReady;
            out.actions.push_back(a);
            break;
        }
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
        case RowId::LobbyReady:
            adjust(i, 1, st, out);
            out.sound = UiSound::Toggle;
            return;
        case RowId::HostName:
        case RowId::HostPassword:
        case RowId::JoinNewAddress:
        case RowId::JoinPassword:
        case RowId::MenuName:
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
    (void)st;
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
    if (page_ == MpPage::Lobby && (st.conn == Conn::Rejected || (st.conn == Conn::Offline && lobbyActive_)))
        setPage(st.hosting ? MpPage::Host : MpPage::Join, st);
    buildRows(st);

    // Pointer.
    hover_ = -1;
    if (in.pointerValid)
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
    else if (in.leftClick) {
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
    hints.push_back({Prompt::Confirm, editing_ ? "Done" : "Select", navigable(focus_)});
    hints.push_back({Prompt::Back, editing_ ? "Done" : "Back"});
    draw(st, out.draw, hints);
    return out;
}

void MultiplayerScreen::draw(const MpState& st, DrawList& d, const std::vector<Hint>& hints) const {
    const ScreenConfig& sc = cfg_.screen;
    const detail::Paint p(lf_, sc);
    const Theme& th = sc.theme;
    const Rect view{lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h};
    d.rect(view, withAlpha(th.backdropEdge, 150));
    Rect fb = lf_.band(960, 120);
    fb.h = std::max(fb.h, lf_.view.y + lf_.view.h - fb.y);
    d.gradient(fb, withAlpha(th.panel, 0), withAlpha(th.panel, 220));

    Rect panel = design::mp::panel(); // as tall as its rows (plus room for a status line)
    float contentBottom = design::mp::firstRow().y;
    for (const Row& r : rows_)
        contentBottom = std::max(contentBottom, r.rect.bottom());
    panel.h = std::clamp(contentBottom + 76 - panel.y, 380.0f, panel.h);
    d.panel(p.map(panel), PanelStyle::Main, withAlpha(th.panel, 226));
    const char* title = page_ == MpPage::Menu   ? "Multiplayer"
                        : page_ == MpPage::Host ? "Host Game"
                        : page_ == MpPage::Join ? "Join Game"
                                                : "Lobby";
    p.text(d, design::mp::titleRect(), title, FontRole::Title, th.heading, TextAlign::Left);
    p.rule(d, panel.x + kPad, panel.y + 84, panel.w - 2 * kPad);
    if (!st.available)
        p.text(d, {panel.x + kPad, panel.bottom() - 60, panel.w - 2 * kPad, 40},
               st.unavailableWhy.empty() ? "Networking is not available in this build." : st.unavailableWhy,
               FontRole::Small, th.bad, TextAlign::Center);
    else if (page_ != MpPage::Lobby && !st.message.empty())
        p.text(d, {panel.x + kPad, panel.bottom() - 60, panel.w - 2 * kPad, 40}, st.message, FontRole::Small, th.bad,
               TextAlign::Center);
    else if (page_ == MpPage::Join)
        p.text(d, {panel.x + kPad, panel.bottom() - 60, panel.w - 2 * kPad, 40},
               "Available Servers: " + std::to_string(entries_.size()), FontRole::Small, th.textMuted,
               TextAlign::Center);

    const bool loadingCard = page_ == MpPage::Lobby && st.phase != MatchPhaseView::Lobby && st.conn == Conn::Connected;
    for (int i = 0; i < rowCount(); ++i) {
        const Row& r = rows_[static_cast<size_t>(i)];
        const bool foc = i == focus_, hov = i == hover_ && r.enabled;
        switch (r.kind) {
            case RowKind::Action:
                p.button(d, r.rect, r.label, foc, hov, r.enabled, TextAlign::Center);
                break;
            case RowKind::Info:
                if (r.id == RowId::JoinHeader) { // the server table's columns (OotF: Server Name / Current Level /
                                                 // Players / Ping)
                    const float x = r.rect.x;
                    p.label(d, {x + 20, r.rect.y, 330, r.rect.h}, "SERVER NAME", th.brass);
                    p.label(d, {x + 360, r.rect.y, 300, r.rect.h}, "CURRENT LEVEL", th.brass);
                    p.label(d, {x + 700, r.rect.y, 120, r.rect.h}, "PLAYERS", th.brass, TextAlign::Center);
                    p.label(d, {x + 880, r.rect.y, 90, r.rect.h}, "PING", th.brass, TextAlign::Right);
                } else if (r.id == RowId::LobbyPlayer) {
                    const RosterRow& pl = st.roster[static_cast<size_t>(r.index)];
                    d.panel(p.map(r.rect), PanelStyle::Inset, withAlpha(th.inset, 200));
                    std::string name = pl.name + (pl.self ? "  (you)" : "") + (pl.host ? "  [host]" : "");
                    p.text(d, {r.rect.x + 24, r.rect.y, r.rect.w * 0.6f, r.rect.h}, name, FontRole::Body,
                           pl.self ? th.heading : th.text);
                    if (pl.team >= 0) { // OotF's default team names
                        static const char* const kTeams[] = {"Fire", "Water", "Earth", "Air"};
                        p.text(d, {r.rect.x + r.rect.w * 0.62f, r.rect.y, 160, r.rect.h}, kTeams[pl.team & 3],
                               FontRole::Small, th.textMuted);
                    }
                    p.text(d, {r.rect.right() - 224, r.rect.y, 200, r.rect.h}, pl.ready ? "Ready" : "Not ready",
                           FontRole::Small, pl.ready ? th.good : th.textMuted, TextAlign::Right);
                } else {
                    p.text(d, {r.rect.x + 8, r.rect.y, r.rect.w - 16, r.rect.h}, r.label, FontRole::Body,
                           r.id == RowId::LobbyInfo ? th.textMuted : th.text);
                }
                break;
            case RowKind::List: {
                const JoinEntry& s = entries_[static_cast<size_t>(r.index)];
                d.panel(p.map(r.rect), PanelStyle::Card, hov ? th.panelRaised : th.panel);
                if (foc)
                    p.focus(d, p.map(r.rect));
                // OotF row colours: blue LAN, white custom ("+" prefix), green selected.
                const Color c = foc ? th.good : (s.custom ? th.text : rgba(122, 172, 232));
                const float x = r.rect.x;
                p.text(d, {x + 20, r.rect.y, 330, r.rect.h}, s.name + (s.locked ? "  [password]" : ""), FontRole::Body,
                       c);
                p.text(d, {x + 360, r.rect.y, 330, r.rect.h}, s.known ? s.level : "-", FontRole::Small, th.textMuted);
                p.text(d, {x + 700, r.rect.y, 120, r.rect.h},
                       s.known ? std::to_string(s.players) + "/" + std::to_string(s.maxPlayers) : "-", FontRole::Small,
                       th.text, TextAlign::Center);
                p.text(d, {x + 880, r.rect.y, 90, r.rect.h}, s.known ? std::to_string(s.pingMs) : "-", FontRole::Small,
                       th.textMuted, TextAlign::Right);
                break;
            }
            default: { // Choice / Number / Toggle / Text: label left, value right
                d.panel(p.map(r.rect), PanelStyle::Card,
                        !r.enabled ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
                if (foc)
                    p.focus(d, p.map(r.rect));
                const Color lc = !r.enabled ? th.textDisabled : (foc ? th.heading : th.text);
                p.text(d, {r.rect.x + 28, r.rect.y, 400, r.rect.h}, r.label, FontRole::Body, lc);
                std::string v = r.value;
                if (r.kind == RowKind::Text && foc && editing_)
                    v += (static_cast<long long>(st.now * 2) & 1) ? "_" : " ";
                const bool arrows = foc && !editing_ && r.enabled && r.kind != RowKind::Text;
                if (arrows)
                    v = "<  " + v + "  >";
                Rect vr{r.rect.x + 430, r.rect.y, r.rect.w - 430 - 28, r.rect.h};
                if (r.id == RowId::MenuColour) {
                    const Rect sw{vr.right() - 44, r.rect.y + 9, 44, r.rect.h - 18};
                    d.rect(p.map(sw), colourRgba(setup_.colour), 4);
                    d.outline(p.map(sw), th.gilt, 1.5f, 4);
                    vr.w -= 60;
                }
                const bool placeholder =
                    r.value.find("coming") != std::string::npos || r.value.find("gone") != std::string::npos;
                p.text(d, vr, v, FontRole::Body,
                       !r.enabled        ? th.textDisabled
                       : editing_ && foc ? th.ember
                       : placeholder     ? th.textMuted
                                         : (foc ? th.heading : th.brass),
                       TextAlign::Right);
                break;
            }
        }
    }
    if (page_ == MpPage::Join && showInfo_ && !entries_.empty()) { // Server Info
        const JoinEntry& e =
            entries_[static_cast<size_t>(std::clamp(selectedServer_, 0, static_cast<int>(entries_.size()) - 1))];
        p.confirmCard(d, design::pause::cardPanel(2), "Server Info",
                      "IP Address " + e.address + "   Level " + (e.known ? e.level : std::string("-")) + "\nPlayers " +
                          (e.known ? std::to_string(e.players) + "/" + std::to_string(e.maxPlayers) : "-") + "   " +
                          modeName(e.mode) + (e.locked ? "   password" : ""));
    }
    if (loadingCard)
        p.confirmCard(d, design::pause::cardPanel(2), "Loading...",
                      st.level.empty() ? std::string("The match is starting.") : "Loading " + st.level + ".");

    // Footer hints.
    std::vector<Hint> hs = hints;
    const std::vector<Rect> rs =
        p.layoutHints(hs, kDesignW - design::mp::kMargin, false, design::mp::kFooterY, design::mp::kHintH,
                      design::mp::kHintGlyph, design::mp::kHintGap, FontRole::Body);
    for (size_t i = 0; i < hs.size(); ++i)
        p.drawHint(d, hs[i], p.map(rs[i]), false, design::mp::kHintGlyph, FontRole::Body);
}

// ---- in-game HUD ----

std::vector<RosterRow> sortedRoster(std::vector<RosterRow> r) {
    std::stable_sort(r.begin(), r.end(), [](const RosterRow& a, const RosterRow& b) {
        if (a.kills != b.kills)
            return a.kills > b.kills;
        if (a.deaths != b.deaths)
            return a.deaths < b.deaths;
        return a.id < b.id;
    });
    return r;
}

DrawList drawHud(const HudState& hs, const Viewport& vp, const ScreenConfig& cfg, float chatLifeSec, int maxLines,
                 float killLifeSec) {
    DrawList d;
    const LayoutFrame lf = computeFrame(vp);
    const detail::Paint p(lf, cfg);
    const Theme& th = cfg.theme;
    if (hs.scoreboard || hs.results) {
        const std::vector<RosterRow> rows = sortedRoster(hs.roster);
        const float w = 980, rowH = 44;
        const float h = 150 + static_cast<float>(std::max<size_t>(rows.size(), 1)) * rowH + 40;
        const Rect box{(kDesignW - w) * 0.5f, 120, w, h};
        d.rect({lf.view.x, lf.view.y, lf.view.w, lf.view.h}, withAlpha(th.backdropEdge, 110));
        d.panel(p.map(box), PanelStyle::Main, withAlpha(th.panel, 230));
        std::string head = hs.serverName.empty() ? std::string("Scoreboard") : hs.serverName;
        p.text(d, {box.x + 32, box.y + 16, box.w - 64, 50}, head, FontRole::Title, th.heading);
        std::string sub = std::string(modeName(hs.mode)) + "   " + hs.level;
        if (hs.fragLimit > 0)
            sub += "   Frag limit " + std::to_string(hs.fragLimit);
        if (hs.timeLeftSec >= 0)
            sub += "   Time left " + std::to_string(hs.timeLeftSec / 60) + ":" + (hs.timeLeftSec % 60 < 10 ? "0" : "") +
                   std::to_string(hs.timeLeftSec % 60);
        p.text(d, {box.x + 32, box.y + 64, box.w - 64, 30}, sub, FontRole::Small, th.textMuted);
        p.rule(d, box.x + 32, box.y + 102, box.w - 64);
        // OotF scoreboard columns: Player, Kills, Deaths, Ping, Time.
        p.label(d, {box.x + 40, box.y + 108, 360, 24}, "PLAYER", th.brass);
        p.label(d, {box.x + 440, box.y + 108, 110, 24}, "KILLS", th.brass, TextAlign::Right);
        p.label(d, {box.x + 570, box.y + 108, 120, 24}, "DEATHS", th.brass, TextAlign::Right);
        p.label(d, {box.x + 710, box.y + 108, 100, 24}, "PING", th.brass, TextAlign::Right);
        p.label(d, {box.x + 830, box.y + 108, 110, 24}, "TIME", th.brass, TextAlign::Right);
        float y = box.y + 140;
        for (const RosterRow& r : rows) {
            const Rect rr{box.x + 28, y, box.w - 56, rowH - 4};
            d.panel(p.map(rr), r.self ? PanelStyle::Card : PanelStyle::Inset,
                    r.self ? th.panelRaised : withAlpha(th.inset, 190));
            static const char* const kTeams[] = {"Fire", "Water", "Earth", "Air"};
            p.text(d, {rr.x + 14, rr.y, 360, rr.h},
                   r.name + (r.team >= 0 ? std::string("  (") + kTeams[r.team & 3] + ")" : std::string()),
                   FontRole::Body, r.self ? th.heading : th.text);
            char tm[16];
            std::snprintf(tm, sizeof tm, "%d:%02d", r.timeSec / 60, r.timeSec % 60);
            p.text(d, {rr.x + 412, rr.y, 110, rr.h}, std::to_string(r.kills), FontRole::Numeric, th.text,
                   TextAlign::Right);
            p.text(d, {rr.x + 542, rr.y, 120, rr.h}, std::to_string(r.deaths), FontRole::Numeric, th.textMuted,
                   TextAlign::Right);
            p.text(d, {rr.x + 682, rr.y, 100, rr.h}, r.pingMs < 0 ? std::string("-") : std::to_string(r.pingMs),
                   FontRole::Numeric, th.textMuted, TextAlign::Right);
            p.text(d, {rr.x + 802, rr.y, 110, rr.h}, tm, FontRole::Numeric, th.textMuted, TextAlign::Right);
            y += rowH;
        }
        if (hs.mode == GameMode::TeamDeathmatch)
            p.text(d, {box.x + 32, box.bottom() - 44, box.w - 64, 32},
                   "Team A " + std::to_string(hs.teamKills[0]) + "   Team B " + std::to_string(hs.teamKills[1]),
                   FontRole::Body, th.brass, TextAlign::Center);
    }
    if (!hs.banner.empty()) { // OotF: no results screen, a banner before the next level
        d.panel(p.map({460, 20, 1000, 80}), PanelStyle::Main, withAlpha(th.panelRaised, 235));
        p.text(d, {480, 24, 960, 44}, hs.banner, FontRole::Heading, th.heading, TextAlign::Center);
        if (!hs.resultsText.empty())
            p.text(d, {480, 66, 960, 28}, hs.resultsText, FontRole::Small, th.textMuted, TextAlign::Center);
    }
    if (!hs.status.empty() && !hs.results) { // limits chip, top centre
        d.panel(p.map({760, 16, 400, 44}), PanelStyle::Inset, withAlpha(th.inset, 200));
        p.text(d, {770, 16, 380, 44}, hs.status, FontRole::Body, th.text, TextAlign::Center);
    }
    { // Kill feed: top right, newest at the bottom, fading.
        std::vector<const HudKill*> ks;
        for (const HudKill& k : hs.kills)
            if (hs.now - k.at < killLifeSec)
                ks.push_back(&k);
        if (ks.size() > 5)
            ks.erase(ks.begin(), ks.end() - 5);
        float ky = 20;
        for (const HudKill* k : ks) {
            const double age = hs.now - k->at;
            const uint8_t a =
                age < killLifeSec - 1.5 ? 255 : static_cast<uint8_t>(255 * std::max(0.0, (killLifeSec - age) / 1.5));
            const std::string line = k->killer.empty() ? k->victim + " died"
                                                       : k->killer + " killed " + k->victim +
                                                             (k->weapon.empty() ? std::string() : " with " + k->weapon);
            const bool mine = k->selfKiller || k->selfVictim;
            d.rect(p.map({1180, ky, 700, 34}), withAlpha(rgba(0, 0, 0), static_cast<uint8_t>(a / 2)));
            if (mine)
                d.outline(p.map({1180, ky, 700, 34}), withAlpha(k->selfKiller ? th.select : th.brass, a), 1.5f);
            p.text(d, {1190, ky, 680, 34}, line, FontRole::Small,
                   withAlpha(k->selfKiller   ? th.heading
                             : k->selfVictim ? th.brass
                                             : th.text,
                             a),
                   TextAlign::Right);
            ky += 38;
        }
    }
    if (!hs.alive && !hs.results) { // death overlay with the respawn countdown
        d.rect({lf.view.x, lf.view.y, lf.view.w, lf.view.h}, withAlpha(rgba(60, 0, 0), 90));
        d.panel(p.map({610, 420, 700, 150}), PanelStyle::Main, withAlpha(th.panelRaised, 235));
        p.text(d, {630, 430, 660, 50}, "You were killed", FontRole::Heading, th.heading, TextAlign::Center);
        if (!hs.deathText.empty())
            p.text(d, {630, 478, 660, 30}, hs.deathText, FontRole::Small, th.textMuted, TextAlign::Center);
        char cd[48];
        std::snprintf(cd, sizeof cd, "Respawning in %d", std::max(1, static_cast<int>(std::ceil(hs.respawnInSec))));
        p.text(d, {630, 512, 660, 44}, hs.respawnInSec > 0 ? std::string(cd) : std::string("Respawning ..."),
               FontRole::Body, th.brass, TextAlign::Center);
    }
    // Chat log: bottom left, fading.
    std::vector<const HudChatLine*> lines;
    for (const HudChatLine& c : hs.chat)
        if (hs.chatOpen || hs.now - c.at < chatLifeSec)
            lines.push_back(&c);
    if (static_cast<int>(lines.size()) > maxLines)
        lines.erase(lines.begin(), lines.end() - maxLines);
    float y = 720 - static_cast<float>(lines.size()) * 34;
    for (const HudChatLine* c : lines) {
        const double age = hs.now - c->at;
        const uint8_t a = hs.chatOpen || age < chatLifeSec - 1.5
                              ? 255
                              : static_cast<uint8_t>(255 * std::max(0.0, (chatLifeSec - age) / 1.5));
        d.rect(p.map({60, y, 760, 30}), withAlpha(rgba(0, 0, 0), static_cast<uint8_t>(a / 3)));
        // One run: "name: text", or just the text for a system line (joined / left).
        p.text(d, {72, y, 730, 30}, c->who.empty() ? c->text : c->who + ": " + c->text, FontRole::Small,
               withAlpha(c->who.empty() ? th.textMuted : th.text, a));
        y += 34;
    }
    if (hs.chatOpen) {
        d.panel(p.map({60, 740, 760, 44}), PanelStyle::Inset, withAlpha(th.inset, 230));
        d.outline(p.map({60, 740, 760, 44}), th.select, 1.5f);
        p.text(d, {76, 740, 730, 44}, "Say: " + hs.chatInput + ((static_cast<long long>(hs.now * 2) & 1) ? "_" : " "),
               FontRole::Body, th.text);
    }
    return d;
}

} // namespace gameui::mp
