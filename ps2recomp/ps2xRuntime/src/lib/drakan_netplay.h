// Drakan: networked ghosts and the multiplayer session control (docs/reference/netcode.md "Playing over the network",
// docs/reference/netcode.md "Multiplayer menu", docs/journal/2026-10-31/netplay-spike.md). Game instances see each
// other's Rynn as a ghost (drakan_ghost).
//
// Control: the title menu's Multiplayer pages call the API below (hostMatch / joinMatch / leave / state / lanServers /
// startMatch / setReady / sendChat). The environment variables stay as dev shortcuts that issue the same calls at
// start:
//   DRAKAN_NET_HOST=<port>          listen server: a drakan::net MatchServer lives in this process, the game joins it;
//                                   the match starts at once (one player, no limits)
//   DRAKAN_NET_CONNECT=<host:port>  client of a listen server or of drakan_server (start that with --auto-start 1)
//   DRAKAN_NET_NAME=<name>          player name (default "Rynn", at most 24 characters)
//   DRAKAN_MULTIPLAYER=0            turn the whole feature off (default on; the ghosts need no DRAKAN_GHOST then)
//
// Threads: all sockets, the server and the client live on one network thread that never touches guest memory. The EE
// thread, once per PadMgr_Update (beforePadMgr), publishes the local pose into a mutex-protected slot and applies the
// remote poses it finds in another one. The network thread sends the published PlayerState at 20 Hz. The API below is
// thread safe (UI thread, EE thread, network thread).
// Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_ghost.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class PS2Runtime;

