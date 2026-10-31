// The multiplayer menu: Multiplayer (Host Game, Join Game, Player Setup, Back), Host, Join (LAN list / direct address),
// Player Setup and Lobby, drawn in the title menu's style with the game's own font and skin art. Std only: the screen
// is a model (`MultiplayerScreen`) that gets an `MpState` snapshot of the network layer each frame and returns draw
// commands plus `MpAction`s for the glue (`drakan_netplay::hostMatch` / `joinMatch` / `leave` ...).
//
// The structure follows what is documented of Drakan: Order of the Flame's multiplayer (connection type LAN / Internet,
// host game setup, join with a server list, player setup, up to 8 players; docs/journal/2026-10-31/
// ootf-multiplayer-ui-spec.md). The documents do not describe the screens' pixel layout, so every position lives in
// `design::mp` (one place to re-layout when reference screenshots exist); no OotF art is used.
// Reference: docs/reference/netcode.md "Multiplayer menu".
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/Screen.h"

namespace gameui::mp {

inline constexpr int kMaxPlayers = 12; // OotF: 2..12 (Dragon.rrc)
inline constexpr int kMinPlayers = 2;

// Menu is the OotF "Multiplayer" screen: player name / colour / skins / connection speed plus Host Game and Join Game.
enum class MpPage : uint8_t { Menu, Host, Join, Lobby };

// Game modes. The first two exist in the netcode (wire modes 0 and 1); the rest are shown greyed as "coming".
enum class GameMode : uint8_t { Deathmatch, TeamDeathmatch, DragonsDuel, DragonMaster, Coop, Count };
const char* modeName(GameMode m);
bool modeAvailable(GameMode m);

struct LevelEntry {
    std::string name;  // long level name the engine loads ("Level 1B - Kragmor"), or a custom map's name
    std::string title; // list label
    bool custom = false;
};
// The disc's levels (the playable ones; cut scene levels are not matches), in campaign order.
std::vector<LevelEntry> vanillaLevels();

struct HostForm {
    std::string serverName = "Drakan server";
    std::string password; // blank = open
    GameMode mode =
        GameMode::Deathmatch; // derived: teams >= 2 is team deathmatch (OotF: the mode comes from the level)
    int level = 0;            // index into the level list filtered by `levelGroup`
    int levelGroup = 0;       // 0 All Levels, 1 disc levels, 2 custom (mod) levels
    int maxPlayers = 8;       // 2..12
    int fragLimit = 20;       // kills limit, 0 = No Limit
    int timeLimitMin = 10;    // 0 = No Limit
    int teams = 0; // 0 = No Teams, 2, 3, 4 (3 and 4 are shown but cannot start yet: the server has two teams)
    bool friendlyFire = false;   // placeholder: the server never allows it in team games yet
    bool registerMaster = false; // placeholder: the master servers are gone
};

struct SetupForm {
    std::string name = "Rynn";
    int colour = 0;     // Player Color
    int model = 0;      // Human Skin
    int dragonSkin = 0; // Dragon Skin (no effect until dragon matches exist)
    int speed = 4;      // Connection Speed (LAN / T1 default)
};
const std::vector<std::string>& modelNames(); // placeholder list: Rynn only until custom models land
const std::vector<std::string>& dragonSkinNames();
const std::vector<std::string>& speedNames();  // Modem 28.8k .. LAN/T1 1M
const std::vector<std::string>& colourNames(); // swatch names; colourRgba(i) gives the swatch
Color colourRgba(int i);

struct LanServer {
    std::string host; // dotted IPv4
    uint16_t port = 0;
    std::string name, level;
    GameMode mode = GameMode::Deathmatch;
    int players = 0, maxPlayers = 8;
    bool passwordProtected = false;
    bool inProgress = false;
    int pingMs = 0;
};

struct RosterRow {
    int id = 0;
    std::string name;
    int team = -1; // 0 / 1, -1 none
    bool ready = false;
    bool self = false;
    bool host = false;
    int kills = 0, deaths = 0;
    int pingMs = -1; // -1 = unknown
    int timeSec = 0; // time in the match
};

enum class Conn : uint8_t { Offline, Connecting, Connected, Rejected };
enum class MatchPhaseView : uint8_t { Lobby, Loading, Playing, Results };

// Snapshot of the network layer, built by the glue each frame.
struct MpState {
    bool available = true; // false: this build / disc profile cannot network (greyed, with `unavailableWhy`)
    std::string unavailableWhy;
    Conn conn = Conn::Offline;
    bool hosting = false;
    MatchPhaseView phase = MatchPhaseView::Lobby;
    std::string serverName, level;
    GameMode mode = GameMode::Deathmatch;
    int maxPlayers = 8, fragLimit = 0, timeLimitMin = 0;
    std::vector<RosterRow> roster;
    bool selfReady = false;
    std::string message; // status / rejection text ("wrong password", "Connecting to 192.168.1.4:47800 ...")
    std::vector<LanServer> lan;
    bool lanScanning = false;
    std::vector<LevelEntry> levels;          // the level catalogue (vanilla + mods); empty = vanillaLevels()
    std::string defaultName;                 // initial Player Setup name
    int defaultColour = 0, defaultModel = 0; // and its colour / model (the saved profile)
    double now = 0;
};

enum class MpActionKind : uint8_t {
    HostMatch,   // host (the form), then the lobby
    JoinMatch,   // address + password
    Leave,       // drop the session
    StartMatch,  // the host starts from the lobby
    SetReady,    // flag
    RefreshLan,  // send a discovery sweep
    SaveProfile, // Player Setup left: persist name / model / colour
    Close,       // Multiplayer -> Back: return to the title menu
};
const char* mpActionName(MpActionKind k);

struct MpAction {
    MpActionKind kind = MpActionKind::Close;
    HostForm host;                 // HostMatch (levelName resolved below)
    std::string level;             // HostMatch: the chosen level's engine name
    std::string address, password; // JoinMatch
    SetupForm setup;               // HostMatch, JoinMatch, SaveProfile
    bool flag = false;             // SetReady
};

enum class RowKind : uint8_t { Action, Choice, Number, Toggle, Text, Info, List };
enum class RowId : uint8_t {
    MenuName,
    MenuColour,
    MenuHumanSkin,
    MenuDragonSkin,
    MenuSpeed,
    MenuHost,
    MenuJoin,
    MenuMaster,
    MenuBack,
    HostName,
    HostPassword,
    HostGroup,
    HostLevel,
    HostMax,
    HostFrag,
    HostTime,
    HostTeams,
    HostFriendly,
    HostRegister,
    HostStart,
    HostBack,
    JoinHeader,
    JoinRefresh,
    JoinServer,
    JoinAdd,
    JoinNewAddress,
    JoinInfo,
    JoinPassword,
    JoinConnect,
    JoinBack,
    LobbyInfo,
    LobbyPlayer,
    LobbyReady,
    LobbyStart,
    LobbyLeave,
};

struct Row {
    RowId id = RowId::MenuBack;
    int index = 0; // JoinServer / LobbyPlayer: list position
    RowKind kind = RowKind::Action;
    std::string label, value;
    bool enabled = true;
    Rect rect; // design units
};

struct MpFrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<MpAction> actions;
    bool wantsClose = false; // Back on the Multiplayer page
    bool wantsText = false;  // a text field is being edited: the glue sends typed characters, not navigation letters
};

