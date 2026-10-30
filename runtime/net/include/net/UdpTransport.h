#pragma once
// UDP transport (IPv4, non-blocking). Winsock on Windows, BSD sockets elsewhere; none of that leaks into this header.
// Wire format of one datagram: 'D' 'K' type(u8) then per type:
//   Connect / ConnectAck / Disconnect: nothing
//   Unreliable: payload
//   Reliable:   seq(u32 LE) payload     (per-peer sequence, delivered in order)
//   Ack:        next expected reliable seq (u32 LE); cumulative, also the keep-alive
// Reliability is go-back-N flavoured: the receiver accepts only the next expected sequence and the sender
// resends unacked packets after a timeout. Not built for the browser (no raw UDP there); see Transport.h.
#include "net/Transport.h"

#include <memory>

namespace drakan::net {

class UdpTransport final : public Transport {
public:
    UdpTransport();
    ~UdpTransport() override;
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // False if sockets cannot be initialised on this host.
    bool available() const;

    bool listen(uint16_t port) override;
    PeerId connect(const std::string& host, uint16_t port) override;
    void disconnect(PeerId peer) override;
    using Transport::send;
    bool send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& out) override;
    uint16_t localPort() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drakan::net
