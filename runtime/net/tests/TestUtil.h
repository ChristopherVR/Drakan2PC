#pragma once
// Shared helpers for the net tests: CHECK macro and a deterministic loopback rig (1 server, N clients).
#include "net/LoopbackTransport.h"
#include "net/MatchClient.h"
#include "net/MatchServer.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

namespace drakan::net::test {

constexpr uint16_t kPort = 27960;

struct Rig {
    LoopbackNetwork net; // declared first: outlives the transports
    std::unique_ptr<LoopbackTransport> serverTransport;
    std::unique_ptr<MatchServer> server;
    std::vector<std::unique_ptr<LoopbackTransport>> clientTransports;
    std::vector<std::unique_ptr<MatchClient>> clients;
    uint64_t now = 1000;

    explicit Rig(ServerOptions opt = {}) {
        serverTransport = net.createTransport();
        CHECK(serverTransport->listen(kPort));
        server = std::make_unique<MatchServer>(*serverTransport, std::move(opt));
    }

    MatchClient& addClient(const std::string& name, uint64_t hash = 0, const std::string& password = "") {
        ClientOptions co;
        co.name = name;
        co.profileHash = hash;
        co.password = password;
        clientTransports.push_back(net.createTransport());
        clients.push_back(std::make_unique<MatchClient>(*clientTransports.back(), co));
        CHECK(clients.back()->connect("127.0.0.1", kPort, now));
        return *clients.back();
    }

    void run(uint32_t ms, uint32_t stepMs = 10) {
        for (uint32_t t = 0; t < ms; t += stepMs) {
            now += stepMs;
            net.advance(stepMs);
            server->tick(now);
            for (auto& c : clients)
                c->tick(now);
            // second pass so a reply produced this step is seen without waiting for the next one
            server->tick(now);
            for (auto& c : clients)
                c->tick(now);
        }
    }
};

// Take everything queued on a client.
inline std::vector<ClientEvent> collect(MatchClient& c) {
    std::vector<ClientEvent> out;
    ClientEvent ev;
    while (c.pollEvent(ev))
        out.push_back(std::move(ev));
    return out;
}

template <class T>
size_t count(const std::vector<ClientEvent>& evs) {
    size_t n = 0;
    for (const auto& e : evs)
        if (std::holds_alternative<T>(e))
            ++n;
    return n;
}

template <class T>
const T* first(const std::vector<ClientEvent>& evs) {
    for (const auto& e : evs)
        if (auto* p = std::get_if<T>(&e))
            return p;
    return nullptr;
}

} // namespace drakan::net::test