namespace drakan_netplay {

void install(PS2Runtime& runtime); // subscribes the lifecycle hooks, starts the (idle) network thread; env shortcuts
bool enabled();                    // the module is installed and can host / join
bool wanted();                     // DRAKAN_MULTIPLAYER is not 0: drakan_ghost installs its hooks for it
// EE thread, at the PadMgr_Update boundary (after drakan_ghost::beforePadMgr).
void beforePadMgr(const uint8_t* rdram);

// ---- session control ----

struct HostConfig {
    std::string serverName = "Drakan server";
    std::string level;      // engine level name ("Level 1B - Kragmor"); the match is played there
    int mode = 0;           // 0 deathmatch, 1 team deathmatch (net::MatchMode)
    int maxPlayers = 8;     // 1..16 on the wire; the menu offers 2..8
    int fragLimit = 20;     // 0 = none
    int timeLimitSec = 600; // 0 = none
    std::string password;   // empty = open
    uint16_t port = 47800;  // UDP; LAN discovery probes 47800..47809
    bool autoStart = false; // start as soon as one player is in (the DRAKAN_NET_HOST shortcut)
};

enum class Conn : uint8_t { Idle, Connecting, Connected, Rejected };
enum class Phase : uint8_t { Lobby, Loading, Playing, Results };

struct Player {
    int id = 0;
    std::string name;
    int team = -1; // 0 / 1, -1 none
    bool ready = false, self = false, host = false;
    int kills = 0, deaths = 0;
    int pingMs = -1; // own player: the measured round trip; others unknown (-1)
    int timeSec = 0; // seconds since this client saw the player join
};

struct ChatLine {
    int player = 0; // 0 = system
    std::string who, text;
    double at = 0; // seconds, drakan_netplay's own clock
};

struct State {
    bool available = false; // can host / join (UDP transport, disc profile, hooks)
    std::string unavailableWhy;
    bool inSession = false; // hosting or joined (any Conn but Idle)
    bool hosting = false;
    Conn conn = Conn::Idle;
    Phase phase = Phase::Lobby;
    std::string message; // last reject / status text
    std::string serverName, level;
    int mode = 0, maxPlayers = 8, fragLimit = 0, timeLimitSec = 0;
    int selfId = 0;
    bool selfReady = false;
    std::vector<Player> roster;
    int teamKills[2] = {0, 0};
    std::string launchError; // the match level is not installed here (OotF: "level files mismatch")
    bool launching = false;  // the level load for a started match is in progress / waiting for the title
    double now = 0;
};

struct LanServer {
    std::string host;
    uint16_t port = 0;
    std::string name, level;
    int mode = 0, players = 0, maxPlayers = 8;
    bool passwordProtected = false, inProgress = false;
    int pingMs = 0;
};

// Open a listen server on cfg.port and join it as the host (the match waits in the lobby until startMatch(), or starts
// at once with cfg.autoStart). False when unavailable or already in a session.
bool hostMatch(const HostConfig& cfg, const std::string& playerName);
// Join a server ("host:port", port defaults to 47800). False when unavailable, bad address or already in a session.
bool joinMatch(const std::string& address, const std::string& password, const std::string& playerName);
void leave();      // drop the session (the server shuts down with its host)
State state();     // snapshot
bool startMatch(); // host only, in the lobby
void setReady(bool ready);
void sendChat(const std::string& text);
std::vector<ChatLine> chatLog(); // newest last, at most 64
// LAN discovery: refreshLan() sends a Query sweep (broadcast to ports 47800..47809 plus loopback); lanServers() returns
// the answers so far (an entry not heard for 8 s is dropped).
void refreshLan();
std::vector<LanServer> lanServers();
bool lanScanning();

// ---- match launch (the title menu's NewGame path with a level override) ----
// When the server enters Loading, the client asks the title screen to start a New Game with the match level standing in
// for the first level (hle::FileService::setLevelRedirect, the DRAKAN_START_LEVEL mechanism). The title glue polls this
// on the UI thread, performs the New Game action, and the intro cut scene is skipped by Start taps while launchTaps().
bool takeLaunchRequest(std::string& level); // one-shot; true when a New Game with `level` should be started now
bool launchTaps();                          // true while START should be tapped to skip the intro cut scene
// The file layer hook: the HLE file service (drakan_overrides) registers how to redirect a level name.
void setLevelRedirectFn(std::function<void(const std::string& from, const std::string& to)> fn);
// Levels the mod layer registered (custom maps), for the Host screen's map list; set once at start-up.
void setExtraLevels(std::vector<std::string> names);
std::vector<std::string> extraLevels();

// ---- combat (PvP, drakan_pvp) ----
// The server owns health, deaths, respawns and scores; the game reports claims and applies the verdicts.
// Thread safe. A melee / projectile hit of the local player on the ghost of `victimPlayer` (drakan::net::Hit.weapon:
// 0 right hand, 1 left hand, 2 projectile; `damage` as the engine computed it); the server validates it.
void sendHitClaim(uint8_t victimPlayer, uint8_t weapon, uint16_t damage);
// Ghost id <-> player id of the remote players currently shown (0 = none).
int playerOfGhost(int ghostId);
int ghostOfPlayer(int player);

struct ScoreRow {
    int id = 0;
    std::string name;
    int team = -1;
    int kills = 0, deaths = 0;
    int health = 0;
    bool alive = false, self = false;
};
struct KillFeedEntry {
    int killer = 0, victim = 0; // player ids (killer 0 = none)
    std::string killerName, victimName;
    int weapon = 0;
    double at = 0; // drakan_netplay's own clock (seconds), compare with Scoreboard::now
};
struct Scoreboard {
    bool inMatch = false; // connected and the phase is Playing or Results
    Phase phase = Phase::Lobby;
    std::vector<ScoreRow> rows; // most kills first, then fewest deaths
    int mode = 0, fragLimit = 0, timeLimitSec = 0;
    int timeLeftSec = -1; // -1 = no time limit or not playing
    int teamKills[2] = {0, 0};
    int selfHealth = 0, selfMaxHealth = 100;
    bool selfAlive = true;
    double respawnInSec = 0; // while dead: seconds until the server respawns us
    // Once phase == Results (the match is over):
    bool over = false, tie = false;
    int winnerId = 0, winnerTeam = -1;   // deathmatch / team deathmatch winner (0 / -1 when tied)
    std::string reason;                  // "frag limit", "time limit" or "match ended"
    std::vector<KillFeedEntry> killFeed; // newest last, the last 8 kills
    double now = 0;
};
Scoreboard scoreboard(); // snapshot for the scoreboard overlay / the results screen

} // namespace drakan_netplay
