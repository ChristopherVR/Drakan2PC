// Drakan: multiplayer menu glue: gameui::mp::MpState <- drakan_netplay::state(), MpAction -> the netplay API. The
// Multiplayer pages are part of the modern title menu (drakan_settings_title.cpp).
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
// ---- multiplayer menu glue: gameui::mp::MpState <- drakan_netplay::state(), MpAction -> the netplay API ----

struct MpProfile {
    std::string name;
    int colour = 0, model = 0, character = -1;
    bool loaded = false;
};
MpProfile g_mpProfile;
std::string mpProfilePath() {
    // A direct-dist harness must not share the player's persisted name/character between its instances.
    if (const char* path = std::getenv("DRAKAN_MULTIPLAYER_INI"); path && *path)
        return path;
    return drakan_cfg::exeDir() + "multiplayer.ini";
}
void loadMpProfile() {
    if (g_mpProfile.loaded)
        return;
    g_mpProfile.loaded = true;
    g_mpProfile.name = "Rynn";
    if (const char* n = std::getenv("DRAKAN_NET_NAME"); n && *n)
        g_mpProfile.name = n;
    std::ifstream in(mpProfilePath());
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' '))
            v.pop_back();
        while (!k.empty() && k.back() == ' ')
            k.pop_back();
        if (k == "name" && !v.empty() && !std::getenv("DRAKAN_NET_NAME"))
            g_mpProfile.name = v.substr(0, 24);
        else if (k == "colour")
            g_mpProfile.colour = std::atoi(v.c_str());
        else if (k == "model")
            g_mpProfile.model = std::atoi(v.c_str());
        else if (k == "character")
            g_mpProfile.character = std::atoi(v.c_str());
    }
    if (!std::getenv("DRAKAN_COOP_CHARACTER") && g_mpProfile.character >= 0 && g_mpProfile.character <= 0xffff)
        drakan_coop_character::setSelected(static_cast<uint16_t>(g_mpProfile.character));
}
void saveMpProfile(const gameui::mp::SetupForm& s) {
    g_mpProfile.name = s.name;
    g_mpProfile.colour = s.colour;
    g_mpProfile.model = s.model;
    g_mpProfile.character = s.character >= 0 ? s.character : drakan_coop_character::selected();
    std::ofstream out(mpProfilePath(), std::ios::trunc);
    out << "; Player Setup of the Multiplayer menu\nname = " << s.name << "\ncolour = " << s.colour
        << "\nmodel = " << s.model << "\ncharacter = " << g_mpProfile.character << "\n";
}

// The multiplayer maps (arenas): the catalogue of configs/mp_maps.ini compiled into drakan::net plus the player's own
// mp_maps.ini next to the exe. DRAKAN_MP_ARENAS=0 hides them (the Host page then lists the campaign levels).
std::vector<gameui::mp::LevelEntry> mpArenas() {
    using gameui::mp::GameMode;
    static std::vector<gameui::mp::LevelEntry> cache;
    static size_t cachedFrom = ~size_t(0);
    // The list of drakan_arena (built-in maps, the player's mp_maps.ini, the maps of the mod packs): maps whose base
    // level is not on this disc are left out (they cannot start). Rebuilt when the catalogue changes size.
    const char* off = std::getenv("DRAKAN_MP_ARENAS");
    const std::vector<drakan_arena::MapEntry> maps =
        off && off[0] == '0' ? std::vector<drakan_arena::MapEntry>() : drakan_arena::maps();
    const size_t modeCount = drakan_modes::customModes().modes().size();
    static size_t cachedModes = ~size_t(0);
    if (cachedFrom != maps.size() || cachedModes != modeCount) {
        cachedFrom = maps.size();
        cachedModes = modeCount;
        cache.clear();
        for (const drakan_arena::MapEntry& a : maps) {
            if (!a.installed)
                continue;
            gameui::mp::LevelEntry e;
            e.name = a.name;
            e.title = a.name;
            e.description = a.description;
            e.arena = true;
            e.custom = a.source != "built-in";
            e.author = e.custom ? a.source : std::string();
            e.game =
                !a.game.empty() ? a.game : (a.source.rfind("mod ", 0) == 0 ? std::string() : std::string("Drakan 2"));
            e.maxPlayers = a.maxPlayers;
            e.spawns = a.spawns;
            e.modes = (a.deathmatch ? 1u << static_cast<unsigned>(GameMode::Deathmatch) : 0u) |
                      (a.teamDeathmatch ? 1u << static_cast<unsigned>(GameMode::TeamDeathmatch) : 0u);
            if (const drakan::net::ArenaDef* def = drakan_arena::findMap(a.id)) {
                using Rules = drakan::net::OotfArenaRules;
                switch (def->ootfRules) {
                    case Rules::Air:
                        e.ootfGroup = "Air Only";
                        break;
                    case Rules::Ground:
                        e.ootfGroup = "Ground Only";
                        break;
                    case Rules::MasterOfTheDragon:
                        e.ootfGroup = "Master of the Dragon";
                        break;
                    case Rules::None:
                        break;
                }
                for (const drakan::net::ModeDef& m : drakan_modes::customModes().modes())
                    if (drakan::net::modeFitsArena(m, *def))
                        e.modeIds.push_back(m.id);
            }
            cache.push_back(std::move(e));
        }
    }
    std::vector<gameui::mp::LevelEntry> out = cache;
    for (gameui::mp::LevelEntry& e : out)
        e.previewId = mapPreviewId(e.name);
    return out;
}
} // namespace

