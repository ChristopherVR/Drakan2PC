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

// Game modes. Deathmatch, Team Deathmatch and Co-op exist in the netcode (wire modes 0, 1 and 2); the rest are shown
// greyed as "coming".
enum class GameMode : uint8_t { Deathmatch, TeamDeathmatch, DragonsDuel, DragonMaster, Coop, Count };
const char* modeName(GameMode m);
bool modeAvailable(GameMode m);

struct LevelEntry {
    std::string name;  // long level name the engine loads ("Level 1B - Kragmor"), or a custom map's name
    std::string title; // list label
    bool custom = false;
    // Multiplayer maps (arenas; the glue fills these from the arena definitions, campaign levels leave them empty).
    std::string description;
    std::string author;     // custom maps: the mod pack
    unsigned modes = 0;     // bit (1 << GameMode) per supported mode, 0 = every mode
    int maxPlayers = 0;     // 0 = unspecified
    int spawns = 0;         // spawn points, 0 = unknown
    uint32_t previewId = 0; // nonzero: the glue can draw a real picture for ImageKind::LevelPreview(previewId)
    bool arena = false;     // a multiplayer map (not a campaign level)
    bool supports(GameMode m) const { return modes == 0 || (modes & (1u << static_cast<unsigned>(m))) != 0; }
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
    bool coop = false;        // Game Type: Co-op (the mode is Coop; limits and teams do not apply)
    int teams = 0; // 0 = No Teams, 2, 3, 4 (3 and 4 are shown but cannot start yet: the server has two teams)
    bool friendlyFire = false;   // placeholder: the server never allows it in team games yet
    bool registerMaster = false; // placeholder: the master servers are gone
};

// A co-op character (the host plays Rynn, joiners pick another). The runtime side supplies the list
// (drakan_coop_character::roster(): class id, name, model); until it does the screen offers `placeholderCharacters()`.
struct CharacterEntry {
    int classId = 0;
    std::string name;
    std::string model;       // model name shown under the name
    std::string description; // one line
    bool hostOnly = false;   // Rynn: locked to the host
};
std::vector<CharacterEntry> placeholderCharacters();

struct SetupForm {
    int character = -1; // co-op character class id (-1 = the default: Rynn for the host, the first free one otherwise)
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
    std::string mods; // comma separated mod pack names the server runs ("" = none)
};

struct RosterRow {
    int id = 0;
    std::string name;
    int team = -1; // 0 / 1, -1 none
    bool ready = false;
    bool self = false;
    bool host = false;
    int kills = 0, deaths = 0;
    int colour = -1;           // Player Color index (colourRgba), -1 = derived from the id
    int pingMs = -1;           // -1 = unknown
    int timeSec = 0;           // time in the match
    std::string characterName; // co-op: the chosen character ("Rynn"), empty = not shown
};

enum class Conn : uint8_t { Offline, Connecting, Connected, Rejected };
enum class MatchPhaseView : uint8_t { Lobby, Loading, Playing, Results };

struct HudChatLine {
    std::string who, text;
    double at = 0; // seconds
};

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
    std::vector<CharacterEntry> characters;  // co-op character list (empty = placeholderCharacters())
    int selfCharacter = -1;                  // the local player's chosen class id as the session has it (-1 unknown)
    std::vector<LevelEntry> arenas;          // the multiplayer maps (arenas + mod pack maps); empty = use `levels`
    std::vector<HudChatLine> chat;           // lobby chat log (oldest first)
    double countdownSec = -1;                // the match starts in this many seconds (< 0 = no countdown)
    std::string defaultName;                 // initial Player Setup name
    int defaultColour = 0, defaultModel = 0; // and its colour / model (the saved profile)
    double now = 0;
};

enum class MpActionKind : uint8_t {
    HostMatch,    // host (the form), then the lobby
    JoinMatch,    // address + password
    Leave,        // drop the session
    StartMatch,   // the host starts from the lobby
    SetReady,     // flag
    RefreshLan,   // send a discovery sweep
    SaveProfile,  // Player Setup left: persist name / model / colour
    Close,        // Multiplayer -> Back: return to the title menu
    SendChat,     // a lobby chat line (MpAction::text)
    SetCharacter, // co-op: the local player's character (MpAction::character, a class id)
    KickPlayer,   // the host removes a player (MpAction::player, a roster id)
};
const char* mpActionName(MpActionKind k);

