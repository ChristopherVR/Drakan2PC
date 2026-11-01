// drakan_server: headless dedicated match server. Runs MatchServer over UdpTransport at a fixed tick.
// No game, no ISO: it only tracks match state (see docs/reference/netcode.md).
#include "net/ArenaMaps.h"
#include "net/GameModes.h"
#include "net/MatchServer.h"
#include "net/UdpTransport.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop = true;
}

void usage() {
    std::puts("usage: drakan_server [options]\n"
              "  --port N          UDP port (default 47800; LAN discovery probes 47800..47809)\n"
              "  --name TEXT       server name shown in server lists\n"
              "  --password TEXT   require this password to join\n"
              "  --max-players N   1..16 (default 8)\n"
              "  --mode dm|tdm|coop deathmatch, team deathmatch or co-op (default dm)\n"
              "  --level NAME      level name clients should load (default \"\"); a multiplayer map name or id\n"
              "                    (built in or from --maps) also gives the arena spawn points and pickups\n"
              "  --maps FILE       extra multiplayer map definitions (the mp_maps.ini format)\n"
              "  --frag-limit N    kills to win, 0 = none (default 20)\n"
              "  --time-limit SEC  match length, 0 = none (default 600)\n"
              "  --auto-start N    start the match once N players joined (default 2, 0 = never)\n"
              "  --tick-rate HZ    server ticks per second (default 30)\n"
              "  --profile-hash H  require this disc profile hash (hex, default accept any)\n"
              "  --damage-scale F  multiplier on accepted hit damage (default 3)\n"
              "  --respawn-ms N    respawn delay after a death, 500..60000 (default 3000)\n"
              "  --max-hit-range F melee reach check between the two players, game units (default 900)\n"
              "  --game-mode ID    custom game mode ([mode <id>] of the built-in list or --maps files): scoring,\n"
              "                    teams, friendly fire, capture / hold objectives; also sets the limits it names\n"
              "  --start-countdown SEC  countdown before a started match loads (default 5 with --auto-start)\n"
              "  --results-sec SEC how long the results stay up before the lobby returns (default 15)\n"
              "  --host-kick       the longest connected player may kick others and choose on the results screen\n"
              "  --status-sec N    print the player table (ping, score, team) every N seconds (default 0 = off)\n"
              "  --help\n"
              "console commands (stdin): status | kick <id|name> | start | end | help | quit");
}

const char* phaseText(drakan::net::MatchPhase p) {
    using drakan::net::MatchPhase;
    return p == MatchPhase::Lobby     ? "lobby"
           : p == MatchPhase::Loading ? "loading"
           : p == MatchPhase::Playing ? "playing"
                                      : "results";
}

// The player table of the console `status` command: id, name, team, ready, score, kills / deaths, ping.
std::string playerTable(const drakan::net::MatchServer& server) {
    using namespace drakan::net;
    std::ostringstream o;
    uint32_t ms = 0;
    const CountdownKind kind = server.countdown(ms);
    o << "phase " << phaseText(server.phase()) << ", " << server.players().size() << "/"
      << static_cast<int>(server.config().maxPlayers) << " players, level '" << server.config().level << "'";
    if (!server.config().modeId.empty())
        o << ", mode " << server.config().modeId;
    if (kind != CountdownKind::None)
        o << ", " << (kind == CountdownKind::MatchStart ? "match starts in " : "next match in ") << (ms + 999) / 1000
          << " s";
    o << "\n";
    o << "  id  name                      team  ready  score  k/d      ping\n";
    const PlayerId host = server.hostPlayer();
    for (const auto& kv : server.players()) {
        const ServerPlayer& p = kv.second;
        char line[160];
        char team[8];
        if (p.team == kNoTeam)
            std::snprintf(team, sizeof team, "-");
        else
            std::snprintf(team, sizeof team, "%d", p.team + 1);
        char ping[16];
        if (p.pingMs == kPingUnknown)
            std::snprintf(ping, sizeof ping, "?");
        else
            std::snprintf(ping, sizeof ping, "%u ms", p.pingMs);
        char kd[16];
        std::snprintf(kd, sizeof kd, "%u/%u", p.kills, p.deaths);
        std::snprintf(line, sizeof line, "  %-3u %-25s %-5s %-6s %-6d %-8s %s%s\n", p.id, p.name.c_str(), team,
                      p.ready ? "yes" : "no", p.score, kd, ping, p.id == host ? "  (host)" : "");
        o << line;
    }
    return o.str();
}

