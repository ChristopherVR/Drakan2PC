#pragma once
// In-process transport for tests and listen servers. A LoopbackNetwork owns a virtual clock (advance()) and the
// in-flight packets; LoopbackTransport endpoints created from it connect to each other by "port" number.
// Optional simulated one-way latency and unreliable-channel loss. The reliable channel is never lossy or reordered.
#include "net/Transport.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace drakan::net {

class LoopbackTransport;

class LoopbackNetwork {
public:
    struct Conditions {
        uint32_t latencyMs = 0;   // one way, constant (so ordering is preserved)
        double lossPercent = 0.0; // 0..100, unreliable channel only
        uint32_t seed = 1;
    };

    void setConditions(const Conditions& c);
    std::unique_ptr<LoopbackTransport> createTransport();
    // Move the virtual clock forward and deliver everything that became due.
    void advance(uint32_t ms);
    uint64_t nowMs() const { return now_; }

private:
    friend class LoopbackTransport;
    struct End {
        LoopbackTransport* owner = nullptr;
        PeerId remote = kInvalidPeer;
    };
    struct Packet {
        PeerId dest = kInvalidPeer;
        TransportEvent event;
    };

    PeerId newPeerId() { return nextPeer_++; }
    void enqueue(PeerId dest, TransportEvent ev);
    void deliverDue();
    uint32_t random();

    Conditions cond_;
    uint64_t now_ = 0;
    uint64_t seq_ = 0;
    uint32_t rng_ = 1;
    PeerId nextPeer_ = 1;
    std::map<std::pair<uint64_t, uint64_t>, Packet> pending_;
    std::unordered_map<PeerId, End> ends_;
    std::map<uint16_t, LoopbackTransport*> listeners_;
    uint16_t nextPort_ = 40000;
};

class LoopbackTransport final : public Transport {
public:
    ~LoopbackTransport() override;
    bool listen(uint16_t port) override;
    PeerId connect(const std::string& host, uint16_t port) override;
    void disconnect(PeerId peer) override;
    using Transport::send;
    bool send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& out) override;
    uint16_t localPort() const override { return port_; }

private:
    friend class LoopbackNetwork;
    explicit LoopbackTransport(LoopbackNetwork& net) : net_(net) {}

    LoopbackNetwork& net_;
    uint16_t port_ = 0;
    std::set<PeerId> peers_;
    std::deque<TransportEvent> inbox_;
};

} // namespace drakan::net