gameui::mp::MpState mpState(bool wantLan) {
    using namespace gameui::mp;
    loadMpProfile();
    MpState s;
    const drakan_netplay::State ns = drakan_netplay::state();
    s.available = ns.available;
    s.unavailableWhy = ns.unavailableWhy.empty() ? std::string() : "Multiplayer is unavailable: " + ns.unavailableWhy;
    s.conn = ns.conn == drakan_netplay::Conn::Connected    ? Conn::Connected
             : ns.conn == drakan_netplay::Conn::Connecting ? (ns.inSession ? Conn::Connecting : Conn::Offline)
             : ns.conn == drakan_netplay::Conn::Rejected   ? Conn::Rejected
                                                           : Conn::Offline;
    s.hosting = ns.hosting;
    s.phase = static_cast<MatchPhaseView>(static_cast<int>(ns.phase));
    s.serverName = ns.serverName;
    s.level = ns.level;
    s.mode = ns.mode == 2 ? GameMode::Coop : ns.mode == 1 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
    s.maxPlayers = ns.maxPlayers;
    s.fragLimit = ns.fragLimit;
    s.timeLimitMin = (ns.timeLimitSec + 59) / 60; // a mode's 150 s is 3 min (the Host page rounds up too)
    s.modeId = ns.modeId;
    if (!ns.modeId.empty())
        if (const drakan::net::ModeDef* m = drakan_modes::customModes().find(ns.modeId)) {
            s.modeName = m->name;
            s.modeGoal = drakan::net::modeGoal(*m);
            for (size_t t = 0; t < 4; ++t) {
                if (t < m->teamNames.size())
                    s.teamNames[t] = m->teamNames[t];
                if (t < m->teamColors.size())
                    s.teamColors[t] = static_cast<int>(m->teamColors[t]);
            }
        }
    for (const drakan::net::ModeDef& m : drakan_modes::customModes().modes()) {
        ModeEntry e;
        e.id = m.id;
        e.name = m.name;
        e.description = m.description;
        e.teamBase = m.teamBase;
        e.teams = m.teamCount();
        e.fragLimit = m.fragLimit;
        e.timeLimitSec = m.timeLimitSec;
        e.lives = m.lives;
        e.goal = drakan::net::modeGoal(m);
        e.summary = drakan::net::modeSummary(m);
        s.modes.push_back(std::move(e));
    }
    s.selfReady = ns.selfReady;
    s.endNotice = ns.endNotice;
    s.message = ns.launching ? std::string("Starting the match ...") : ns.message;
    for (const auto& p : ns.roster) {
        RosterRow r;
        r.id = p.id;
        r.name = p.name;
        r.team = p.team;
        r.ready = p.ready;
        r.self = p.self;
        r.host = p.host;
        r.kills = p.kills;
        r.deaths = p.deaths;
        r.score = p.score;
        r.pingMs = p.pingMs;
        r.timeSec = p.timeSec;
        r.characterName = drakan_coop_character::entry(static_cast<uint16_t>(p.character)).name;
        s.roster.push_back(std::move(r));
    }
    s.countdownSec = ns.countdownKind == 1 ? ns.countdownSec : -1.0;
    for (const drakan_coop_character::Entry& e : drakan_coop_character::roster()) {
        CharacterEntry c;
        c.classId = e.id;
        c.name = e.name;
        if (e.id == drakan_coop_character::selected()) // only the shown one is loaded
            c.previewReady = characterPreviewReady(e.id);
        c.model = ((e.recordId & 0xffff0000u) == 0x20000u ? "SYSTEM.MOD #" : "NPCS.MOD #") + std::to_string(e.modelId);
        c.description = e.id == 0 ? "The dragon rider." : "The original game model, using its own animations.";
        s.characters.push_back(std::move(c));
    }
    s.selfCharacter = drakan_coop_character::selected();
    if (wantLan) {
        for (const auto& l : drakan_netplay::lanServers()) {
            LanServer o;
            o.host = l.host;
            o.port = l.port;
            o.name = l.name;
            o.level = l.level;
            o.mode = l.mode == 2 ? GameMode::Coop : l.mode == 1 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
            o.players = l.players;
            o.maxPlayers = l.maxPlayers;
            o.passwordProtected = l.passwordProtected;
            o.inProgress = l.inProgress;
            o.pingMs = l.pingMs;
            o.mods = l.mods;
            s.lan.push_back(std::move(o));
        }
        s.lanScanning = drakan_netplay::lanScanning();
    }
    const bool ootf = drakan_game::current() == drakan_game::Game::Ootf;
    const bool custom = drakan_game::current() == drakan_game::Game::Custom;
    if (!ootf && !custom)
        s.levels = vanillaLevels();
    else { // a campaign: co-op opens the campaign's start level first (drakan_game::firstLevel)
        LevelEntry e;
        e.name = drakan_game::firstLevel();
        e.title = e.name;
        s.levels.push_back(std::move(e));
    }
    // Order of the Flame packs also carry the Multiplayer/* levels as arena base levels: they are not campaign levels.
    std::vector<std::string> arenaLevels;
    if (ootf)
        for (const auto& m : drakan_arena::maps())
            arenaLevels.push_back(m.level);
    // A custom campaign lists its own levels in play order; otherwise every level the packs registered.
    for (const std::string& n : custom ? drakan_game::campaignLevels() : drakan_netplay::extraLevels()) {
        if ((ootf || custom) && n == drakan_game::firstLevel())
            continue;
        if (std::find(arenaLevels.begin(), arenaLevels.end(), n) != arenaLevels.end())
            continue;
        LevelEntry e;
        e.name = n;
        e.title = n.rfind("Level ", 0) == 0 ? n.substr(6) : n;
        e.custom = !ootf && !custom;
        s.levels.push_back(std::move(e));
    }
    for (LevelEntry& e : s.levels)
        e.previewId = mapPreviewId(e.name);
    s.arenas = mpArenas();
    for (const auto& c : drakan_netplay::chatLog()) {
        HudChatLine l;
        l.who = c.player == 0 ? std::string() : c.who;
        l.text = c.text;
        l.at = c.at;
        s.chat.push_back(std::move(l));
    }
    s.defaultName = g_mpProfile.name;
    s.defaultColour = g_mpProfile.colour;
    s.defaultModel = g_mpProfile.model;
    s.now = ns.now;
    return s;
}