struct MpConfig {
    ScreenConfig screen;
    float panelW = 1100;
};

class MultiplayerScreen {
public:
    explicit MultiplayerScreen(MpConfig cfg = {});

    void open(const MpState& st);
    void close();
    bool isOpen() const { return open_; }

    MpFrameOutput frame(const MpState& st, const UiInput& in, const Viewport& vp);

    // Model access (tests, glue).
    MpPage page() const { return page_; }
    HostForm& host() { return host_; }
    const HostForm& host() const { return host_; }
    SetupForm& setup() { return setup_; }
    const SetupForm& setup() const { return setup_; }
    const std::vector<Row>& rows() const { return rows_; }
    int focus() const { return focus_; }
    bool editing() const { return editing_; }
    const std::vector<std::string>& customServers() const { return custom_; }
    bool showingInfo() const { return showInfo_; }
    void setPage(MpPage p, const MpState& st);

private:
    MpConfig cfg_;
    bool open_ = false;
    MpPage page_ = MpPage::Menu;
    HostForm host_;
    SetupForm setup_;
    std::string newAddress_ = "127.0.0.1:47800", joinPassword_;
    std::vector<std::string> custom_; // "Add Server" entries (host:port), shown with a "+" prefix
    bool addingServer_ = false, showInfo_ = false;
    int selectedServer_ = 0;
    struct JoinEntry {
        std::string name, level, address;
        int players = 0, maxPlayers = 0, pingMs = 0;
        bool custom = false, locked = false, known = false;
        GameMode mode = GameMode::Deathmatch;
    };
    std::vector<JoinEntry> entries_; // LAN answers sorted by ping, then the custom entries
    int focus_ = 0, hover_ = -1;
    bool editing_ = false;
    std::vector<Row> rows_;
    LayoutFrame lf_;
    int listScroll_ = 0;
    bool lobbyFocused_ = false;
    bool lobbyActive_ = false; // the session was seen up while on the lobby page (a later Offline = dropped)
    bool profileSeeded_ = false;

