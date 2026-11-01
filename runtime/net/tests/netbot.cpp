// netbot: a scripted MatchClient over real UDP, for the dedicated server check
// (scripts/harness/dedicated_server_check.py).
//   netbot --port N --name NAME [--host H] [--ready] [--results CHOICE] [--seconds S]
// It joins, optionally sends Ready, publishes a state at 20 Hz, prints its events ("event phase=playing", "event
// kicked: ...") and, once the Results phase starts, sends the results choice (1 lobby, 2 next map, 3 rematch).
#include "net/MatchClient.h"
#include "net/UdpTransport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace drakan::net;

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", name = "Bot";
    int port = kDefaultPort, results = 0, seconds = 30;
    bool ready = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto val = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port")
            port = std::atoi(val());
        else if (a == "--name")
            name = val();
        else if (a == "--host")
            host = val();
        else if (a == "--ready")
            ready = true;
        else if (a == "--results")
            results = std::atoi(val());
        else if (a == "--seconds")
            seconds = std::atoi(val());
    }
    UdpTransport transport;
    if (!transport.available()) {
        std::fprintf(stderr, "no sockets\n");
        return 2;
    }
    ClientOptions co;
    co.name = name;
    MatchClient client(transport, co);
    const auto start = std::chrono::steady_clock::now();
    const auto nowMs = [&] {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };
    if (!client.connect(host, static_cast<uint16_t>(port), nowMs())) {
        std::fprintf(stderr, "cannot connect\n");
        return 2;
    }
    bool readySent = false, resultsSent = false;
    uint64_t lastState = 0;
    float x = 0;
    while (nowMs() < static_cast<uint64_t>(seconds) * 1000) {
        const uint64_t now = nowMs();
        client.tick(now);
        ClientEvent ev;
        while (client.pollEvent(ev)) {
            if (const auto* pc = std::get_if<PhaseChange>(&ev))
                std::printf("event phase=%d byHost=%d\n", static_cast<int>(pc->phase), pc->byHost ? 1 : 0);
            else if (std::get_if<DisconnectedEvent>(&ev))
                std::printf("event disconnected%s\n", client.rejected() ? " (rejected)" : "");
            else if (const auto* c = std::get_if<Chat>(&ev))
                std::printf("event chat %u: %s\n", c->player, c->text.c_str());
            std::fflush(stdout);
        }
        if (client.rejected()) {
            std::printf("event kicked: %s (reason %d)\n", client.rejectInfo().message.c_str(),
                        static_cast<int>(client.rejectInfo().reason));
            std::fflush(stdout);
            return 0;
        }
        if (client.connected()) {
            if (ready && !readySent && client.phase() == MatchPhase::Lobby)
                readySent = client.sendReady(true);
            if (client.phase() == MatchPhase::Loading)
                client.sendLoadDone();
            if (client.phase() == MatchPhase::Playing && now - lastState >= 50) {
                lastState = now;
                PlayerState s;
                x += 1.0f;
                s.pos[0] = x;
                client.sendState(s);
            }
            if (results && client.phase() == MatchPhase::Results && !resultsSent)
                resultsSent = client.sendResultsAction(static_cast<ResultsChoice>(results));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("event done\n");
    return 0;
}