void mpApply(const gameui::mp::MpAction& a, gameui::mp::MultiplayerScreen& screen) {
    using gameui::mp::MpActionKind;
    std::fprintf(stderr, "[menu-screens] multiplayer action %s\n", gameui::mp::mpActionName(a.kind));
    switch (a.kind) {
        case MpActionKind::HostMatch: {
            saveMpProfile(a.setup);
            drakan_netplay::HostConfig hc;
            hc.serverName = a.host.serverName;
            hc.level = a.level;
            hc.mode = a.host.mode == gameui::mp::GameMode::Coop             ? 2
                      : a.host.mode == gameui::mp::GameMode::TeamDeathmatch ? 1
                                                                            : 0;
            hc.maxPlayers = a.host.maxPlayers;
            hc.fragLimit = a.host.fragLimit;
            hc.timeLimitSec = a.host.timeLimitMin * 60;
            hc.password = a.host.password;
            hc.modeId = a.host.modeId;
            hc.useOotfGroup = a.host.levelGroup >= 3;
            drakan_netplay::hostMatch(hc, a.setup.name);
            break;
        }
        case MpActionKind::JoinMatch:
            saveMpProfile(a.setup);
            drakan_netplay::joinMatch(a.address, a.password, a.setup.name);
            break;
        case MpActionKind::Leave:
            drakan_netplay::leave();
            break;
        case MpActionKind::StartMatch:
            drakan_netplay::startMatch();
            break;
        case MpActionKind::SetReady:
            drakan_netplay::setReady(a.flag);
            break;
        case MpActionKind::RefreshLan:
            drakan_netplay::refreshLan();
            break;
        case MpActionKind::SaveProfile:
            saveMpProfile(a.setup);
            break;
        case MpActionKind::SendChat:
            drakan_netplay::sendChat(a.text);
            break;
        case MpActionKind::SetCharacter:
            std::fprintf(stderr, "[menu-screens] co-op character %d chosen\n", a.character);
            drakan_netplay::pickCharacter(static_cast<uint16_t>(a.character));
            break;
        case MpActionKind::KickPlayer:
            std::fprintf(stderr, "[menu-screens] kick player %d\n", a.player);
            drakan_netplay::kickPlayer(a.player);
            break;
        case MpActionKind::Close:
            break;
    }
    (void)screen;
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