bool parseUint(const char* s, unsigned long long maxValue, unsigned long long& out, int base = 10) {
    char* end = nullptr;
    out = std::strtoull(s, &end, base);
    return end != s && *end == '\0' && out <= maxValue;
}

} // namespace

int main(int argc, char** argv) {
    using namespace drakan::net;
    ServerOptions opt;
    opt.autoStartPlayers = 2;
    applyDrakanPvpTuning(opt);
    unsigned long long port = kDefaultPort;
    unsigned long long tickRate = 30;
    std::string gameMode;
    unsigned long long statusSec = 0;
    bool startCountdownGiven = false;

    std::vector<std::string> mapFiles;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        unsigned long long v = 0;
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (a == "--port") {
            if (!parseUint(value(), 65535, port))
                return std::fprintf(stderr, "bad --port\n"), 2;
        } else if (a == "--max-players") {
            if (!parseUint(value(), kMaxPlayers, v) || v < 1)
                return std::fprintf(stderr, "bad --max-players (1..%zu)\n", kMaxPlayers), 2;
            opt.config.maxPlayers = static_cast<uint8_t>(v);
        } else if (a == "--mode") {
            const std::string m = value();
            if (m == "dm")
                opt.config.mode = MatchMode::Deathmatch;
            else if (m == "tdm")
                opt.config.mode = MatchMode::TeamDeathmatch;
            else if (m == "coop")
                opt.config.mode = MatchMode::Coop;
            else
                return std::fprintf(stderr, "bad --mode (dm|tdm|coop)\n"), 2;
        } else if (a == "--name") {
            opt.config.serverName = std::string(value()).substr(0, kMaxServerNameLength);
        } else if (a == "--password") {
            opt.password = value();
            if (opt.password.size() > kMaxPasswordLength)
                return std::fprintf(stderr, "--password too long\n"), 2;
        } else if (a == "--level") {
            opt.config.level = value();
            if (opt.config.level.size() > kMaxLevelLength)
                return std::fprintf(stderr, "--level too long\n"), 2;
        } else if (a == "--frag-limit") {
            if (!parseUint(value(), 65535, v))
                return std::fprintf(stderr, "bad --frag-limit\n"), 2;
            opt.config.fragLimit = static_cast<uint16_t>(v);
        } else if (a == "--time-limit") {
            if (!parseUint(value(), 65535, v))
                return std::fprintf(stderr, "bad --time-limit\n"), 2;
            opt.config.timeLimitSec = static_cast<uint16_t>(v);
        } else if (a == "--auto-start") {
            if (!parseUint(value(), kMaxPlayers, v))
                return std::fprintf(stderr, "bad --auto-start\n"), 2;
            opt.autoStartPlayers = static_cast<uint8_t>(v);
        } else if (a == "--tick-rate") {
            if (!parseUint(value(), 240, tickRate) || tickRate < 1)
                return std::fprintf(stderr, "bad --tick-rate (1..240)\n"), 2;
        } else if (a == "--profile-hash") {
            if (!parseUint(value(), ~0ULL, v, 16))
                return std::fprintf(stderr, "bad --profile-hash\n"), 2;
            opt.profileHash = v;
        } else if (a == "--damage-scale") {
            opt.damageScale = static_cast<float>(std::atof(value()));
            if (!(opt.damageScale >= 0.1f && opt.damageScale <= 100.0f))
                return std::fprintf(stderr, "bad --damage-scale (0.1..100)\n"), 2;
        } else if (a == "--respawn-ms") {
            if (!parseUint(value(), 60000, v) || v < 500)
                return std::fprintf(stderr, "bad --respawn-ms (500..60000)\n"), 2;
            opt.config.respawnDelayMs = static_cast<uint16_t>(v);
        } else if (a == "--game-mode") {
            gameMode = value();
        } else if (a == "--start-countdown") {
            if (!parseUint(value(), 60, v))
                return std::fprintf(stderr, "bad --start-countdown (0..60)\n"), 2;
            opt.startCountdownMs = static_cast<uint32_t>(v) * 1000u;
            startCountdownGiven = true;
        } else if (a == "--results-sec") {
            if (!parseUint(value(), 600, v) || v < 1)
                return std::fprintf(stderr, "bad --results-sec (1..600)\n"), 2;
            opt.resultsDurationMs = static_cast<uint32_t>(v) * 1000u;
        } else if (a == "--host-kick") {
            opt.hostKick = true;
        } else if (a == "--status-sec") {
            if (!parseUint(value(), 3600, statusSec))
                return std::fprintf(stderr, "bad --status-sec\n"), 2;
        } else if (a == "--maps") {
            mapFiles.emplace_back(value());
        } else if (a == "--max-hit-range") {
            opt.maxHitRange = static_cast<float>(std::atof(value()));
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }
    opt.tickRateHz = static_cast<uint8_t>(tickRate);
    if (!startCountdownGiven && opt.autoStartPlayers > 0)
        opt.startCountdownMs = 5000; // the console `start` and the lobby show a countdown like the listen server's
    ModeCatalog modeCatalog;

    { // multiplayer maps: the built-in list, then the --maps files (a later definition of an id wins)
        ArenaCatalog maps;
        std::vector<std::string> errors;
        maps.add(builtinArenaText(), "built-in", &errors);
        modeCatalog.add(builtinArenaText(), "built-in", &errors);
        for (const std::string& f : mapFiles) {
            std::ifstream in(f, std::ios::binary);
            if (!in) {
                std::fprintf(stderr, "cannot read --maps %s\n", f.c_str());
                return 2;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            maps.add(ss.str(), f, &errors);
            modeCatalog.add(ss.str(), f, &errors);
        }
        for (const std::string& e : errors)
            std::fprintf(stderr, "maps: %s\n", e.c_str());
        opt.rotation = maps.arenas(); // "Next map" on the results screen walks through the whole list
        opt.modes = modeCatalog.modes();
        if (!gameMode.empty()) {
            const ModeDef* mode = modeCatalog.find(gameMode);
            if (!mode)
                return std::fprintf(stderr, "unknown --game-mode '%s'\n", gameMode.c_str()), 2;
            applyModeToConfig(*mode, opt.config);
            std::printf("drakan_server: game mode '%s' (%s): score_kill %d, score_death %d, limit %u, %u teams, %zu "
                        "objectives\n",
                        mode->id.c_str(), mode->name.c_str(), mode->scoreKill, mode->scoreDeath, mode->fragLimit,
                        mode->teamCount(), mode->objectives.size());
        }
        if (const ArenaDef* arena = maps.find(opt.config.level)) {
            if (const ModeDef* mode = modeCatalog.find(gameMode); mode && !modeFitsArena(*mode, *arena)) {
                std::string why;
                modeFitsArena(*mode, *arena, &why);
                std::fprintf(stderr, "map '%s' does not fit game mode '%s': %s\n", arena->name.c_str(),
                             mode->id.c_str(), why.c_str());
                return 2;
            }
            if (opt.config.mode == MatchMode::Coop ||
                (gameMode.empty() && ((opt.config.mode == MatchMode::TeamDeathmatch && !arena->teamDeathmatch) ||
                                      (opt.config.mode == MatchMode::Deathmatch && !arena->deathmatch)))) {
                std::fprintf(stderr, "map '%s' does not support this mode\n", arena->name.c_str());
                return 2;
            }
            opt.hasArena = true;
            opt.arena = *arena;
            opt.config.level = arena->name;
            if (!(opt.config.modeId.size() && modeCatalog.find(opt.config.modeId) &&
                  modeCatalog.find(opt.config.modeId)->startHealth))
                opt.config.startHealth = arena->startHealth;
            opt.config.maxPlayers = std::min<uint8_t>(opt.config.maxPlayers, arena->maxPlayers);
            std::printf("drakan_server: multiplayer map '%s' (%s): %zu spawn points, %zu pickups\n",
                        arena->name.c_str(), arena->level.c_str(), arena->spawns.size(), arena->pickups.size());
        }
    }

    UdpTransport transport;
    if (!transport.available() || !transport.listen(static_cast<uint16_t>(port))) {
        std::fprintf(stderr, "cannot listen on UDP port %llu\n", port);
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto elapsedMs = [&] {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };

    MatchServer server(transport, opt);
    transport.setQueryResponder([&server](const std::vector<uint8_t>& req) { return server.answerQuery(req); });
    server.onLog = [&](const std::string& line) {
        std::printf("[%8.3f] %s\n", static_cast<double>(elapsedMs()) / 1000.0, line.c_str());
        std::fflush(stdout);
    };
    std::printf("drakan_server: protocol v%u, UDP port %u, %s, max %u players, tick %llu Hz\n", kProtocolVersion,
                transport.localPort(),
                opt.config.mode == MatchMode::Deathmatch ? "deathmatch"
                : opt.config.mode == MatchMode::Coop     ? "co-op"
                                                         : "team deathmatch",
                opt.config.maxPlayers, tickRate);
    std::fflush(stdout);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Console: lines from stdin, handled on the server thread (the MatchServer is not thread safe).
    std::mutex consoleM;
    std::vector<std::string> consoleLines;
    std::thread consoleThread([&] {
        std::string line;
        while (!g_stop && std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> l(consoleM);
            consoleLines.push_back(line);
        }
    });
    consoleThread.detach();
    const auto say = [&](const std::string& text) {
        std::printf("%s", text.c_str());
        std::fflush(stdout);
    };
    const auto handleConsole = [&](const std::string& line) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        std::string rest;
        std::getline(in >> std::ws, rest);
        if (cmd == "status" || cmd == "players") {
            say(playerTable(server));
        } else if (cmd == "kick") {
            if (rest.empty()) {
                say("usage: kick <id|name>\n");
                return;
            }
            PlayerId target = kNoPlayer;
            char* end = nullptr;
            const unsigned long id = std::strtoul(rest.c_str(), &end, 10);
            if (end != rest.c_str() && *end == '\0') {
                target = static_cast<PlayerId>(id);
            } else {
                for (const auto& kv : server.players())
                    if (kv.second.name == rest)
                        target = kv.first;
            }
            say(server.kick(target) ? "kicked player " + std::to_string(target) + "\n"
                                    : "no such player '" + rest + "'\n");
        } else if (cmd == "start") {
            say(server.startMatch(elapsedMs()) ? "starting\n"
                                               : "cannot start (not in the lobby, or nobody connected)\n");
        } else if (cmd == "end") {
            server.endMatch(elapsedMs());
        } else if (cmd == "lobby") {
            say("the results screen's Back to lobby is the host's button; use `end` and wait for the results timer\n");
        } else if (cmd == "quit" || cmd == "exit") {
            g_stop = true;
        } else if (cmd == "help" || cmd == "?") {
            say("commands: status | kick <id|name> | start | end | quit\n");
        } else if (!cmd.empty()) {
            say("unknown command '" + cmd + "' (help)\n");
        }
    };
    uint64_t lastStatusPrintMs = 0;
    int lastCountdownSec = -1;
    CountdownKind lastCountdownKind = CountdownKind::None;

    const auto period = std::chrono::microseconds(1000000 / tickRate);
    auto next = std::chrono::steady_clock::now();
    while (!g_stop) {
        server.tick(elapsedMs());
        {
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> l(consoleM);
                lines.swap(consoleLines);
            }
            for (const std::string& l : lines)
                handleConsole(l);
        }
        { // countdowns: one console line per second while the match start / the next match counts down
            uint32_t ms = 0;
            const CountdownKind kind = server.countdown(ms);
            const int sec = kind == CountdownKind::None ? -1 : static_cast<int>((ms + 999) / 1000);
            if (kind != lastCountdownKind || sec != lastCountdownSec) {
                if (kind != CountdownKind::None)
                    std::printf("[%8.3f] %s in %d s\n", static_cast<double>(elapsedMs()) / 1000.0,
                                kind == CountdownKind::MatchStart ? "match starts" : "next match (lobby)", sec),
                        std::fflush(stdout);
                lastCountdownKind = kind;
                lastCountdownSec = sec;
            }
        }
        if (statusSec > 0 && elapsedMs() - lastStatusPrintMs >= statusSec * 1000 && !server.players().empty()) {
            lastStatusPrintMs = elapsedMs();
            say(playerTable(server));
        }
        next += period;
        const auto now = std::chrono::steady_clock::now();
        if (next < now)
            next = now; // fell behind; do not burst
        std::this_thread::sleep_until(next);
    }
    std::puts("drakan_server: shutting down");
    return 0;
}
