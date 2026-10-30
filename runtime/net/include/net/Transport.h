#pragma once
// Transport seam for the multiplayer layer. Std-only: no socket headers here.
//
// A transport moves opaque datagram-sized messages between peers on two channels:
//   Unreliable: may be lost or reordered (snapshots, player state).
//   Reliable:   delivered exactly once, in order, per peer (handshake, events).
// Messages are never larger than kMaxPacketPayload bytes; the protocol layer frames its own messages.
//
// Backends: LoopbackTransport (in-process, tests), UdpTransport (Winsock / BSD sockets). A browser build cannot use
// raw UDP; a WebSocket (reliable only; the unreliable channel may ride the same stream) or WebRTC data-channel
// backend implements this same interface in src/ (not written yet). Nothing above Transport may assume UDP
// semantics beyond the two channel guarantees stated here.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace drakan::net {

using PeerId = uint32_t;
constexpr PeerId kInvalidPeer = 0;
constexpr size_t kMaxPacketPayload = 1100;

enum class Channel : uint8_t { Unreliable = 0, Reliable = 1 };
enum class TransportEventType : uint8_t { Connected, Disconnected, Message };

struct TransportEvent {
    TransportEventType type = TransportEventType::Message;
    PeerId peer = kInvalidPeer;
    Channel channel = Channel::Reliable;
    std::vector<uint8_t> data; // Message only
};

class Transport {
public:
    virtual ~Transport() = default;

    // Accept incoming connections on `port` (0 = pick one, see localPort()). False if it cannot.
    virtual bool listen(uint16_t port) = 0;
    // Start connecting. Returns the peer id at once (kInvalidPeer if the address cannot be used); a Connected
    // event follows on success, a Disconnected event on failure or timeout.
    virtual PeerId connect(const std::string& host, uint16_t port) = 0;
    // Drop a peer. The remote side gets a Disconnected event; the local side gets none for this call.
    virtual void disconnect(PeerId peer) = 0;
    // Queue one message. False if the peer is unknown or not connected, or the payload is too large.
    virtual bool send(PeerId peer, Channel channel, const uint8_t* data, size_t size) = 0;
    // Pump the transport and fetch the next event. False when nothing is pending. Non-blocking.
    virtual bool poll(TransportEvent& out) = 0;
    // Bound port after listen() (or the local port of a client socket), 0 if none.
    virtual uint16_t localPort() const = 0;

    bool send(PeerId peer, Channel channel, const std::vector<uint8_t>& bytes) {
        return send(peer, channel, bytes.data(), bytes.size());
    }
};

} // namespace drakan::net
