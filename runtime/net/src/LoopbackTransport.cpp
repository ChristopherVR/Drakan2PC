#include "net/LoopbackTransport.h"

#include <utility>

namespace drakan::net {

void LoopbackNetwork::setConditions(const Conditions& c) {
    cond_ = c;
    rng_ = c.seed ? c.seed : 1;
}

std::unique_ptr<LoopbackTransport> LoopbackNetwork::createTransport() {
    return std::unique_ptr<LoopbackTransport>(new LoopbackTransport(*this));
}

uint32_t LoopbackNetwork::random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void LoopbackNetwork::enqueue(PeerId dest, TransportEvent ev) {
    Packet p;
    p.dest = dest;
    p.event = std::move(ev);
    pending_.emplace(std::make_pair(now_ + cond_.latencyMs, seq_++), std::move(p));
    deliverDue();
}

void LoopbackNetwork::deliverDue() {
    while (!pending_.empty() && pending_.begin()->first.first <= now_) {
        Packet p = std::move(pending_.begin()->second);
        pending_.erase(pending_.begin());
        auto it = ends_.find(p.dest);
        if (it == ends_.end())
            continue; // endpoint went away
        LoopbackTransport* owner = it->second.owner;
        p.event.peer = p.dest;
        const bool gone = p.event.type == TransportEventType::Disconnected;
        owner->inbox_.push_back(std::move(p.event));
        if (gone) {
            owner->peers_.erase(p.dest);
            ends_.erase(it);
        }
    }
}

void LoopbackNetwork::advance(uint32_t ms) {
    now_ += ms;
    deliverDue();
}

LoopbackTransport::~LoopbackTransport() {
    const std::set<PeerId> peers = peers_;
    for (PeerId p : peers)
        disconnect(p);
    if (port_ != 0) {
        auto it = net_.listeners_.find(port_);
        if (it != net_.listeners_.end() && it->second == this)
            net_.listeners_.erase(it);
    }
}

bool LoopbackTransport::listen(uint16_t port) {
    if (port_ != 0)
        return false;
    if (port == 0) {
        while (net_.listeners_.count(net_.nextPort_))
            ++net_.nextPort_;
        port = net_.nextPort_++;
    }
    if (net_.listeners_.count(port))
        return false;
    net_.listeners_[port] = this;
    port_ = port;
    return true;
}

PeerId LoopbackTransport::connect(const std::string&, uint16_t port) {
    auto it = net_.listeners_.find(port);
    if (it == net_.listeners_.end())
        return kInvalidPeer;
    LoopbackTransport* server = it->second;
    const PeerId mine = net_.newPeerId();
    const PeerId theirs = net_.newPeerId();
    net_.ends_[mine] = {this, theirs};
    net_.ends_[theirs] = {server, mine};
    peers_.insert(mine);
    server->peers_.insert(theirs);
    TransportEvent ev;
    ev.type = TransportEventType::Connected;
    net_.enqueue(theirs, ev);
    net_.enqueue(mine, ev);
    return mine;
}

void LoopbackTransport::disconnect(PeerId peer) {
    auto it = net_.ends_.find(peer);
    if (it == net_.ends_.end() || it->second.owner != this)
        return;
    const PeerId remote = it->second.remote;
    net_.ends_.erase(it);
    peers_.erase(peer);
    TransportEvent ev;
    ev.type = TransportEventType::Disconnected;
    net_.enqueue(remote, ev);
}

bool LoopbackTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    if (size > kMaxPacketPayload)
        return false;
    auto it = net_.ends_.find(peer);
    if (it == net_.ends_.end() || it->second.owner != this)
        return false;
    const PeerId remote = it->second.remote;
    if (!net_.ends_.count(remote))
        return false;
    if (channel == Channel::Unreliable && net_.cond_.lossPercent > 0.0) {
        const double roll = (net_.random() % 100000) / 1000.0;
        if (roll < net_.cond_.lossPercent)
            return true; // lost in transit
    }
    TransportEvent ev;
    ev.type = TransportEventType::Message;
    ev.channel = channel;
    ev.data.assign(data, data + size);
    net_.enqueue(remote, std::move(ev));
    return true;
}

bool LoopbackTransport::poll(TransportEvent& out) {
    if (inbox_.empty())
        return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

} // namespace drakan::net
