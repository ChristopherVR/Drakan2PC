// drakan_server: headless dedicated match server. Runs MatchServer over UdpTransport at a fixed tick.
// No game, no ISO: it only tracks match state (see docs/reference/netcode.md).
#include "net/MatchServer.h"
#include "net/UdpTransport.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop = true;
}

void usage() {
    std::puts("usage: drakan_server [options]\n"
              "  --port N          UDP port (default 27960)\n"
              "  --max-players N   1..16 (default 8)\n"
              "  --mode dm|tdm     deathmatch or team deathmatch (default dm)\n"
              "  --level NAME      level name clients should load (default \"\")\n"
              "  --frag-limit N    kills to win, 0 = none (default 20)\n"
              "  --time-limit SEC  match length, 0 = none (default 600)\n"
              "  --auto-start N    start the match once N players joined (default 2, 0 = never)\n"
              "  --tick-rate HZ    server ticks per second (default 30)\n"
              "  --profile-hash H  require this disc profile hash (hex, default accept any)\n"
              "  --help");
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
    unsigned long long port = 27960;
    unsigned long long tickRate = 30;

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
            else
                return std::fprintf(stderr, "bad --mode (dm|tdm)\n"), 2;
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
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }
    opt.tickRateHz = static_cast<uint8_t>(tickRate);

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
    server.onLog = [&](const std::string& line) {
        std::printf("[%8.3f] %s\n", static_cast<double>(elapsedMs()) / 1000.0, line.c_str());
        std::fflush(stdout);
    };
    std::printf("drakan_server: protocol v%u, UDP port %u, %s, max %u players, tick %llu Hz\n", kProtocolVersion,
                transport.localPort(), opt.config.mode == MatchMode::Deathmatch ? "deathmatch" : "team deathmatch",
                opt.config.maxPlayers, tickRate);
    std::fflush(stdout);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const auto period = std::chrono::microseconds(1000000 / tickRate);
    auto next = std::chrono::steady_clock::now();
    while (!g_stop) {
        server.tick(elapsedMs());
        next += period;
        const auto now = std::chrono::steady_clock::now();
        if (next < now)
            next = now; // fell behind; do not burst
        std::this_thread::sleep_until(next);
    }
    std::puts("drakan_server: shutting down");
    return 0;
}