struct MpAction {
    MpActionKind kind = MpActionKind::Close;
    HostForm host;                 // HostMatch (levelName resolved below)
    std::string level;             // HostMatch: the chosen level's engine name
    std::string address, password; // JoinMatch
    SetupForm setup;               // HostMatch, JoinMatch, SaveProfile
    bool flag = false;             // SetReady
    std::string text;              // SendChat
    int character = -1;            // SetCharacter
    int player = 0;                // KickPlayer
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
    HostType,
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
    LobbyCharacter, // co-op: the character selector (host: Rynn, locked)
    LobbyChat,      // the chat line (Text row; Confirm sends it)
    LobbyReady,
    LobbyStart,
    LobbyLeave,
    LobbyKick, // host: Left / Right choose a player, Select removes them
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
    // open() on the lobby page of the session that is already up (the title comes back after a match).
    void openLobby(const MpState& st);
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
    // Preview model: the map the Host page shows (empty name = none), the selected Join server's level, and the
    // lobby's. The glue reads `previewRequests()` to know which pictures to load (LevelEntry::previewId).
    LevelEntry hostMap(const MpState& st) const; // empty name = no map
    std::vector<uint32_t> previewRequests(const MpState& st) const;
    int selectedServer() const { return selectedServer_; }
    bool mapListActive() const { return mapListMode_; } // Host: Up / Down step through the map list (Select toggles)
    GameMode hostMode() const; // the mode the Host form currently describes
    // Host page: pick a game mode card (mode cards are clickable; unavailable modes are ignored, returns false).
    bool selectMode(GameMode m, const MpState& st);
    const std::string& chatInput() const { return chatInput_; }
    // Co-op character select: the character the local player has (host: Rynn).
    CharacterEntry character(const MpState& st) const { return selectedCharacter(st); }
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
        std::string name, level, address, mods;
        int players = 0, maxPlayers = 0, pingMs = 0;
        bool custom = false, locked = false, known = false;
        GameMode mode = GameMode::Deathmatch;
    };
    std::vector<JoinEntry> entries_; // LAN answers sorted by ping, then the custom entries
    int focus_ = 0, hover_ = -1;
    bool ptrValid_ = false;
    float ptrX_ = 0, ptrY_ = 0; // last pointer position (pixels), for the mode cards' hover
    bool editing_ = false;
    std::vector<Row> rows_;
    LayoutFrame lf_;
    int listScroll_ = 0;
    int levelScroll_ = 0; // Host page: first visible map of the list
    bool mapListMode_ = false; // Host page: the map list holds Up / Down (entered with Select on the Map row)
    int autoChar_ = -1;        // co-op joiner: the class id the screen already announced on its own (see frame)
    int kickIdx_ = 0;          // Lobby: the player the Kick row would remove (index among the other players)
    std::string chatInput_;
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
    void layoutRows(const MpState& st);
    std::vector<CharacterEntry> allowedCharacters(const MpState& st) const;
    CharacterEntry selectedCharacter(const MpState& st) const;
    int rowOf(RowId id) const;
    std::string* textTarget(RowId id);
    void draw(const MpState& st, DrawList& d, const std::vector<Hint>& hints) const;
    MpAction baseAction(MpActionKind k, const MpState& st) const;
};

// In-game hooks (the PvP agent / netplay feed these): the scoreboard held on a key and the chat log.
struct HudKill {
    std::string killer, victim, weapon; // killer empty = environment / unknown
    double at = 0;                      // seconds, same clock as HudState::now
    bool selfKiller = false, selfVictim = false;
};
// Co-op: one other player of the party (up to 7 besides the local player).
struct HudPartner {
    int id = 0;
    std::string name;
    std::string character;   // co-op character name ("" = not shown)
    int colour = -1;         // Player Color index, -1 = derived from the id
    float hpFrac = 1;        // 0..1 health
    bool hpKnown = true;     // false: the session cannot say (no health bar / percentage is drawn)
    bool down = false;       // downed / dead, waiting for the respawn
    double respawnInSec = 0; // while down (0 = unknown)
    // Marker: where the partner is on the screen. `onScreen` positions are 0..1 of the 16:9 design canvas; otherwise
    // `dirDeg` is the direction to the partner on the screen plane (0 = right, 90 = down) and the marker sits on the
    // screen edge.
    bool markerValid = false, onScreen = false;
    float markerX = 0.5f, markerY = 0.5f, dirDeg = 0;
    float distance = 0; // metres, 0 = unknown
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
    std::vector<HudKill> kills;       // kill feed, newest last; lines older than killLifeSec fade out (top right)
    bool alive = true;                // the local player; false = the death overlay with the respawn countdown
    double respawnInSec = 0;          // while dead
    std::string deathText;            // "Killed by <name> (<weapon>)"
    std::vector<HudPartner> partners; // Co-op: the party's other players (drawn when mode == Coop)
    float selfHpFrac = 1;             // Co-op: the local player's health bar
    bool levelComplete = false;       // Co-op results: the level was completed (false = the party was defeated)
    bool results = false;             // the match is over: the standings stay up under the banner
    std::string resultsText;          // second banner line ("Next match: back to the lobby ...")
    double nextMatchSec = -1;         // results: the next match / lobby follows in this many seconds (< 0 = unknown)
    std::string status;               // top centre chip: "3:42 left   First to 3" (empty = none)
    std::string chatInput;            // while typing
    bool chatOpen = false;
    double now = 0;
};
// Draws the full-screen scoreboard (Player / Kills / Deaths / Ping / Time, sorted by kills, then fewer deaths; team
// columns in team games; the local player highlighted), the results screen (winner, standings, what happens next), the
// kill feed, the respawn overlay and the fading chat log (lines younger than `chatLifeSec`, the last `maxLines`), in
// the game's panel style, over the world.
DrawList drawHud(const HudState& hs, const Viewport& vp, const ScreenConfig& cfg, float chatLifeSec = 8.0f,
                 int maxLines = 6, float killLifeSec = 7.0f);
std::vector<RosterRow> sortedRoster(std::vector<RosterRow> r);

} // namespace gameui::mp

namespace gameui::design::mp {
Rect panel();     // the page panel (legacy single column; the pages lay out in Multiplayer.cpp)
float rowH();     // action / value row height
float rowStep();  // row pitch
Rect firstRow();  // top row rect (x, y, w, h) inside the panel
Rect titleRect(); // page title above the panel contents
inline constexpr float kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36, kMargin = 96;
} // namespace gameui::design::mp