    void buildRows(const MpState& st);
    void buildEntries(const MpState& st);
    std::vector<LevelEntry> groupLevels(const MpState& st) const;
    int rowCount() const { return static_cast<int>(rows_.size()); }
    bool navigable(int i) const;
    void moveFocus(int dir);
    void ensureFocus();
    void activate(int i, const MpState& st, MpFrameOutput& out);
    void adjust(int i, int dir, const MpState& st, MpFrameOutput& out);
    void back(const MpState& st, MpFrameOutput& out);
    void typeChars(const UiInput& in, MpFrameOutput& out);
    void padEdit(UiKey k);
    bool stepList(int dir, const MpState& st);
    std::string* textTarget(RowId id);
    void draw(const MpState& st, DrawList& d, const std::vector<Hint>& hints) const;
    MpAction baseAction(MpActionKind k, const MpState& st) const;
};

// In-game hooks (the PvP agent / netplay feed these): the scoreboard held on a key and the chat log.
struct HudChatLine {
    std::string who, text;
    double at = 0; // seconds
};
struct HudState {
    bool scoreboard = false; // draw the scoreboard
    std::vector<RosterRow> roster;
    std::string serverName, level;
    GameMode mode = GameMode::Deathmatch;
    std::string banner; // OotF has no results screen, only a banner ("Game KILL limit reached!"); empty = none
    int fragLimit = 0;
    int timeLeftSec = -1; // -1 = no limit
    int teamKills[2] = {0, 0};
    std::vector<HudChatLine> chat;
    std::string chatInput; // while typing
    bool chatOpen = false;
    double now = 0;
};
// Draws the scoreboard (Player / Kills / Deaths / Ping / Time, sorted by kills, then fewer deaths) and the fading chat
// log (lines younger than `chatLifeSec`, the last `maxLines`), in the game's panel style, over the world.
DrawList drawHud(const HudState& hs, const Viewport& vp, const ScreenConfig& cfg, float chatLifeSec = 8.0f,
                 int maxLines = 6);
std::vector<RosterRow> sortedRoster(std::vector<RosterRow> r);

} // namespace gameui::mp

namespace gameui::design::mp {
Rect panel();     // the page panel
float rowH();     // action / value row height
float rowStep();  // row pitch
Rect firstRow();  // top row rect (x, y, w, h) inside the panel
Rect titleRect(); // page title above the panel contents
inline constexpr float kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36, kMargin = 96;
} // namespace gameui::design::mp
