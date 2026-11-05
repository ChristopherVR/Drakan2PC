// UDP localhost smoke test: connect, reliable ordered delivery (including a burst larger than the send window),
// unreliable delivery, MatchServer/MatchClient handshake, disconnect. Skips (exit 77) when sockets are unavailable.
#include "TestUtil.h"
#include "net/ServerBrowser.h"
#include "net/UdpTransport.h"

#include <chrono>
#include <thread>

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

uint64_t wallMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

template <class F>
bool waitFor(F&& done, uint32_t timeoutMs = 5000) {
    const uint64_t end = wallMs() + timeoutMs;
    while (wallMs() < end) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

} // namespace

int main() {
    UdpTransport server;
    UdpTransport client;
    if (!server.available() || !client.available() || !server.listen(0) || server.localPort() == 0) {
        std::puts("net_udp_smoke_test skipped: sockets unavailable");
        return 77;
    }
    const uint16_t port = server.localPort();

    // Raw transport behaviour.
    const PeerId toServer = client.connect("127.0.0.1", port);
    CHECK(toServer != kInvalidPeer);
    PeerId fromClient = kInvalidPeer;
    bool clientUp = false;
    CHECK(waitFor([&] {
        TransportEvent ev;
        while (server.poll(ev))
            if (ev.type == TransportEventType::Connected)
                fromClient = ev.peer;
        while (client.poll(ev))
            if (ev.type == TransportEventType::Connected)
                clientUp = true;
        return fromClient != kInvalidPeer && clientUp;
    }));

    constexpr int kBurst = 400; // more than the 128 packet send window
    for (int i = 0; i < kBurst; ++i) {
        const uint8_t msg[3] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), 0x5A};
        CHECK(client.send(toServer, Channel::Reliable, msg, sizeof(msg)));
    }
    uint8_t big[kMaxPacketPayload + 1] = {};
    CHECK(!client.send(toServer, Channel::Reliable, big, sizeof(big))); // too large
    CHECK(client.send(toServer, Channel::Unreliable, big, 100));

    int next = 0;
    bool gotUnreliable = false;
    CHECK(waitFor([&] {
        TransportEvent ev;
        client.poll(ev); // keep the client pumping (acks, resends)
        while (server.poll(ev)) {
            if (ev.type != TransportEventType::Message)
                continue;
            if (ev.channel == Channel::Unreliable) {
                gotUnreliable = ev.data.size() == 100;
                continue;
            }
            CHECK(ev.data.size() == 3);
            const int v = ev.data[0] | (ev.data[1] << 8);
            CHECK(v == next); // in order, exactly once
            ++next;
        }
        return next == kBurst && gotUnreliable;
    }));

    // Server -> client reliable.
    const uint8_t pong[] = {1, 2, 3, 4};
    CHECK(server.send(fromClient, Channel::Reliable, pong, sizeof(pong)));
    bool gotPong = false;
    CHECK(waitFor([&] {
        TransportEvent ev;
        server.poll(ev);
        while (client.poll(ev))
            if (ev.type == TransportEventType::Message && ev.data.size() == 4 && ev.data[3] == 4)
                gotPong = true;
        return gotPong;
    }));

    // Disconnect is seen by the other side.
    client.disconnect(toServer);
    bool serverSawDisconnect = false;
    CHECK(waitFor([&] {
        TransportEvent ev;
        while (server.poll(ev))
            if (ev.type == TransportEventType::Disconnected && ev.peer == fromClient)
                serverSawDisconnect = true;
        return serverSawDisconnect;
    }));

    // Full match handshake over real sockets.
    {
        UdpTransport st, ct;
        CHECK(st.listen(0));
        ServerOptions o;
        o.config.level = "udp";
        o.profileHash = 0x77;
        MatchServer ms(st, o);
        ClientOptions co;
        co.name = "Net";
        co.profileHash = 0x77;
        MatchClient mc(ct, co);
        const uint64_t t0 = wallMs();
        CHECK(mc.connect("127.0.0.1", st.localPort(), 0));
        CHECK(waitFor([&] {
            ms.tick(wallMs() - t0);
            mc.tick(wallMs() - t0);
            return mc.connected();
        }));
        CHECK(mc.config().level == "udp" && ms.players().size() == 1);
        mc.disconnect();
        CHECK(waitFor([&] {
            ms.tick(wallMs() - t0);
            return ms.players().empty();
        }));
    }

    // A listen host's session teardown destroys its server transport. The client receives a real disconnect,
    // rather than just a PlayerLeave (dedicated-server host migration), and cannot keep sending match state.
    {
        auto st = std::make_unique<UdpTransport>();
        CHECK(st->listen(0));
        auto ms = std::make_unique<MatchServer>(*st, ServerOptions{});
        UdpTransport ct;
        MatchClient mc(ct, ClientOptions{});
        const uint64_t t0 = wallMs();
        CHECK(mc.connect("127.0.0.1", st->localPort(), 0));
        CHECK(waitFor([&] {
            ms->tick(wallMs() - t0);
            mc.tick(wallMs() - t0);
            return mc.connected();
        }));
        collect(mc);
        ms.reset();
        st.reset();
        CHECK(waitFor([&] {
            mc.tick(wallMs() - t0);
            return !mc.connected();
        }));
        const auto events = collect(mc);
        CHECK(count<DisconnectedEvent>(events) == 1);
        CHECK(!first<DisconnectedEvent>(events)->wasRejected);
        CHECK(!mc.sendState(PlayerState{}));
    }

    // v5 discovery: a ServerBrowser queries a listening server (unicast and the loopback sweep) and gets its status.
    {
        UdpTransport st;
        CHECK(st.listen(kDefaultPort + 3)); // inside the probed port range
        ServerOptions o;
        o.config.level = "Level 2 - Shadowmire";
        o.config.serverName = "Browse me";
        o.config.maxPlayers = 6;
        o.password = "x";
        o.serverId = 0x5EED;
        MatchServer ms(st, o);
        st.setQueryResponder([&](const std::vector<uint8_t>& req) { return ms.answerQuery(req); });
        ServerBrowser browser;
        CHECK(browser.available());
        const uint64_t t0 = wallMs();
        browser.queryLan();
        CHECK(browser.queryHost("127.0.0.1", st.localPort()));
        CHECK(waitFor([&] {
            ms.tick(wallMs() - t0);
            return !browser.poll().empty();
        }));
        // The broadcast also reaches the server over its LAN adapter addresses and other servers may listen in the
        // probed range: answers carrying our serverId merge into exactly one entry (whichever address answered first
        // or fastest), with the right port and status.
        const ServerEntry* mine = nullptr;
        int ours = 0;
        for (const ServerEntry& e : browser.poll())
            if (e.info.serverId == 0x5EED) {
                mine = &e;
                ++ours;
            }
        CHECK(mine && ours == 1);
        CHECK(mine->port == st.localPort() && mine->ageMs < 3000);
        CHECK(mine->info.name == "Browse me" && mine->info.level == "Level 2 - Shadowmire" &&
              mine->info.maxPlayers == 6 && mine->info.passwordProtected && mine->info.players == 0);
        browser.clear();
        CHECK(browser.poll().empty());
    }

    std::puts("net_udp_smoke_test ok");
    return 0;
}
